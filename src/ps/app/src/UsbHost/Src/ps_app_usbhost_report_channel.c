#include "UsbHost/Src/ps_app_usbhost_impl_internal.h"

#include <string.h>

#include "Input/Inc/ps_app_input.h"

#include "xil_printf.h"

#include "usb_errno.h"

static void PsAppUsbHost_IntInResubmit(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class);

XStatus PsAppUsbHost_SubmitInterruptOut(PsAppUsbHostImplContext *impl_ctx,
                                        struct usbh_xbox *xbox_class,
                                        const u8 *payload,
                                        u32 payload_len,
                                        u32 timeout_ms)
{
    PsAppUsbHostState *state;
    int ret;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (impl_ctx == NULL) || (xbox_class == NULL) || (payload == NULL) ||
        (payload_len == 0U) || (payload_len > impl_ctx->int_out_report_buffer_size) ||
        (xbox_class->hport == NULL) || (xbox_class->intout == NULL) ||
        (impl_ctx->int_out_report_buffer_ptr == NULL)) {
        return XST_INVALID_PARAM;
    }

    memcpy(impl_ctx->int_out_report_buffer_ptr, payload, payload_len);
    usbh_int_urb_fill(&xbox_class->intout_urb,
                      xbox_class->hport,
                      xbox_class->intout,
                      impl_ctx->int_out_report_buffer_ptr,
                      payload_len,
                      timeout_ms,
                      NULL,
                      NULL);

    ret = usbh_submit_urb(&xbox_class->intout_urb);
    if (ret < 0) {
        state->out_report_error_count++;
        return XST_FAILURE;
    }

    state->out_report_count++;
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_Send8BitDoStartupSequence(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class)
{
    static const u8 s_ps_app_usbhost_8bitdo_sideband_packet_0[3] = { 0x01U, 0x03U, 0x02U };
    static const u8 s_ps_app_usbhost_8bitdo_sideband_packet_1[3] = { 0x02U, 0x08U, 0x03U };
    XStatus status;

    status = PsAppUsbHost_SubmitInterruptOut(impl_ctx,
                                             xbox_class,
                                             s_ps_app_usbhost_8bitdo_sideband_packet_0,
                                             sizeof(s_ps_app_usbhost_8bitdo_sideband_packet_0),
                                             100U);
    if (status != XST_SUCCESS) {
        return status;
    }
    status = PsAppUsbHost_SubmitInterruptOut(impl_ctx,
                                             xbox_class,
                                             s_ps_app_usbhost_8bitdo_sideband_packet_1,
                                             sizeof(s_ps_app_usbhost_8bitdo_sideband_packet_1),
                                             100U);
    if (status != XST_SUCCESS) {
        return status;
    }
    status = PsAppUsbHost_SubmitInterruptOut(impl_ctx,
                                             xbox_class,
                                             s_ps_app_usbhost_8bitdo_sideband_packet_0,
                                             sizeof(s_ps_app_usbhost_8bitdo_sideband_packet_0),
                                             100U);
    if (status != XST_SUCCESS) {
        return status;
    }

    if ((impl_ctx != NULL) &&
        ((impl_ctx->startup_sideband_log_count < 3U) ||
         ((impl_ctx->startup_sideband_log_count % 64U) == 0U))) {
        xil_printf("[USBH] 8BitDo startup sideband sent\r\n");
    }
    if ((impl_ctx != NULL) && (impl_ctx->startup_sideband_log_count < 0xFFFFFFFFU)) {
        impl_ctx->startup_sideband_log_count++;
    }
    return XST_SUCCESS;
}

