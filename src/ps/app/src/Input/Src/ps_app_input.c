#include "Input/Inc/ps_app_input.h"

#include <string.h>

#include "xil_printf.h"

#include "Common/Inc/ps_project_config.h"

#ifndef PS_APP_INPUT_ENABLE_DEFAULT
#define PS_APP_INPUT_ENABLE_DEFAULT 1U
#endif

#ifndef PS_APP_INPUT_MAP_AB_BY_POSITION
#define PS_APP_INPUT_MAP_AB_BY_POSITION 1U
#endif

#ifndef PS_APP_INPUT_LSTICK_DEADZONE
#define PS_APP_INPUT_LSTICK_DEADZONE 12000
#endif

#ifndef PS_APP_INPUT_TRIGGER_THRESHOLD
#define PS_APP_INPUT_TRIGGER_THRESHOLD 80U
#endif

#define PS_APP_GBA_KEY_A       (1U << 0U)
#define PS_APP_GBA_KEY_B       (1U << 1U)
#define PS_APP_GBA_KEY_SELECT  (1U << 2U)
#define PS_APP_GBA_KEY_START   (1U << 3U)
#define PS_APP_GBA_KEY_RIGHT   (1U << 4U)
#define PS_APP_GBA_KEY_LEFT    (1U << 5U)
#define PS_APP_GBA_KEY_UP      (1U << 6U)
#define PS_APP_GBA_KEY_DOWN    (1U << 7U)
#define PS_APP_GBA_KEY_R       (1U << 8U)
#define PS_APP_GBA_KEY_L       (1U << 9U)

static u32 PsAppInput_MapXinputToGbaKeys(const PsXinputPadState *pad) {
    u32 keys;

    if (pad == NULL) {
        return 0U;
    }

    keys = 0U;

    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_RIGHT) != 0U) {
        keys |= PS_APP_GBA_KEY_RIGHT;
    }
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_LEFT) != 0U) {
        keys |= PS_APP_GBA_KEY_LEFT;
    }
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_UP) != 0U) {
        keys |= PS_APP_GBA_KEY_UP;
    }
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_DOWN) != 0U) {
        keys |= PS_APP_GBA_KEY_DOWN;
    }
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_BACK) != 0U) {
        keys |= PS_APP_GBA_KEY_SELECT;
    }
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_START) != 0U) {
        keys |= PS_APP_GBA_KEY_START;
    }

#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_B) != 0U) {
        keys |= PS_APP_GBA_KEY_A;
    }
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_A) != 0U) {
        keys |= PS_APP_GBA_KEY_B;
    }
#else
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_A) != 0U) {
        keys |= PS_APP_GBA_KEY_A;
    }
    if ((pad->buttons & PS_XINPUT_BUTTON_MASK_B) != 0U) {
        keys |= PS_APP_GBA_KEY_B;
    }
#endif

    if (((pad->buttons & PS_XINPUT_BUTTON_MASK_LB) != 0U) ||
        (pad->lt >= PS_APP_INPUT_TRIGGER_THRESHOLD)) {
        keys |= PS_APP_GBA_KEY_L;
    }
    if (((pad->buttons & PS_XINPUT_BUTTON_MASK_RB) != 0U) ||
        (pad->rt >= PS_APP_INPUT_TRIGGER_THRESHOLD)) {
        keys |= PS_APP_GBA_KEY_R;
    }

    if (pad->lx >= PS_APP_INPUT_LSTICK_DEADZONE) {
        keys |= PS_APP_GBA_KEY_RIGHT;
    } else if (pad->lx <= (-PS_APP_INPUT_LSTICK_DEADZONE)) {
        keys |= PS_APP_GBA_KEY_LEFT;
    }

    if (pad->ly >= PS_APP_INPUT_LSTICK_DEADZONE) {
        keys |= PS_APP_GBA_KEY_UP;
    } else if (pad->ly <= (-PS_APP_INPUT_LSTICK_DEADZONE)) {
        keys |= PS_APP_GBA_KEY_DOWN;
    }

    if ((keys & (PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT)) ==
        (PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT)) {
        keys &= ~(PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT);
    }
    if ((keys & (PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN)) ==
        (PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN)) {
        keys &= ~(PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN);
    }

    return keys & 0x3FFU;
}

