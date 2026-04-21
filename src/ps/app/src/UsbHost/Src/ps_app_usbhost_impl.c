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
    .stale_detach_ticks = 0U
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
    impl_ctx->recover_cooldown_ticks = 0U;
    impl_ctx->recover_attempts = 0U;
    xil_printf("[USBH] init ok: bus=%u base=0x%08x\r\n",
               (unsigned int)state->bus_id,
               (unsigned int)PS_APP_USBHOST_BASE_ADDR);
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

        if ((logical_present != 0U) || (logical_active != 0U)) {
            impl_ctx->enum_observe_ticks = 0U;
        } else {
            XStatus rst_status;

            if (impl_ctx->enum_observe_ticks < 0xFFFFFFFFU) {
                impl_ctx->enum_observe_ticks++;
            }

            if ((impl_ctx->enum_observe_ticks % scan_retry_ticks) == 0U) {
                PsAppUsbHost_RequestRootHubScan();
            }

            if ((impl_ctx->enum_observe_ticks >= enum_observe_threshold) &&
                (impl_ctx->recover_cooldown_ticks == 0U) &&
                (impl_ctx->recover_attempts < PS_APP_USBHOST_ENUM_MAX_RETRY)) {
                rst_status = PsAppUsbHost_PortResetImpl(impl_ctx->public_context_ptr);
                if (rst_status != XST_SUCCESS) {
                    PsAppUsbHost_RequestRootHubScan();
                }
                impl_ctx->recover_attempts++;
                impl_ctx->recover_cooldown_ticks =
                    PsAppUsbHost_GetRecoverCooldownTicks(impl_ctx->recover_attempts);
                impl_ctx->enum_observe_ticks = 0U;
                xil_printf("[USBH] enumerate recovery retry=%u status=%d cooldown=%u\r\n",
                           (unsigned int)impl_ctx->recover_attempts,
                           (int)rst_status,
                           (unsigned int)impl_ctx->recover_cooldown_ticks);
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
        (PsXinput_IsLikely8BitDo(state->vendor_id, state->product_id) != 0U) &&
        (state->in_report_count == 0U)) {
        impl_ctx->no_report_ticks++;
        if (impl_ctx->no_report_ticks >= PS_APP_USBHOST_8BITDO_REPORT_TIMEOUT_TICKS) {
            if (PsAppUsbHost_Send8BitDoStartupSequence(impl_ctx, impl_ctx->active_xbox_ptr) == XST_SUCCESS) {
                if ((impl_ctx->retry_sideband_log_count < 3U) ||
                    ((impl_ctx->retry_sideband_log_count % 32U) == 0U)) {
                    xil_printf("[USBH] 8BitDo sideband retry after no input\r\n");
                }
                if (impl_ctx->retry_sideband_log_count < 0xFFFFFFFFU) {
                    impl_ctx->retry_sideband_log_count++;
                }
            }
            impl_ctx->no_report_ticks = 0U;
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
