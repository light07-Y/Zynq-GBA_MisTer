#include "UsbHost/Inc/ps_app_usbhost.h"
#include "UsbHost/Src/ps_app_usbhost_impl_internal.h"
#include "UsbHost/Src/ps_app_usbhost_internal.h"

#include <stdint.h>

#include "xil_io.h"
#include "xil_printf.h"

#include "Input/Inc/ps_app_input.h"

#include "usb_osal.h"

static PsAppUsbHostImplContext *s_ps_app_usbhost_impl_context_ptr = NULL;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX u8 s_ps_app_usbhost_int_in_report_buffer[PS_APP_USBHOST_INTIN_BUFFER_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX u8 s_ps_app_usbhost_int_out_report_buffer[PS_XINPUT_REPORT_SIZE_OUTPUT];
/* 每次调整恢复状态机后递增该标签，便于现场日志确认是否跑到最新固件。 */
#define PS_APP_USBH_RECOVERY_BUILD_TAG "2026-04-22-r6"
/*
 * 说明：
 * - int_in/out 缓冲放在 non-cache 区，避免 DMA/USB 访问与 CPU cache 不一致。
 * - 下面这些计数字段不是“统计装饰”，而是恢复状态机的关键输入：
 *   no_report_ticks / enum_observe_ticks / stale_detach_ticks / recover_*。
 */
static PsAppUsbHostImplContext s_ps_app_usbhost_impl_context = {
    .public_context_ptr = NULL,
    .active_xbox_ptr = NULL,
    .int_in_report_buffer_ptr = s_ps_app_usbhost_int_in_report_buffer,
    .int_out_report_buffer_ptr = s_ps_app_usbhost_int_out_report_buffer,
    .int_in_report_buffer_size = PS_APP_USBHOST_INTIN_BUFFER_SIZE,
    .int_out_report_buffer_size = PS_XINPUT_REPORT_SIZE_OUTPUT,
    .last_portsc = 0U,
    .force_scan_pending = 0U,
    .recover_cooldown_ticks = 0U,
    .recover_attempts = 0U,
    .ulpi_recovering = 0U,
    .no_report_ticks = 0U,
    .startup_sideband_log_count = 0U,
    .retry_sideband_log_count = 0U,
    .phy_connected = 0U,
    .phy_raw_ccs = 0U,
    .phy_stable_ticks = 0U,
    .enum_observe_ticks = 0U,
    .stale_detach_ticks = 0U,
    .waiting_first_input_after_attach = 0U,
    .detach_defer_active = 0U,
    .detach_defer_ticks = 0U,
    .sideband_attempted_this_attach = 0U
};

PsAppUsbHostImplContext *PsAppUsbHost_ImplGetContext(void)
{
    return s_ps_app_usbhost_impl_context_ptr;
}

void PsAppUsbHost_ImplSetContext(PsAppUsbHostImplContext *impl_ctx)
{
    s_ps_app_usbhost_impl_context_ptr = impl_ctx;
}

static PsAppUsbHostImplContext *PsAppUsbHost_GetOrInitImplContext(void)
{
    if (s_ps_app_usbhost_impl_context_ptr == NULL) {
        PsAppUsbHost_ImplSetContext(&s_ps_app_usbhost_impl_context);
    }
    return s_ps_app_usbhost_impl_context_ptr;
}

static void PsAppUsbHost_BindPublicContext(PsAppUsbHostImplContext *impl_ctx, PsAppUsbHostContext *ctx)
{
    if ((impl_ctx != NULL) && (ctx != NULL)) {
        impl_ctx->public_context_ptr = ctx;
    }
}

void usb_hc_low_level_init(struct usbh_bus *bus)
{
    PsAppUsbHost_LowLevelInit(PsAppUsbHost_GetOrInitImplContext(), bus);
}

void usb_hc_low_level2_init(struct usbh_bus *bus)
{
    PsAppUsbHost_LowLevelPostInit(PsAppUsbHost_GetOrInitImplContext(), bus);
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
    PsAppUsbHost_LowLevelDeinit(PsAppUsbHost_GetOrInitImplContext(), bus);
}

u8 usbh_get_port_speed(struct usbh_bus *bus, const u8 port)
{
    return PsAppUsbHost_GetPortSpeed(PsAppUsbHost_GetOrInitImplContext(), bus, port);
}

void usbh_xbox_run(struct usbh_xbox *xbox_class)
{
    PsAppUsbHost_OnXboxRun(PsAppUsbHost_GetOrInitImplContext(), xbox_class);
}

void usbh_xbox_stop(struct usbh_xbox *xbox_class)
{
    PsAppUsbHost_OnXboxStop(PsAppUsbHost_GetOrInitImplContext(), xbox_class);
}

XStatus PsAppUsbHost_InitImpl(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;
    int ret;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->input == NULL)) {
        return XST_INVALID_PARAM;
    }

    impl_ctx = PsAppUsbHost_GetOrInitImplContext();
    PsAppUsbHost_BindPublicContext(impl_ctx, ctx);
    state = ctx->state;

    if (state->enabled == 0U) {
        xil_printf("[USBH] disabled by configuration\r\n");
        return XST_SUCCESS;
    }
    if (state->initialized != 0U) {
        return XST_SUCCESS;
    }

    state->bus_id = PS_APP_USBHOST_BUS_ID;
    state->irq_count = 0U;
    state->event_count = 0U;
    state->attach_count = 0U;
    state->detach_count = 0U;
    state->in_report_count = 0U;
    state->in_report_error_count = 0U;
    state->out_report_count = 0U;
    state->out_report_error_count = 0U;
    state->device_present = 0U;
    state->xbox_interface_active = 0U;
    PsAppUsbHost_ResetDeviceIdentity(state);

    impl_ctx->active_xbox_ptr = NULL;
    impl_ctx->int_in_report_buffer_ptr = s_ps_app_usbhost_int_in_report_buffer;
    impl_ctx->int_out_report_buffer_ptr = s_ps_app_usbhost_int_out_report_buffer;
    impl_ctx->int_in_report_buffer_size = PS_APP_USBHOST_INTIN_BUFFER_SIZE;
    impl_ctx->int_out_report_buffer_size = PS_XINPUT_REPORT_SIZE_OUTPUT;
    impl_ctx->no_report_ticks = 0U;
    impl_ctx->startup_sideband_log_count = 0U;
    impl_ctx->retry_sideband_log_count = 0U;
    impl_ctx->phy_connected = 0U;
    impl_ctx->phy_raw_ccs = 0U;
    impl_ctx->phy_stable_ticks = 0U;
    impl_ctx->enum_observe_ticks = 0U;
    impl_ctx->stale_detach_ticks = 0U;
    impl_ctx->waiting_first_input_after_attach = 0U;
    impl_ctx->detach_defer_active = 0U;
    impl_ctx->detach_defer_ticks = 0U;
    impl_ctx->sideband_attempted_this_attach = 0U;

    PsAppUsbHost_ForceSlcrUsb0Config();
    PsAppUsbHost_PulsePhyReset();

    ret = usbh_initialize(PS_APP_USBHOST_BUS_ID,
                          (uintptr_t)PS_APP_USBHOST_BASE_ADDR,
                          PsAppUsbHost_OnEvent);
    if (ret < 0) {
        xil_printf("[USBH] usbh_initialize failed: %d\r\n", ret);
        return XST_FAILURE;
    }

    state->initialized = 1U;
    PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
    (void)PsAppUsbHost_SetVbusDriveByBase(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, 1U);
    impl_ctx->last_portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    impl_ctx->phy_raw_ccs = ((impl_ctx->last_portsc & XUSBPS_PORTSCR_CCS_MASK) != 0U) ? 1U : 0U;
    impl_ctx->phy_connected = impl_ctx->phy_raw_ccs;
    impl_ctx->force_scan_pending = 1U;
    /*
     * 关键：即使上电前接收器已插入，也要在初始化后主动触发一次 root-hub 扫描。
     * 否则可能没有新的“连接沿”事件，导致上层迟迟不进入枚举流程。
     */
    impl_ctx->recover_cooldown_ticks = 0U;
    impl_ctx->recover_attempts = 0U;
    xil_printf("[USBH] init ok: bus=%u base=0x%08x\r\n",
               (unsigned int)state->bus_id,
               (unsigned int)PS_APP_USBHOST_BASE_ADDR);
    xil_printf("[USBH] recovery build=%s\r\n", PS_APP_USBH_RECOVERY_BUILD_TAG);
    return XST_SUCCESS;
}

