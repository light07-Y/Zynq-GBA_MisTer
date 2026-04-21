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

    (void)intf;

    impl_ctx = PsAppUsbHost_ImplGetContext();
    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (busid != PS_APP_USBHOST_BUS_ID)) {
        return;
    }

    state->event_count++;
    hport = usbh_find_hubport(busid, hub_index, hub_port);

    switch (event) {
        case USBH_EVENT_DEVICE_CONNECTED:
            state->device_present = 1U;
            state->attach_count++;
            impl_ctx->recover_attempts = 0U;
            impl_ctx->recover_cooldown_ticks = 0U;
            impl_ctx->enum_observe_ticks = 0U;
            impl_ctx->stale_detach_ticks = 0U;
            break;
        case USBH_EVENT_DEVICE_DISCONNECTED:
            state->device_present = 0U;
            state->detach_count++;
            PsAppUsbHost_ResetDeviceIdentity(state);
            impl_ctx->recover_attempts = 0U;
            impl_ctx->recover_cooldown_ticks = 0U;
            impl_ctx->enum_observe_ticks = 0U;
            impl_ctx->stale_detach_ticks = 0U;
            impl_ctx->no_report_ticks = 0U;
            break;
        case USBH_EVENT_DEVICE_CONFIGURED:
            if (hport != NULL) {
                state->vendor_id = hport->device_desc.idVendor;
                state->product_id = hport->device_desc.idProduct;
                state->port_speed = hport->speed;
            }
            impl_ctx->recover_attempts = 0U;
            impl_ctx->recover_cooldown_ticks = 0U;
            impl_ctx->enum_observe_ticks = 0U;
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