static void PsAppUsbHost_IntInComplete(void *arg, int nbytes)
{
    PsAppUsbHostImplContext *impl_ctx;
    PsAppUsbHostState *state;
    struct usbh_xbox *xbox_class;

    impl_ctx = PsAppUsbHost_ImplGetContext();
    state = PsAppUsbHost_GetState(impl_ctx);
    xbox_class = (struct usbh_xbox *)arg;

    if ((xbox_class == NULL) || (state == NULL) || (impl_ctx == NULL)) {
        return;
    }

    if (nbytes > 0) {
        state->in_report_count++;
        impl_ctx->no_report_ticks = 0U;
        impl_ctx->waiting_first_input_after_attach = 0U;
        if (state->in_report_count == 1U) {
            /*
             * “first input report”是输入链路真正打通的关键里程碑。
             * 注意它可能晚于 attach 日志：
             * - 接收器可能先完成 USB 枚举，再等待手柄无线链路建立；
             * - 8BitDo 可能要等到 one-shot sideband/超时兜底后才开始稳定出包。
             */
            xil_printf("[USBH] first input report len=%d hdr=%02x %02x\r\n",
                       nbytes,
                       (unsigned int)impl_ctx->int_in_report_buffer_ptr[0],
                       (unsigned int)impl_ctx->int_in_report_buffer_ptr[1]);
        }
        if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
            if (PsAppInput_OnInterruptInReport(impl_ctx->public_context_ptr->input,
                                               impl_ctx->int_in_report_buffer_ptr,
                                               (u32)nbytes) != XST_SUCCESS) {
                state->in_report_error_count++;
            }
        }
    } else if (nbytes < 0) {
        if (nbytes != -USB_ERR_SHUTDOWN) {
            state->in_report_error_count++;
        } else {
            return;
        }
    }

    /*
     * 保护性退出：
     * stop/disconnect 可能与 IN 回调并发到达；若端口已不在 connected 状态，
     * 不再重提 URB，避免对已失效链路持续提交导致噪声日志或异常。
     */
    if ((xbox_class->hport == NULL) || (xbox_class->hport->connected == false)) {
        return;
    }
    if (impl_ctx->active_xbox_ptr != xbox_class) {
        return;
    }

    PsAppUsbHost_IntInResubmit(impl_ctx, xbox_class);
}

static void PsAppUsbHost_IntInResubmit(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class)
{
    PsAppUsbHostState *state;
    int ret;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (impl_ctx == NULL) || (xbox_class == NULL) ||
        (xbox_class->hport == NULL) || (xbox_class->intin == NULL) ||
        (impl_ctx->int_in_report_buffer_ptr == NULL)) {
        return;
    }

    usbh_int_urb_fill(&xbox_class->intin_urb,
                      xbox_class->hport,
                      xbox_class->intin,
                      impl_ctx->int_in_report_buffer_ptr,
                      impl_ctx->int_in_report_buffer_size,
                      0U,
                      PsAppUsbHost_IntInComplete,
                      xbox_class);

    ret = usbh_submit_urb(&xbox_class->intin_urb);
    if (ret < 0) {
        state->in_report_error_count++;
        xil_printf("[USBH] intin submit failed: %d\r\n", ret);
    }
}