static XStatus PsAppInput_UpdateFromReport(PsAppInputContext *ctx, const u8 *report, u32 report_len) {
    PsXinputPadState parsed;
    PsAppInputState *state;
    u32 copy_len;
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL) || (report == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    if (state->enabled == 0U) {
        return XST_FAILURE;
    }

    status = PsXinput_ParseInputReport(report, report_len, &parsed);
    if (status != XST_SUCCESS) {
        state->parse_error_count++;
        return status;
    }

    state->active = 1U;
    state->protocol_is_xinput = 1U;
    state->report_valid = 1U;
    state->buttons = parsed.buttons;
    state->lt = parsed.lt;
    state->rt = parsed.rt;
    state->lx = parsed.lx;
    state->ly = parsed.ly;
    state->rx = parsed.rx;
    state->ry = parsed.ry;
    state->mapped_keys = PsAppInput_MapXinputToGbaKeys(&parsed);
    state->report_count++;

    copy_len = report_len;
    if (copy_len > sizeof(state->last_report)) {
        copy_len = sizeof(state->last_report);
    }
    state->last_report_len = (u8)copy_len;
    if (copy_len > 0U) {
        memcpy(state->last_report, report, copy_len);
    }

    return XST_SUCCESS;
}

#ifdef __GNUC__
__attribute__((weak))
#endif
void PsAppInput_BackendPoll(PsAppInputContext *ctx) {
    (void)ctx;
}

void PsAppInput_Init(PsAppInputContext *ctx) {
    PsAppInputState *state;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    state = ctx->state;
    memset(state, 0, sizeof(*state));
    state->enabled = (u8)((PS_APP_INPUT_ENABLE_DEFAULT != 0U) ? 1U : 0U);
    state->last_committed_keys = 0U;
}

void PsAppInput_Service(PsAppInputContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }
    if (ctx->state->enabled == 0U) {
        return;
    }

    PsAppInput_BackendPoll(ctx);
}

XStatus PsAppInput_OnUsbXInputAttached(PsAppInputContext *ctx,
                                       const PsAppInputUsbDeviceInfo *info) {
    PsAppInputState *state;

    if ((ctx == NULL) || (ctx->state == NULL) || (info == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    if (state->enabled == 0U) {
        return XST_FAILURE;
    }

    if (PsXinput_IsInterfaceCompatible(info->interface_class,
                                       info->interface_subclass,
                                       info->interface_protocol) == 0U) {
        state->unsupported_report_count++;
        return XST_INVALID_PARAM;
    }

    state->active = 1U;
    state->release_pending = 0U;
    state->report_valid = 0U;
    state->protocol_is_xinput = 1U;
    state->output_capable = (info->ep_out_addr != 0U) ? 1U : 0U;
    state->vendor_id = info->vendor_id;
    state->product_id = info->product_id;
    state->interface_number = info->interface_number;
    state->interface_class = info->interface_class;
    state->interface_subclass = info->interface_subclass;
    state->interface_protocol = info->interface_protocol;
    state->ep_in_addr = info->ep_in_addr;
    state->ep_out_addr = info->ep_out_addr;
    state->ep_in_interval_ms = info->ep_in_interval_ms;
    state->ep_out_interval_ms = info->ep_out_interval_ms;
    state->mapped_keys = 0U;
    state->buttons = 0U;
    state->lt = 0U;
    state->rt = 0U;
    state->lx = 0;
    state->ly = 0;
    state->rx = 0;
    state->ry = 0;
    state->last_report_len = 0U;

    xil_printf("[INPUT] xbox attached vid=0x%04x pid=0x%04x intf=%u ep_in=0x%02x ep_out=0x%02x\r\n",
               (unsigned int)state->vendor_id,
               (unsigned int)state->product_id,
               (unsigned int)state->interface_number,
               (unsigned int)state->ep_in_addr,
               (unsigned int)state->ep_out_addr);
    if (PsXinput_IsLikely8BitDo(state->vendor_id, state->product_id) != 0U) {
        xil_printf("[INPUT] detected likely 8BitDo xinput-compatible receiver\r\n");
    }

    return XST_SUCCESS;
}

void PsAppInput_OnUsbDetached(PsAppInputContext *ctx) {
    PsAppInputState *state;
    u8 need_release;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    state = ctx->state;
    if (state->active != 0U) {
        xil_printf("[INPUT] xbox detached\r\n");
    }

    need_release = ((state->active != 0U) ||
                    (state->report_valid != 0U) ||
                    (state->mapped_keys != 0U)) ? 1U : 0U;

    state->active = 0U;
    state->release_pending = need_release;
    state->report_valid = 0U;
    state->protocol_is_xinput = 0U;
    state->output_capable = 0U;
    state->vendor_id = 0U;
    state->product_id = 0U;
    state->interface_number = 0U;
    state->interface_class = 0U;
    state->interface_subclass = 0U;
    state->interface_protocol = 0U;
    state->ep_in_addr = 0U;
    state->ep_out_addr = 0U;
    state->ep_in_interval_ms = 0U;
    state->ep_out_interval_ms = 0U;
    state->buttons = 0U;
    state->lt = 0U;
    state->rt = 0U;
    state->lx = 0;
    state->ly = 0;
    state->rx = 0;
    state->ry = 0;
    state->mapped_keys = 0U;
    state->last_report_len = 0U;
}

XStatus PsAppInput_OnInterruptInReport(PsAppInputContext *ctx,
                                       const u8 *report,
                                       u32 report_len) {
    return PsAppInput_UpdateFromReport(ctx, report, report_len);
}

XStatus PsAppInput_InjectRawReport(PsAppInputContext *ctx, const u8 *report, u32 report_len) {
    PsAppInputUsbDeviceInfo fake_info;

    if ((ctx == NULL) || (ctx->state == NULL) || (report == NULL)) {
        return XST_INVALID_PARAM;
    }

    if (ctx->state->active == 0U) {
        memset(&fake_info, 0, sizeof(fake_info));
        fake_info.vendor_id = 0x2DC8U;
        fake_info.product_id = 0x3106U;
        fake_info.interface_class = PS_XINPUT_INTERFACE_CLASS;
        fake_info.interface_subclass = PS_XINPUT_INTERFACE_SUBCLASS;
        fake_info.interface_protocol = PS_XINPUT_INTERFACE_PROTOCOL;
        fake_info.ep_in_addr = 0x81U;
        fake_info.ep_out_addr = 0x01U;
        fake_info.ep_in_interval_ms = 4U;
        fake_info.ep_out_interval_ms = 8U;
        (void)PsAppInput_OnUsbXInputAttached(ctx, &fake_info);
    }

    return PsAppInput_UpdateFromReport(ctx, report, report_len);
}

u8 PsAppInput_ShouldOverrideKeys(const PsAppInputContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return 0U;
    }
    if (ctx->state->enabled == 0U) {
        return 0U;
    }
    if ((ctx->state->active != 0U) || (ctx->state->release_pending != 0U)) {
        return 1U;
    }
    return 0U;
}

u8 PsAppInput_IsReleasePending(const PsAppInputContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return 0U;
    }
    return ctx->state->release_pending;
}