void PsAppUsbHost_ServiceImpl(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;
    u32 portsc;
    u32 changed_bits;
    u8 raw_ccs;
    u32 debounce_ticks;
    u8 logical_present;
    u8 logical_active;
    u32 enum_observe_threshold;
    u32 stale_detach_threshold;
    u32 scan_retry_ticks;

    impl_ctx = PsAppUsbHost_GetOrInitImplContext();
    PsAppUsbHost_BindPublicContext(impl_ctx, ctx);

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (state->enabled == 0U) || (state->initialized == 0U)) {
        return;
    }

    if (impl_ctx->force_scan_pending != 0U) {
        PsAppUsbHost_RequestRootHubScan();
        impl_ctx->force_scan_pending = 0U;
    }

    if (impl_ctx->recover_cooldown_ticks > 0U) {
        impl_ctx->recover_cooldown_ticks--;
    }

    enum_observe_threshold = PS_APP_USBHOST_ENUM_OBSERVE_TICKS;
    if (enum_observe_threshold == 0U) {
        enum_observe_threshold = 1U;
    }
    stale_detach_threshold = PS_APP_USBHOST_STALE_DETACH_TICKS;
    if (stale_detach_threshold == 0U) {
        stale_detach_threshold = 1U;
    }
    scan_retry_ticks = PS_APP_USBHOST_SCAN_RETRY_TICKS;
    if (scan_retry_ticks == 0U) {
        scan_retry_ticks = 1U;
    }

    portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    if ((portsc & XUSBPS_PORTSCR_PHCD_MASK) != 0U) {
        PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
        (void)PsAppUsbHost_SetVbusDriveByBase(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, 1U);
        portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    }
    /* 只关心“连接状态相关”变化位，避免无关位抖动导致过度唤醒。 */
    changed_bits = (portsc ^ impl_ctx->last_portsc) &
                   (XUSBPS_PORTSCR_CSC_MASK | XUSBPS_PORTSCR_CCS_MASK);
    if (changed_bits != 0U) {
        PsAppUsbHost_RequestRootHubScan();
    }

    raw_ccs = ((portsc & XUSBPS_PORTSCR_CCS_MASK) != 0U) ? 1U : 0U;
    if (raw_ccs == impl_ctx->phy_raw_ccs) {
        if (impl_ctx->phy_stable_ticks < 0xFFFFFFFFU) {
            impl_ctx->phy_stable_ticks++;
        }
    } else {
        impl_ctx->phy_raw_ccs = raw_ccs;
        impl_ctx->phy_stable_ticks = 0U;
    }

    /* 连接与断开使用不同去抖窗口，降低误判概率。 */
    debounce_ticks = (raw_ccs != 0U) ?
                     (u32)PS_APP_USBHOST_CONNECT_DEBOUNCE_TICKS :
                     (u32)PS_APP_USBHOST_DISCONNECT_DEBOUNCE_TICKS;
    if (debounce_ticks == 0U) {
        debounce_ticks = 1U;
    }

    if ((impl_ctx->phy_connected != raw_ccs) &&
        (impl_ctx->phy_stable_ticks >= debounce_ticks)) {
        impl_ctx->phy_connected = raw_ccs;
        impl_ctx->enum_observe_ticks = 0U;
        impl_ctx->stale_detach_ticks = 0U;
        impl_ctx->recover_attempts = 0U;
        impl_ctx->recover_cooldown_ticks = 0U;
        PsAppUsbHost_RequestRootHubScan();
        xil_printf("[USBH] physical %s stable (debounce=%u)\r\n",
                   (raw_ccs != 0U) ? "connect" : "disconnect",
                   (unsigned int)debounce_ticks);
    }

    logical_present = (state->device_present != 0U) ? 1U : 0U;
    logical_active = ((state->xbox_interface_active != 0U) ||
                      (impl_ctx->active_xbox_ptr != NULL)) ? 1U : 0U;

    if (impl_ctx->phy_connected != 0U) {
        impl_ctx->stale_detach_ticks = 0U;

        /*
         * 防坑说明：
         * 仅有 device_present=1 并不代表输入链路可用。
         * 现场曾出现“接收器物理在位，但 xbox class 未激活（active=0）”导致后续
         * 不再触发恢复的卡死路径。这里统一要求 logical_active 才视作“枚举成功”。
         */
        if (logical_active != 0U) {
            impl_ctx->enum_observe_ticks = 0U;
        } else {
            XStatus rst_status;
            u8 use_vbus_bounce = 0U;

            if (impl_ctx->enum_observe_ticks < 0xFFFFFFFFU) {
                impl_ctx->enum_observe_ticks++;
            }

            /*
             * “观察阶段”内持续唤醒 root hub 线程，覆盖偶发漏事件和枚举线程竞态。
             * 这样可避免必须手动 replug 才恢复的情况。
             */
            if ((impl_ctx->enum_observe_ticks % scan_retry_ticks) == 0U) {
                PsAppUsbHost_RequestRootHubScan();
            }

            if ((impl_ctx->enum_observe_ticks >= enum_observe_threshold) &&
                (impl_ctx->recover_cooldown_ticks == 0U) &&
                (impl_ctx->recover_attempts < PS_APP_USBHOST_ENUM_MAX_RETRY)) {
                u8 live_present;
                u8 live_active;

                /*
                 * 防竞态：
                 * Service() 与 Hub 线程并行，逻辑状态可能在本周期内已从 inactive -> active。
                 * 恢复动作执行前必须二次采样，避免“刚连上又被恢复动作打断”。
                 */
                live_present = (state->device_present != 0U) ? 1U : 0U;
                live_active = ((state->xbox_interface_active != 0U) ||
                               (impl_ctx->active_xbox_ptr != NULL)) ? 1U : 0U;
                if (live_active != 0U) {
                    impl_ctx->enum_observe_ticks = 0U;
                    impl_ctx->recover_attempts = 0U;
                    impl_ctx->recover_cooldown_ticks = 0U;
                } else {
                /*
                 * 防坑说明：
                 * 这里不要在“present=1, active=0”时提前 ForceLogicalDetach。
                 * 原因是 ForceLogicalDetach 只改本地状态，不会同步改变 Hub 子端口状态；
                 * 若此时又没有新的 C_CONNECTION 事件，后续可能长期卡在 present=0/active=0。
                 */
                logical_present = live_present;
                logical_active = live_active;
                if ((logical_active == 0U) && (impl_ctx->recover_attempts >= 1U)) {
                    /*
                     * 统一策略：
                     * - 第一次恢复先做 port-reset（成本低）。
                     * - 从第二次恢复起统一切 vbus-bounce（强制制造断开/连接沿），
                     *   不再区分 present=0/1，避免“在位但不激活”长期卡死。
                     */
                    use_vbus_bounce = 1U;
                }

                if (use_vbus_bounce != 0U) {
                    /*
                     * 当已经进入“物理在位但逻辑空洞”阶段，优先做一次 VBUS 断电重枚举，
                     * 主动制造断开/连接沿，避免仅靠 PortReset 无法触发 Hub 重建流程。
                     */
                    rst_status = PsAppUsbHost_VbusBounceRecover(impl_ctx);
                } else {
                    /* 端口复位是第一层恢复手段：先于更重的 VBUS bounce。 */
                    rst_status = PsAppUsbHost_PortResetImpl(impl_ctx->public_context_ptr);
                }
                xil_printf("[USBH] recovery decision attempt_prev=%u present=%u active=%u mode=%s\r\n",
                           (unsigned int)impl_ctx->recover_attempts,
                           (unsigned int)logical_present,
                           (unsigned int)logical_active,
                           (use_vbus_bounce != 0U) ? "vbus-bounce" : "port-reset");
                if (rst_status != XST_SUCCESS) {
                    PsAppUsbHost_RequestRootHubScan();
                }
                impl_ctx->recover_attempts++;
                impl_ctx->recover_cooldown_ticks =
                    PsAppUsbHost_GetRecoverCooldownTicks(impl_ctx->recover_attempts);
                impl_ctx->enum_observe_ticks = 0U;
                xil_printf("[USBH] enumerate/bind recovery retry=%u present=%u active=%u status=%d cooldown=%u mode=%s\r\n",
                           (unsigned int)impl_ctx->recover_attempts,
                           (unsigned int)logical_present,
                           (unsigned int)logical_active,
                           (int)rst_status,
                           (unsigned int)impl_ctx->recover_cooldown_ticks,
                           (use_vbus_bounce != 0U) ? "vbus-bounce" : "port-reset");
                }
            }
        }
    } else {
        impl_ctx->enum_observe_ticks = 0U;
        if ((logical_present != 0U) || (logical_active != 0U)) {
            if (impl_ctx->stale_detach_ticks < 0xFFFFFFFFU) {
                impl_ctx->stale_detach_ticks++;
            }
            if (impl_ctx->stale_detach_ticks >= stale_detach_threshold) {
                PsAppUsbHost_ForceLogicalDetach(impl_ctx, "physical disconnect watchdog");
                impl_ctx->stale_detach_ticks = 0U;
                PsAppUsbHost_RequestRootHubScan();
            }
        } else {
            impl_ctx->stale_detach_ticks = 0U;
        }
    }

    if ((state->xbox_interface_active != 0U) &&
        (impl_ctx->active_xbox_ptr != NULL) &&
        (PsXinput_IsLikely8BitDo(state->vendor_id, state->product_id) != 0U)) {
        /*
         * 8BitDo 接收器在“手柄后开机/中途重开机”场景可能需要再次 sideband 激活。
         * 但为避免“刚 attach 就因 sideband 导致设备重枚举”，这里只在
         * “本次 attach 仍未收到首包输入”时才触发重试。
         */
        /*
         * no_report_ticks 由输入回调在“收到任何 IN 报告”时清零。
         * 因此这里统计的是“持续无报告时间”，而不是“是否曾经收过报告”。
         */
        if (impl_ctx->waiting_first_input_after_attach != 0U) {
            u32 report_timeout_ticks = PS_APP_USBHOST_8BITDO_REPORT_TIMEOUT_TICKS;

            if ((state->product_id == 0x310BU) &&
                (impl_ctx->sideband_attempted_this_attach == 0U)) {
                report_timeout_ticks = PS_APP_USBHOST_8BITDO_310B_FIRST_FALLBACK_TICKS;
            }
            impl_ctx->no_report_ticks++;
            if (impl_ctx->no_report_ticks >= report_timeout_ticks) {
                /*
                 * 对 0x310B 做“单次兜底 sideband”：
                 * - 不在 attach 立即发送（避免触发自复位）
                 * - 仅在长期无首包输入时尝试一次
                 * - 不做高频重试，避免再次进入抖动循环
                 */
                if ((impl_ctx->waiting_first_input_after_attach == 0U) ||
                    (state->in_report_count != 0U)) {
                    /*
                     * 防竞态：
                     * IN 回调可能刚在并行上下文收到了首包，这里再次确认后直接放弃 sideband。
                     */
                    impl_ctx->waiting_first_input_after_attach = 0U;
                    impl_ctx->no_report_ticks = 0U;
                } else if ((state->product_id == 0x310BU) &&
                    (impl_ctx->sideband_attempted_this_attach != 0U)) {
                    impl_ctx->waiting_first_input_after_attach = 0U;
                    impl_ctx->no_report_ticks = 0U;
                } else if (PsAppUsbHost_Send8BitDoStartupSequence(impl_ctx, impl_ctx->active_xbox_ptr) == XST_SUCCESS) {
                    impl_ctx->sideband_attempted_this_attach = 1U;
                    if ((impl_ctx->retry_sideband_log_count < 3U) ||
                        ((impl_ctx->retry_sideband_log_count % 32U) == 0U)) {
                        if (state->product_id == 0x310BU) {
                            xil_printf("[USBH] 8BitDo sideband one-shot fallback for pid=0x310B\r\n");
                        } else {
                            xil_printf("[USBH] 8BitDo sideband retry after attach-timeout(no first input)\r\n");
                        }
                    }
                    if (impl_ctx->retry_sideband_log_count < 0xFFFFFFFFU) {
                        impl_ctx->retry_sideband_log_count++;
                    }
                }
                impl_ctx->no_report_ticks = 0U;
            }
        } else {
            impl_ctx->no_report_ticks = 0U;
        }
    } else {
        impl_ctx->no_report_ticks = 0U;
    }

    /*
     * INTERFACE_STOP 发生时不立即认定“输入链路断开”，而是给一个确认窗口。
     * 这能规避“接收器已连上，但 stop/disconnect 事件瞬态抖动导致 UI 反复掉线”的问题。
     */
    if (impl_ctx->detach_defer_active != 0U) {
        if ((state->xbox_interface_active != 0U) || (impl_ctx->active_xbox_ptr != NULL)) {
            impl_ctx->detach_defer_active = 0U;
            impl_ctx->detach_defer_ticks = 0U;
        } else if ((state->device_present == 0U) || (impl_ctx->phy_connected == 0U)) {
            if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
                PsAppInput_OnUsbDetached(impl_ctx->public_context_ptr->input);
            }
            impl_ctx->detach_defer_active = 0U;
            impl_ctx->detach_defer_ticks = 0U;
        } else {
            if (impl_ctx->detach_defer_ticks > 0U) {
                impl_ctx->detach_defer_ticks--;
            } else {
                if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
                    PsAppInput_OnUsbDetached(impl_ctx->public_context_ptr->input);
                }
                impl_ctx->detach_defer_active = 0U;
                impl_ctx->detach_defer_ticks = 0U;
            }
        }
    }

    impl_ctx->last_portsc = portsc;
}

