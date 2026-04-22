#include "UsbHost/Src/ps_app_usbhost_impl_internal.h"

#include "Input/Inc/ps_app_input.h"

#include "xil_io.h"
#include "xil_printf.h"
#include "xinterrupt_wrap.h"

#include "usb_osal.h"
#include "usbh_hub.h"

PsAppUsbHostState *PsAppUsbHost_GetState(PsAppUsbHostImplContext *impl_ctx)
{
    if ((impl_ctx == NULL) || (impl_ctx->public_context_ptr == NULL) || (impl_ctx->public_context_ptr->state == NULL)) {
        return NULL;
    }
    return impl_ctx->public_context_ptr->state;
}

void PsAppUsbHost_ResetDeviceIdentity(PsAppUsbHostState *state)
{
    if (state == NULL) {
        return;
    }

    state->vendor_id = 0U;
    state->product_id = 0U;
    state->interface_number = 0U;
    state->port_speed = 0U;
    state->interface_class = 0U;
    state->interface_subclass = 0U;
    state->interface_protocol = 0U;
    state->ep_in_addr = 0U;
    state->ep_out_addr = 0U;
}

u32 PsAppUsbHost_GetRecoverCooldownTicks(u8 attempt_no)
{
    u32 cooldown;
    u32 cooldown_max;
    u8 idx;

    cooldown = PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_BASE;
    if (cooldown == 0U) {
        cooldown = 1U;
    }

    cooldown_max = PS_APP_USBHOST_ENUM_RETRY_COOLDOWN_MAX;
    if (cooldown_max < cooldown) {
        cooldown_max = cooldown;
    }

    if (attempt_no <= 1U) {
        return cooldown;
    }

    for (idx = 1U; idx < attempt_no; ++idx) {
        if (cooldown >= cooldown_max) {
            return cooldown_max;
        }
        if (cooldown > (cooldown_max >> 1U)) {
            cooldown = cooldown_max;
        } else {
            cooldown <<= 1U;
        }
    }

    if (cooldown > cooldown_max) {
        cooldown = cooldown_max;
    }
    return cooldown;
}

void PsAppUsbHost_ForceLogicalDetach(PsAppUsbHostImplContext *impl_ctx, const char *reason)
{
    PsAppUsbHostState *state;
    u8 had_binding = 0U;

    state = PsAppUsbHost_GetState(impl_ctx);
    if (state != NULL) {
        had_binding = ((state->device_present != 0U) ||
                       (state->xbox_interface_active != 0U)) ? 1U : 0U;
    }
    if ((had_binding == 0U) && (impl_ctx != NULL) && (impl_ctx->active_xbox_ptr != NULL)) {
        had_binding = 1U;
    }

    if (state != NULL) {
        /*
         * 这里只做“逻辑解绑”，不依赖物理拔插事件：
         * - 清空 device/xbox 状态
         * - 清空身份信息
         * - 通知 Input 释放按键
         * 用于兜底“物理层在位但上层状态机卡住”的场景。
         */
        state->device_present = 0U;
        state->xbox_interface_active = 0U;
        PsAppUsbHost_ResetDeviceIdentity(state);
        if ((had_binding != 0U) && (state->detach_count < 0xFFFFFFFFU)) {
            state->detach_count++;
        }
    }

    if (impl_ctx != NULL) {
        impl_ctx->active_xbox_ptr = NULL;
        impl_ctx->no_report_ticks = 0U;
        impl_ctx->waiting_first_input_after_attach = 0U;
        impl_ctx->enum_observe_ticks = 0U;
        impl_ctx->stale_detach_ticks = 0U;

        if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
            PsAppInput_OnUsbDetached(impl_ctx->public_context_ptr->input);
        }
    }

    if (had_binding != 0U) {
        xil_printf("[USBH] forced logical detach (%s)\r\n",
                   (reason != NULL) ? reason : "unknown");
    }
}