void PsAppInput_ClearReleasePending(PsAppInputContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }
    ctx->state->release_pending = 0U;
}

u32 PsAppInput_GetMappedKeys(const PsAppInputContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return 0U;
    }
    return (ctx->state->mapped_keys & 0x3FFU);
}

void PsAppInput_BuildRumbleReport(u8 large_motor,
                                  u8 small_motor,
                                  u8 out_report[PS_XINPUT_REPORT_SIZE_OUTPUT]) {
    PsXinput_BuildRumbleOutputReport(large_motor, small_motor, out_report);
}

void PsAppInput_PrintStatus(const PsAppInputContext *ctx) {
    const PsAppInputState *state;
    u32 idx;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        xil_printf("[INPUT] status unavailable\r\n");
        return;
    }

    state = ctx->state;
    xil_printf("[INPUT] en=%u active=%u rel=%u valid=%u proto=%u out=%u\r\n",
               (unsigned int)state->enabled,
               (unsigned int)state->active,
               (unsigned int)state->release_pending,
               (unsigned int)state->report_valid,
               (unsigned int)state->protocol_is_xinput,
               (unsigned int)state->output_capable);
    xil_printf("[INPUT] vid=0x%04x pid=0x%04x intf=%u class=%02x/%02x/%02x ep_in=0x%02x@%ums ep_out=0x%02x@%ums\r\n",
               (unsigned int)state->vendor_id,
               (unsigned int)state->product_id,
               (unsigned int)state->interface_number,
               (unsigned int)state->interface_class,
               (unsigned int)state->interface_subclass,
               (unsigned int)state->interface_protocol,
               (unsigned int)state->ep_in_addr,
               (unsigned int)state->ep_in_interval_ms,
               (unsigned int)state->ep_out_addr,
               (unsigned int)state->ep_out_interval_ms);
    xil_printf("[INPUT] reports=%u parse_err=%u unsupported=%u mapped=0x%03x committed=0x%03x btn=0x%04x lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\r\n",
               (unsigned int)state->report_count,
               (unsigned int)state->parse_error_count,
               (unsigned int)state->unsupported_report_count,
               (unsigned int)(state->mapped_keys & 0x3FFU),
               (unsigned int)(state->last_committed_keys & 0x3FFU),
               (unsigned int)state->buttons,
               (unsigned int)state->lt,
               (unsigned int)state->rt,
               (int)state->lx,
               (int)state->ly,
               (int)state->rx,
               (int)state->ry);
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    xil_printf("[INPUT] map=A<=B(right) B<=A(bottom) deadzone=%d trigger_th=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#else
    xil_printf("[INPUT] map=A<=A(bottom) B<=B(right) deadzone=%d trigger_th=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#endif

    if (state->last_report_len != 0U) {
        xil_printf("[INPUT] last_report(%u)=", (unsigned int)state->last_report_len);
        for (idx = 0U; idx < (u32)state->last_report_len; ++idx) {
            xil_printf("%s%02x",
                       (idx == 0U) ? "" : " ",
                       (unsigned int)state->last_report[idx]);
        }
        xil_printf("\r\n");
    }
}
