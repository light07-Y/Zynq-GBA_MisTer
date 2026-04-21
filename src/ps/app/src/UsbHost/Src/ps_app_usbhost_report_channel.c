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
        if (state->in_report_count == 1U) {
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
        (void)PsAppUsbHost_Send8BitDoStartupSequence(impl_ctx, xbox_class);
    }

    PsAppUsbHost_IntInResubmit(impl_ctx, xbox_class);
}

void PsAppUsbHost_OnXboxStop(PsAppUsbHostImplContext *impl_ctx, struct usbh_xbox *xbox_class)
{
    PsAppUsbHostState *state;

    state = PsAppUsbHost_GetState(impl_ctx);
    if ((state == NULL) || (impl_ctx == NULL)) {
        return;
    }

    state->xbox_interface_active = 0U;
    if (state->device_present == 0U) {
        PsAppUsbHost_ResetDeviceIdentity(state);
    }
    impl_ctx->no_report_ticks = 0U;
    impl_ctx->enum_observe_ticks = 0U;
    impl_ctx->stale_detach_ticks = 0U;

    if (impl_ctx->active_xbox_ptr == xbox_class) {
        impl_ctx->active_xbox_ptr = NULL;
    }

    if ((impl_ctx->public_context_ptr != NULL) && (impl_ctx->public_context_ptr->input != NULL)) {
        PsAppInput_OnUsbDetached(impl_ctx->public_context_ptr->input);
    }
}