void PsAppUsbHost_PrintStatusImpl(const PsAppUsbHostContext *ctx)
{
    const PsAppUsbHostImplContext *impl_ctx;
    const PsAppUsbHostContext *effective_ctx;
    const PsAppUsbHostState *state;
    u32 cmd;
    u32 mode;
    u32 isr;
    u32 ier;
    u32 portsc;
    u32 hcsparams;
    u32 otgcsr;
    u32 ulpiview;
    u32 line_status;
    u32 port_speed;

    impl_ctx = PsAppUsbHost_ImplGetContext();
    effective_ctx = ctx;
    if ((effective_ctx == NULL) && (impl_ctx != NULL)) {
        effective_ctx = impl_ctx->public_context_ptr;
    }

    if ((effective_ctx == NULL) || (effective_ctx->state == NULL)) {
        xil_printf("[USBH] status unavailable\r\n");
        return;
    }

    state = effective_ctx->state;
    xil_printf("[USBH] en=%u init=%u irq=%u dev=%u xbox=%u bus=%u speed=%u\r\n",
               (unsigned int)state->enabled,
               (unsigned int)state->initialized,
               (unsigned int)state->irq_connected,
               (unsigned int)state->device_present,
               (unsigned int)state->xbox_interface_active,
               (unsigned int)state->bus_id,
               (unsigned int)state->port_speed);
    xil_printf("[USBH] vid=0x%04x pid=0x%04x intf=%u cls=%02x/%02x/%02x ep_in=0x%02x ep_out=0x%02x\r\n",
               (unsigned int)state->vendor_id,
               (unsigned int)state->product_id,
               (unsigned int)state->interface_number,
               (unsigned int)state->interface_class,
               (unsigned int)state->interface_subclass,
               (unsigned int)state->interface_protocol,
               (unsigned int)state->ep_in_addr,
               (unsigned int)state->ep_out_addr);
    xil_printf("[USBH] irq_cnt=%u events=%u attach=%u detach=%u in_ok=%u in_err=%u out_ok=%u out_err=%u\r\n",
               (unsigned int)state->irq_count,
               (unsigned int)state->event_count,
               (unsigned int)state->attach_count,
               (unsigned int)state->detach_count,
               (unsigned int)state->in_report_count,
               (unsigned int)state->in_report_error_count,
               (unsigned int)state->out_report_count,
               (unsigned int)state->out_report_error_count);
    xil_printf("[USBH] phy debounced=%u raw=%u stable=%u enum_wait=%u detach_wait=%u retry=%u cooldown=%u\r\n",
               (unsigned int)((impl_ctx != NULL) ? impl_ctx->phy_connected : 0U),
               (unsigned int)((impl_ctx != NULL) ? impl_ctx->phy_raw_ccs : 0U),
               (unsigned int)((impl_ctx != NULL) ? impl_ctx->phy_stable_ticks : 0U),
               (unsigned int)((impl_ctx != NULL) ? impl_ctx->enum_observe_ticks : 0U),
               (unsigned int)((impl_ctx != NULL) ? impl_ctx->stale_detach_ticks : 0U),
               (unsigned int)((impl_ctx != NULL) ? impl_ctx->recover_attempts : 0U),
               (unsigned int)((impl_ctx != NULL) ? impl_ctx->recover_cooldown_ticks : 0U));

    if (state->initialized != 0U) {
        PsAppUsbHost_PrintSlcrSummary();

        cmd = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_CMD_OFFSET);
        mode = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_MODE_OFFSET);
        isr = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_ISR_OFFSET);
        ier = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_IER_OFFSET);
        portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
        hcsparams = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_HCSPARAMS_OFFSET);
        otgcsr = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_OTGCSR_OFFSET);
        ulpiview = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_ULPIVIEW_OFFSET);
        line_status = (portsc & XUSBPS_PORTSCR_LS_MASK) >> 10;
        port_speed = (portsc & XUSBPS_PORTSCR_PSPD_MASK) >> 26;

        xil_printf("[USBH] reg cmd=0x%08x mode=0x%08x isr=0x%08x ier=0x%08x hcs=0x%08x\r\n",
                   (unsigned int)cmd,
                   (unsigned int)mode,
                   (unsigned int)isr,
                   (unsigned int)ier,
                   (unsigned int)hcsparams);
        xil_printf("[USBH] reg portsc=0x%08x otg=0x%08x ulpi=0x%08x\r\n",
                   (unsigned int)portsc,
                   (unsigned int)otgcsr,
                   (unsigned int)ulpiview);
        xil_printf("[USBH] port1 ccs=%u csc=%u pe=%u pec=%u oca=%u occ=%u susp=%u pr=%u pp=%u owner=%u phcd=%u ls=%u pspd=%u\r\n",
                   (unsigned int)((portsc & XUSBPS_PORTSCR_CCS_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_CSC_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PE_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PEC_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_OCA_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_OCC_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_SUSP_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PR_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PP_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PO_MASK) != 0U),
                   (unsigned int)((portsc & XUSBPS_PORTSCR_PHCD_MASK) != 0U),
                   (unsigned int)line_status,
                   (unsigned int)port_speed);
        xil_printf("[USBH] intr ui=%u ue=%u pc=%u aa=%u hch=%u ps=%u as=%u ulpi=%u\r\n",
                   (unsigned int)((isr & XUSBPS_IXR_UI_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_UE_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_PC_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_AA_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_HCH_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_PS_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_AS_MASK) != 0U),
                   (unsigned int)((isr & XUSBPS_IXR_ULPI_MASK) != 0U));
    }
}