void PsAppUsbHost_OnXboxRun(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class)
{
    PsAppInputUsbDeviceInfo info;
    PsAppUsbHostState *state;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (impl_ctx == NULL) || (xbox_class == NULL) || (xbox_class->hport == NULL)) {
        return;
    }

    memset(&info, 0, sizeof(info));
    impl_ctx->active_xbox_ptr = xbox_class;
    impl_ctx->no_report_ticks = 0U;
    impl_ctx->waiting_first_input_after_attach = 1U;
    impl_ctx->detach_defer_active = 0U;
    impl_ctx->detach_defer_ticks = 0U;
    impl_ctx->sideband_attempted_this_attach = 0U;
    xil_printf("[USBH] xbox run minor=%u ptr=0x%08x hport_conn=%u\r\n",
               (unsigned int)xbox_class->minor,
               (unsigned int)(UINTPTR)xbox_class,
               (unsigned int)((xbox_class->hport != NULL) ? (xbox_class->hport->connected ? 1U : 0U) : 0U));

    PsAppUsbHost_UpdateStateFromXbox(impl_ctx, xbox_class);

    info.vendor_id = xbox_class->hport->device_desc.idVendor;
    info.product_id = xbox_class->hport->device_desc.idProduct;
    info.interface_number = xbox_class->intf;
    info.interface_class = state->interface_class;
    info.interface_subclass = state->interface_subclass;
    info.interface_protocol = state->interface_protocol;
    info.ep_in_addr = state->ep_in_addr;
    info.ep_out_addr = state->ep_out_addr;
    info.ep_in_interval_ms = (xbox_class->intin != NULL) ? xbox_class->intin->bInterval : 0U;
    info.ep_out_interval_ms = (xbox_class->intout != NULL) ? xbox_class->intout->bInterval : 0U;

    if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
        (void)PsAppInput_OnUsbXInputAttached(impl_ctx->public_context_ptr->input, &info);
    }

    if (PsXinput_IsLikely8BitDo(info.vendor_id, info.product_id) != 0U) {
        if (info.product_id == 0x310BU) {
            /*
             * 现场日志表明 0x310B 已是原生 XInput 接口（FF/5D/01），
             * 再发 sideband 可能导致设备自复位并触发立即 detach。
             * 这里不立即发送，改为“仅在长时间无首包输入时做一次兜底 sideband”。
             */
            xil_printf("[USBH] 8BitDo sideband deferred(one-shot fallback) for pid=0x310B\r\n");
        } else {
        /*
         * 防抖策略：
         * 8BitDo 在部分固件上“刚 attach 立即 sideband”会触发重枚举/短断连。
         * 因此改为延迟触发：仅当 attach 后长时间收不到首包输入，再在 Service() 中发送。
         */
            xil_printf("[USBH] 8BitDo sideband deferred until no-first-input timeout\r\n");
        }
    }

    PsAppUsbHost_IntInResubmit(impl_ctx, xbox_class);
}

void PsAppUsbHost_OnXboxStop(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class)
{
    PsAppUsbHostState *state;
    u8 is_active_stop;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (impl_ctx == NULL)) {
        return;
    }

    is_active_stop = ((impl_ctx->active_xbox_ptr == xbox_class) ||
                      (impl_ctx->active_xbox_ptr == NULL)) ? 1U : 0U;
    xil_printf("[USBH] xbox stop minor=%u ptr=0x%08x active_ptr=0x%08x match=%u hport_conn=%u dev_present=%u\r\n",
               (unsigned int)((xbox_class != NULL) ? xbox_class->minor : 0U),
               (unsigned int)(UINTPTR)xbox_class,
               (unsigned int)(UINTPTR)impl_ctx->active_xbox_ptr,
               (unsigned int)is_active_stop,
               (unsigned int)((xbox_class != NULL) && (xbox_class->hport != NULL) ? (xbox_class->hport->connected ? 1U : 0U) : 0U),
               (unsigned int)state->device_present);

    if (is_active_stop == 0U) {
        /*
         * 防坑：
         * 多次重枚举时，旧实例的 stop 事件可能晚到。
         * 旧 stop 不应打断当前 active 实例，否则会出现“刚 attached 就 detached”的假掉线。
         */
        return;
    }

    state->xbox_interface_active = 0U;
    if (state->device_present == 0U) {
        PsAppUsbHost_ResetDeviceIdentity(state);
    }
    impl_ctx->no_report_ticks = 0U;
    impl_ctx->waiting_first_input_after_attach = 0U;
    impl_ctx->enum_observe_ticks = 0U;
    impl_ctx->stale_detach_ticks = 0U;
    impl_ctx->sideband_attempted_this_attach = 0U;

    if (impl_ctx->active_xbox_ptr == xbox_class) {
        impl_ctx->active_xbox_ptr = NULL;
    }

    if ((state->device_present != 0U) && (impl_ctx->phy_connected != 0U)) {
        /*
         * 关键防抖：
         * stop 事件可能先于“真实断开事实”到达，不能立刻把输入层标成 detached。
         * 进入延迟确认窗口，交由 Service() + DEVICE_DISCONNECTED 共同裁决。
         */
        impl_ctx->detach_defer_active = 1U;
        impl_ctx->detach_defer_ticks = PS_APP_USBHOST_DETACH_DEFER_TICKS;
    } else if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
        PsAppInput_OnUsbDetached(impl_ctx->public_context_ptr->input);
    }
}