void PsAppUsbHost_RequestRootHubScan(void)
{
    struct usbh_bus *bus;

    bus = &g_usbhost_bus[PS_APP_USBHOST_BUS_ID];
    usbh_hub_thread_wakeup(&bus->hcd.roothub);
}

XStatus PsAppUsbHost_VbusBounceRecover(PsAppUsbHostImplContext *impl_ctx)
{
    PsAppUsbHostState *state;
    UINTPTR base_addr;
    u32 portsc;
    u8 raw_ccs;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((impl_ctx == NULL) || (state == NULL) ||
        (state->enabled == 0U) || (state->initialized == 0U)) {
        return XST_FAILURE;
    }

    base_addr = (UINTPTR)PS_APP_USBHOST_BASE_ADDR;

    /*
     * 关键兜底：
     * 当 physical=1 但 logical_present/active 都为 0 时，单纯 PortReset 可能不会产生
     * 可被 Hub 线程消费的连接变化事件（C_CONNECTION）。
     * 这里执行一次“VBUS 断电再上电”，制造明确的断开/连接沿，迫使根集线器重新枚举。
     */
    if (PsAppUsbHost_SetVbusDriveByBase(impl_ctx, base_addr, 0U) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    usb_osal_msleep(120U);

    PsAppUsbHost_ApplyPortKick(base_addr);
    if (PsAppUsbHost_SetVbusDriveByBase(impl_ctx, base_addr, 1U) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    usb_osal_msleep(30U);

    portsc = Xil_In32(base_addr + XUSBPS_PORTSCR1_OFFSET);
    raw_ccs = ((portsc & XUSBPS_PORTSCR_CCS_MASK) != 0U) ? 1U : 0U;
    impl_ctx->last_portsc = portsc;
    impl_ctx->phy_raw_ccs = raw_ccs;
    impl_ctx->phy_stable_ticks = 0U;
    impl_ctx->phy_connected = raw_ccs;

    PsAppUsbHost_RequestRootHubScan();
    return XST_SUCCESS;
}

void PsAppUsbHost_UpdateStateFromXbox(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class)
{
    PsAppUsbHostState *state;
    const struct usb_interface_descriptor *intf_desc;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (xbox_class == NULL) || (xbox_class->hport == NULL)) {
        return;
    }

    intf_desc = &xbox_class->hport->config.intf[xbox_class->intf].altsetting[0].intf_desc;

    state->vendor_id = xbox_class->hport->device_desc.idVendor;
    state->product_id = xbox_class->hport->device_desc.idProduct;
    state->interface_number = xbox_class->intf;
    state->ep_in_addr = (xbox_class->intin != NULL) ? xbox_class->intin->bEndpointAddress : 0U;
    state->ep_out_addr = (xbox_class->intout != NULL) ? xbox_class->intout->bEndpointAddress : 0U;
    state->interface_class = intf_desc->bInterfaceClass;
    state->interface_subclass = intf_desc->bInterfaceSubClass;
    state->interface_protocol = intf_desc->bInterfaceProtocol;
    state->port_speed = xbox_class->hport->speed;
    state->xbox_interface_active = 1U;
}

void PsAppUsbHost_OnEvent(u8 busid, u8 hub_index, u8 hub_port, u8 intf, u8 event)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;
    struct usbh_hubport *hport;

    impl_ctx = PsAppUsbHost_ImplGetContext();
    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (busid != PS_APP_USBHOST_BUS_ID)) {
        return;
    }

    state->event_count++;
    hport = usbh_find_hubport(busid, hub_index, hub_port);

    /*
     * 事件处理原则：
     * 1) 事件只更新“事实状态”和恢复计数，不直接做重操作（例如 reset）。
     * 2) 真正的恢复动作在 Service() 里集中执行，避免 IRQ/Hub 线程上下文过重。
     */
    switch (event) {
        case USBH_EVENT_DEVICE_CONNECTED:
            xil_printf("[USBH] event DEVICE_CONNECTED hub=%u port=%u intf=%u\r\n",
                       (unsigned int)hub_index,
                       (unsigned int)hub_port,
                       (unsigned int)intf);
            state->device_present = 1U;
            state->attach_count++;
            impl_ctx->recover_attempts = 0U;
            impl_ctx->recover_cooldown_ticks = 0U;
            impl_ctx->enum_observe_ticks = 0U;
            impl_ctx->stale_detach_ticks = 0U;
            break;
        case USBH_EVENT_DEVICE_DISCONNECTED:
        {
            u32 portsc_now;
            u8 raw_ccs_now;
            xil_printf("[USBH] event DEVICE_DISCONNECTED hub=%u port=%u intf=%u\r\n",
                       (unsigned int)hub_index,
                       (unsigned int)hub_port,
                       (unsigned int)intf);
            portsc_now = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
            raw_ccs_now = ((portsc_now & XUSBPS_PORTSCR_CCS_MASK) != 0U) ? 1U : 0U;
            xil_printf("[USBH] disconnect sample ccs=%u portsc=0x%08x\r\n",
                       (unsigned int)raw_ccs_now,
                       (unsigned int)portsc_now);
            if (raw_ccs_now != 0U) {
                /*
                 * 关键保护：
                 * 当根端口仍为连接态(CCS=1)时，DEVICE_DISCONNECTED 很可能是枚举重排/瞬态抖动。
                 * 这里先不立刻判死，交给延迟确认窗口与后续 scan 共同决策。
                 */
                state->device_present = 1U;
                impl_ctx->phy_connected = 1U;
                impl_ctx->phy_raw_ccs = 1U;
                impl_ctx->detach_defer_active = 1U;
                impl_ctx->detach_defer_ticks = PS_APP_USBHOST_DETACH_DEFER_TICKS;
                impl_ctx->recover_attempts = 0U;
                impl_ctx->recover_cooldown_ticks = 0U;
                impl_ctx->enum_observe_ticks = 0U;
                impl_ctx->stale_detach_ticks = 0U;
                PsAppUsbHost_RequestRootHubScan();
                xil_printf("[USBH] disconnect deferred (ccs=1, wait re-enum)\r\n");
                break;
            }
            state->device_present = 0U;
            state->detach_count++;
            PsAppUsbHost_ResetDeviceIdentity(state);
            impl_ctx->recover_attempts = 0U;
            impl_ctx->recover_cooldown_ticks = 0U;
            impl_ctx->enum_observe_ticks = 0U;
            impl_ctx->stale_detach_ticks = 0U;
            impl_ctx->no_report_ticks = 0U;
            impl_ctx->detach_defer_active = 0U;
            impl_ctx->detach_defer_ticks = 0U;
            impl_ctx->sideband_attempted_this_attach = 0U;
            if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
                PsAppInput_OnUsbDetached(impl_ctx->public_context_ptr->input);
            }
            break;
        }
        case USBH_EVENT_DEVICE_CONFIGURED:
            xil_printf("[USBH] event DEVICE_CONFIGURED hub=%u port=%u intf=%u\r\n",
                       (unsigned int)hub_index,
                       (unsigned int)hub_port,
                       (unsigned int)intf);
            if (hport != NULL) {
                u8 intf_count;
                u8 i;
                state->vendor_id = hport->device_desc.idVendor;
                state->product_id = hport->device_desc.idProduct;
                state->port_speed = hport->speed;

                intf_count = hport->config.config_desc.bNumInterfaces;
                xil_printf("[USBH] configured vid=0x%04x pid=0x%04x speed=%u intf=%u\r\n",
                           (unsigned int)state->vendor_id,
                           (unsigned int)state->product_id,
                           (unsigned int)state->port_speed,
                           (unsigned int)intf_count);
                for (i = 0U; i < intf_count; ++i) {
                    const struct usb_interface_descriptor *id =
                        &hport->config.intf[i].altsetting[0].intf_desc;
                    xil_printf("[USBH] intf%u cls=%02x/%02x/%02x eps=%u\r\n",
                               (unsigned int)i,
                               (unsigned int)id->bInterfaceClass,
                               (unsigned int)id->bInterfaceSubClass,
                               (unsigned int)id->bInterfaceProtocol,
                               (unsigned int)id->bNumEndpoints);
                }
            }
            /*
             * 注意：DEVICE_CONFIGURED 只代表 USB 枚举完成，不等于 xbox class 已激活。
             * 是否真正可输入，仍需结合 state->xbox_interface_active / active_xbox_ptr 判定。
             */
            impl_ctx->recover_attempts = 0U;
            impl_ctx->recover_cooldown_ticks = 0U;
            impl_ctx->enum_observe_ticks = 0U;
            break;
        case USBH_EVENT_INTERFACE_UNSUPPORTED:
            /*
             * 重要说明：
             * 对多接口设备（如 8BitDo 0x310B），MI_01/MI_02 常会触发 UNSUPPORTED，
             * 这本身不是错误；只要 MI_00 已由 xbox class 接管并持续有输入报告，
             * 就不应因为这些日志触发恢复或判定掉线。
             */
            xil_printf("[USBH] event INTERFACE_UNSUPPORTED hub=%u port=%u intf=%u\r\n",
                       (unsigned int)hub_index,
                       (unsigned int)hub_port,
                       (unsigned int)intf);
            if ((hport != NULL) &&
                (intf != USB_INTERFACE_ANY) &&
                (intf < hport->config.config_desc.bNumInterfaces)) {
                const struct usb_interface_descriptor *id =
                    &hport->config.intf[intf].altsetting[0].intf_desc;
                xil_printf("[USBH] intf unsupported idx=%u cls=%02x/%02x/%02x vid=0x%04x pid=0x%04x\r\n",
                           (unsigned int)intf,
                           (unsigned int)id->bInterfaceClass,
                           (unsigned int)id->bInterfaceSubClass,
                           (unsigned int)id->bInterfaceProtocol,
                           (unsigned int)hport->device_desc.idVendor,
                           (unsigned int)hport->device_desc.idProduct);
            }
            break;
        case USBH_EVENT_INTERFACE_START:
            xil_printf("[USBH] event INTERFACE_START hub=%u port=%u intf=%u\r\n",
                       (unsigned int)hub_index,
                       (unsigned int)hub_port,
                       (unsigned int)intf);
            if ((hport != NULL) && (intf != USB_INTERFACE_ANY)) {
                xil_printf("[USBH] intf start idx=%u vid=0x%04x pid=0x%04x\r\n",
                           (unsigned int)intf,
                           (unsigned int)hport->device_desc.idVendor,
                           (unsigned int)hport->device_desc.idProduct);
            }
            break;
        case USBH_EVENT_INTERFACE_STOP:
            xil_printf("[USBH] event INTERFACE_STOP hub=%u port=%u intf=%u\r\n",
                       (unsigned int)hub_index,
                       (unsigned int)hub_port,
                       (unsigned int)intf);
            if ((hport != NULL) && (intf != USB_INTERFACE_ANY)) {
                xil_printf("[USBH] intf stop idx=%u vid=0x%04x pid=0x%04x\r\n",
                           (unsigned int)intf,
                           (unsigned int)hport->device_desc.idVendor,
                           (unsigned int)hport->device_desc.idProduct);
            }
            break;
        default:
            break;
    }
}