void PsAppUsbHost_ForceRootHubScanImpl(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;

    impl_ctx = PsAppUsbHost_GetOrInitImplContext();
    PsAppUsbHost_BindPublicContext(impl_ctx, ctx);

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (state->enabled == 0U) || (state->initialized == 0U)) {
        return;
    }

    impl_ctx->force_scan_pending = 1U;
    PsAppUsbHost_RequestRootHubScan();
}

XStatus PsAppUsbHost_SetVbusDriveImpl(PsAppUsbHostContext *ctx, u8 enable)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    impl_ctx = PsAppUsbHost_GetOrInitImplContext();
    PsAppUsbHost_BindPublicContext(impl_ctx, ctx);
    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U)) {
        return XST_FAILURE;
    }

    if (enable != 0U) {
        PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
    }

    if (PsAppUsbHost_SetVbusDriveByBase(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, enable) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    impl_ctx->last_portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    PsAppUsbHost_ForceRootHubScanImpl(ctx);
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_DumpUlpiImpl(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;
    u8 vid_low;
    u8 vid_high;
    u8 pid_low;
    u8 pid_high;
    u8 otg_ctrl;
    u8 func_ctrl;
    u8 iface_ctrl;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    impl_ctx = PsAppUsbHost_GetOrInitImplContext();
    PsAppUsbHost_BindPublicContext(impl_ctx, ctx);
    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U)) {
        return XST_FAILURE;
    }

    if ((PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_LOW, &vid_low) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_VENDOR_ID_HIGH, &vid_high) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_LOW, &pid_low) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_PRODUCT_ID_HIGH, &pid_high) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_OTG_CTRL, &otg_ctrl) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_FUNC_CTRL, &func_ctrl) != XST_SUCCESS) ||
        (PsAppUsbHost_UlpiRead(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, PS_APP_USBHOST_ULPI_REG_IFACE_CTRL, &iface_ctrl) != XST_SUCCESS)) {
        xil_printf("[USBH] ulpi dump failed (viewport timeout)\r\n");
        return XST_FAILURE;
    }

    xil_printf("[USBH] ulpi phy vid=0x%02x%02x pid=0x%02x%02x otg=0x%02x func=0x%02x iface=0x%02x\r\n",
               (unsigned int)vid_high,
               (unsigned int)vid_low,
               (unsigned int)pid_high,
               (unsigned int)pid_low,
               (unsigned int)otg_ctrl,
               (unsigned int)func_ctrl,
               (unsigned int)iface_ctrl);
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_PortResetImpl(PsAppUsbHostContext *ctx)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;
    u32 portsc;
    u32 timeout_ms;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    impl_ctx = PsAppUsbHost_GetOrInitImplContext();
    PsAppUsbHost_BindPublicContext(impl_ctx, ctx);
    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U)) {
        return XST_FAILURE;
    }

    PsAppUsbHost_ApplyPortKick((UINTPTR)PS_APP_USBHOST_BASE_ADDR);
    (void)PsAppUsbHost_SetVbusDriveByBase(impl_ctx, (UINTPTR)PS_APP_USBHOST_BASE_ADDR, 1U);

    portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    portsc &= ~XUSBPS_PORTSCR_PE_MASK;
    portsc |= (XUSBPS_PORTSCR_PR_MASK | XUSBPS_PORTSCR_PP_MASK | PS_APP_USBHOST_PORTSC_CHANGE_BITS);
    Xil_Out32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET, portsc);

    usb_osal_msleep(55U);

    portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    portsc &= ~XUSBPS_PORTSCR_PR_MASK;
    portsc |= (XUSBPS_PORTSCR_PP_MASK | PS_APP_USBHOST_PORTSC_CHANGE_BITS);
    Xil_Out32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET, portsc);

    timeout_ms = 0U;
    while (timeout_ms < PS_APP_USBHOST_PORT_RESET_TIMEOUT_MS) {
        portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
        if ((portsc & XUSBPS_PORTSCR_PR_MASK) == 0U) {
            break;
        }
        usb_osal_msleep(1U);
        timeout_ms++;
    }

    impl_ctx->last_portsc = Xil_In32((UINTPTR)PS_APP_USBHOST_BASE_ADDR + XUSBPS_PORTSCR1_OFFSET);
    PsAppUsbHost_ForceRootHubScanImpl(ctx);

    if (timeout_ms >= PS_APP_USBHOST_PORT_RESET_TIMEOUT_MS) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_SetRumbleImpl(PsAppUsbHostContext *ctx, u8 large_motor, u8 small_motor)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->input == NULL)) {
        return XST_INVALID_PARAM;
    }

    impl_ctx = PsAppUsbHost_GetOrInitImplContext();
    PsAppUsbHost_BindPublicContext(impl_ctx, ctx);
    state = ctx->state;
    if ((state->enabled == 0U) || (state->initialized == 0U) ||
        (state->xbox_interface_active == 0U) ||
        (impl_ctx->active_xbox_ptr == NULL) ||
        (impl_ctx->active_xbox_ptr->hport == NULL) ||
        (impl_ctx->active_xbox_ptr->intout == NULL) ||
        (impl_ctx->int_out_report_buffer_ptr == NULL)) {
        return XST_FAILURE;
    }

    PsAppInput_BuildRumbleReport(large_motor, small_motor, impl_ctx->int_out_report_buffer_ptr);
    status = PsAppUsbHost_SubmitInterruptOut(impl_ctx,
                                             impl_ctx->active_xbox_ptr,
                                             impl_ctx->int_out_report_buffer_ptr,
                                             impl_ctx->int_out_report_buffer_size,
                                             100U);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}