static void PsAppUsbHost_IrqHandler(void *ref)
{
    PsAppUsbHostImplContext *impl_ctx;
    struct usbh_bus *bus;
    PsAppUsbHostState *state;

    impl_ctx = PsAppUsbHost_ImplGetContext();
    bus = (struct usbh_bus *)ref;
    state = PsAppUsbHost_GetState(impl_ctx);

    if ((state != NULL) && (state->enabled != 0U)) {
        state->irq_count++;
    }

    if (bus != NULL) {
        USBH_IRQHandler(bus->busid);
    }
}

void PsAppUsbHost_LowLevelInit(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus)
{
    PsAppUsbHostState *state;
    u32 mode;
    XStatus status;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((bus == NULL) || (state == NULL)) {
        return;
    }

    mode = Xil_In32((UINTPTR)bus->hcd.reg_base + XUSBPS_MODE_OFFSET);
    mode &= ~XUSBPS_MODE_CM_MASK;
    mode |= XUSBPS_MODE_CM_HOST_MASK;
    Xil_Out32((UINTPTR)bus->hcd.reg_base + XUSBPS_MODE_OFFSET, mode);

    Xil_Out32((UINTPTR)bus->hcd.reg_base + XUSBPS_ISR_OFFSET, 0xFFFFFFFFU);

    if (state->irq_connected == 0U) {
#if (PS_APP_USBHOST_INTR_ID != 0U) && (PS_APP_USBHOST_INTR_PARENT != 0U)
        status = XSetupInterruptSystem((void *)bus,
                                       (void *)PsAppUsbHost_IrqHandler,
                                       PS_APP_USBHOST_INTR_ID,
                                       PS_APP_USBHOST_INTR_PARENT,
                                       XINTERRUPT_DEFAULT_PRIORITY);
        if (status == XST_SUCCESS) {
            state->irq_connected = 1U;
        } else {
            xil_printf("[USBH] interrupt setup failed: %d\r\n", status);
        }
#else
        xil_printf("[USBH] interrupt macros are unavailable in xparameters.h\r\n");
#endif
    }
}

void PsAppUsbHost_LowLevelPostInit(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus)
{
    u8 vid_low;
    u8 vid_high;
    u8 pid_low;
    u8 pid_high;

    if ((impl_ctx == NULL) || (bus == NULL)) {
        return;
    }

    PsAppUsbHost_ApplyPortKick((UINTPTR)bus->hcd.reg_base);
    (void)PsAppUsbHost_SetVbusDriveByBase(impl_ctx, (UINTPTR)bus->hcd.reg_base, 1U);

    if ((PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_LOW, &vid_low) == XST_SUCCESS) &&
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_HIGH, &vid_high) == XST_SUCCESS) &&
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_LOW, &pid_low) == XST_SUCCESS) &&
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)bus->hcd.reg_base, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_HIGH, &pid_high) == XST_SUCCESS)) {
        xil_printf("[USBH] ulpi phy vid=0x%02x%02x pid=0x%02x%02x\r\n",
                   (unsigned int)vid_high,
                   (unsigned int)vid_low,
                   (unsigned int)pid_high,
                   (unsigned int)pid_low);
    } else {
        xil_printf("[USBH] ulpi phy id read failed\r\n");
    }
}

void PsAppUsbHost_LowLevelDeinit(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus)
{
    (void)impl_ctx;
    (void)bus;
}

u8 PsAppUsbHost_GetPortSpeed(PsAppUsbHostImplContext *impl_ctx, struct usbh_bus *bus, u8 port)
{
    u32 portsc;
    u32 speed;

    (void)impl_ctx;

    if ((bus == NULL) || (port == 0U)) {
        return USB_SPEED_UNKNOWN;
    }

    portsc = Xil_In32((UINTPTR)bus->hcd.reg_base + XUSBPS_PORTSCRn_OFFSET(port));
    speed = (portsc & XUSBPS_PORTSCR_PSPD_MASK) >> 26;

    if (speed == 0x02U) {
        return USB_SPEED_HIGH;
    }
    if (speed == 0x01U) {
        return USB_SPEED_LOW;
    }
    if (speed == 0x00U) {
        return USB_SPEED_FULL;
    }
    return USB_SPEED_HIGH;
}
