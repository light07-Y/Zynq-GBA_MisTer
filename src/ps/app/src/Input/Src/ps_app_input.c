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

#ifndef PS_APP_INPUT_STABLE_SAMPLE_COUNT
#define PS_APP_INPUT_STABLE_SAMPLE_COUNT 2U
#endif

#ifndef PS_APP_INPUT_SHORT_PULSE_TICKS
#define PS_APP_INPUT_SHORT_PULSE_TICKS 8U
#endif

#ifndef PS_APP_INPUT_PRESS_DEBOUNCE_TICKS
#define PS_APP_INPUT_PRESS_DEBOUNCE_TICKS 2U
#endif

#ifndef PS_APP_INPUT_RELEASE_DEBOUNCE_TICKS
#define PS_APP_INPUT_RELEASE_DEBOUNCE_TICKS 6U
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
#define PS_APP_GBA_KEY_MASK_ALL 0x3FFU
#define PS_APP_GBA_KEY_COUNT    10U
#define PS_APP_GBA_KEY_MASK_DPAD \
    (PS_APP_GBA_KEY_RIGHT | PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN)
#define PS_APP_GBA_KEY_MASK_ACTION (PS_APP_GBA_KEY_MASK_ALL & ~PS_APP_GBA_KEY_MASK_DPAD)
#define PS_APP_INPUT_DPAD_SOURCE_MASK \
    (PS_XINPUT_BUTTON_MASK_UP | PS_XINPUT_BUTTON_MASK_DOWN | \
     PS_XINPUT_BUTTON_MASK_LEFT | PS_XINPUT_BUTTON_MASK_RIGHT)
#define PS_APP_INPUT_NONSTICK_SOURCE_MASK \
    (PS_XINPUT_BUTTON_MASK_UP | PS_XINPUT_BUTTON_MASK_DOWN | \
     PS_XINPUT_BUTTON_MASK_LEFT | PS_XINPUT_BUTTON_MASK_RIGHT | \
     PS_XINPUT_BUTTON_MASK_START | PS_XINPUT_BUTTON_MASK_BACK | \
     PS_XINPUT_BUTTON_MASK_LB | PS_XINPUT_BUTTON_MASK_RB | \
     PS_XINPUT_BUTTON_MASK_A | PS_XINPUT_BUTTON_MASK_B | \
     PS_APP_INPUT_SRC_MASK_LT | PS_APP_INPUT_SRC_MASK_RT)

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

static u32 PsAppInput_BuildSourceMaskFromPad(const PsXinputPadState *pad) {
    u32 source_mask;

    if (pad == NULL) {
        return 0U;
    }

    source_mask = (u32)pad->buttons;
    if (pad->lt >= PS_APP_INPUT_TRIGGER_THRESHOLD) {
        source_mask |= PS_APP_INPUT_SRC_MASK_LT;
    }
    if (pad->rt >= PS_APP_INPUT_TRIGGER_THRESHOLD) {
        source_mask |= PS_APP_INPUT_SRC_MASK_RT;
    }
    if (pad->lx >= PS_APP_INPUT_LSTICK_DEADZONE) {
        source_mask |= PS_APP_INPUT_SRC_MASK_LS_RIGHT;
    } else if (pad->lx <= (-PS_APP_INPUT_LSTICK_DEADZONE)) {
        source_mask |= PS_APP_INPUT_SRC_MASK_LS_LEFT;
    }
    if (pad->ly >= PS_APP_INPUT_LSTICK_DEADZONE) {
        source_mask |= PS_APP_INPUT_SRC_MASK_LS_UP;
    } else if (pad->ly <= (-PS_APP_INPUT_LSTICK_DEADZONE)) {
        source_mask |= PS_APP_INPUT_SRC_MASK_LS_DOWN;
    }

    return source_mask;
}

static u8 PsAppInput_IsExpectedMainReport(const PsAppInputState *state,
                                          const u8 *report,
                                          u32 report_len) {
    if ((state == NULL) || (report == NULL)) {
        return 0U;
    }

    /*
     * 8BitDo 主输入流常见两种形态：
     * 1) 20B: 00 14 ...
     * 2) 19B: 14 ...
     * 解析器两种都支持，这里的“主输入包筛选”必须同步放宽，否则会出现
     * “URB 已收包但输入层全部丢弃”的假死现象。
     *
     * 另一个容易踩坑点：
     * 串口里 first input report 可能较晚出现，不一定是“手柄没连上”，
     * 也可能只是接收器先完成枚举、后续才切到可用报告格式。
     */
    if (PsXinput_IsLikely8BitDo(state->vendor_id, state->product_id) != 0U) {
        if (!(((report_len >= 20U) &&
               (report[0] == PS_XINPUT_REPORT_ID_DEFAULT) &&
               (report[1] == PS_XINPUT_REPORT_SIZE_INPUT)) ||
              ((report_len >= 19U) &&
               (report[0] == PS_XINPUT_REPORT_SIZE_INPUT)))) {
            return 0U;
        }
    }

    return 1U;
}

static void PsAppInput_ResetKeyConditioner(PsAppInputState *state) {
    if (state == NULL) {
        return;
    }

    state->raw_mapped_keys = 0U;
    state->source_snapshot_mask = 0U;
    state->last_logged_source_mask = 0U;
    state->mapped_keys = 0U;
    state->sampled_keys = 0U;
    state->frame_press_keys = 0U;
    state->pending_mapped_keys = 0U;
    state->debounced_nonstick_keys = 0U;
    state->frame_sync_ready = 0U;
    state->frame_sync_token = 0U;
    memset(state->debounce_count, 0, sizeof(state->debounce_count));
    memset(state->short_pulse_ticks, 0, sizeof(state->short_pulse_ticks));
}

static u32 PsAppInput_StickMappedKeysFromSource(u32 source_mask) {
    u32 stick_keys = 0U;

    if ((source_mask & PS_APP_INPUT_SRC_MASK_LS_RIGHT) != 0U) {
        stick_keys |= PS_APP_GBA_KEY_RIGHT;
    }
    if ((source_mask & PS_APP_INPUT_SRC_MASK_LS_LEFT) != 0U) {
        stick_keys |= PS_APP_GBA_KEY_LEFT;
    }
    if ((source_mask & PS_APP_INPUT_SRC_MASK_LS_UP) != 0U) {
        stick_keys |= PS_APP_GBA_KEY_UP;
    }
    if ((source_mask & PS_APP_INPUT_SRC_MASK_LS_DOWN) != 0U) {
        stick_keys |= PS_APP_GBA_KEY_DOWN;
    }

    return stick_keys & PS_APP_GBA_KEY_MASK_ALL;
}

static u32 PsAppInput_MapNonStickSourceToGbaKeys(u32 source_mask) {
    u32 keys = 0U;

    if ((source_mask & PS_XINPUT_BUTTON_MASK_RIGHT) != 0U) {
        keys |= PS_APP_GBA_KEY_RIGHT;
    }
    if ((source_mask & PS_XINPUT_BUTTON_MASK_LEFT) != 0U) {
        keys |= PS_APP_GBA_KEY_LEFT;
    }
    if ((source_mask & PS_XINPUT_BUTTON_MASK_UP) != 0U) {
        keys |= PS_APP_GBA_KEY_UP;
    }
    if ((source_mask & PS_XINPUT_BUTTON_MASK_DOWN) != 0U) {
        keys |= PS_APP_GBA_KEY_DOWN;
    }
    if ((source_mask & PS_XINPUT_BUTTON_MASK_BACK) != 0U) {
        keys |= PS_APP_GBA_KEY_SELECT;
    }
    if ((source_mask & PS_XINPUT_BUTTON_MASK_START) != 0U) {
        keys |= PS_APP_GBA_KEY_START;
    }
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    if ((source_mask & PS_XINPUT_BUTTON_MASK_B) != 0U) {
        keys |= PS_APP_GBA_KEY_A;
    }
    if ((source_mask & PS_XINPUT_BUTTON_MASK_A) != 0U) {
        keys |= PS_APP_GBA_KEY_B;
    }
#else
    if ((source_mask & PS_XINPUT_BUTTON_MASK_A) != 0U) {
        keys |= PS_APP_GBA_KEY_A;
    }
    if ((source_mask & PS_XINPUT_BUTTON_MASK_B) != 0U) {
        keys |= PS_APP_GBA_KEY_B;
    }
#endif
    if ((source_mask & (PS_XINPUT_BUTTON_MASK_LB | PS_APP_INPUT_SRC_MASK_LT)) != 0U) {
        keys |= PS_APP_GBA_KEY_L;
    }
    if ((source_mask & (PS_XINPUT_BUTTON_MASK_RB | PS_APP_INPUT_SRC_MASK_RT)) != 0U) {
        keys |= PS_APP_GBA_KEY_R;
    }

    /* 行业常见防御：对互斥方向做消抖前归一化，避免过渡态把“左右/上下同时按下”
     * 误送进后级状态机，导致短按边沿被拆成多次触发。 */
    if ((keys & (PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT)) ==
        (PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT)) {
        keys &= ~(PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT);
    }
    if ((keys & (PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN)) ==
        (PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN)) {
        keys &= ~(PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN);
    }

    return keys & PS_APP_GBA_KEY_MASK_ALL;
}

static void PsAppInput_UpdatePulseAndDebounce(PsAppInputState *state,
                                              u32 source_mask) {
    u32 nonstick_source_mask;
    u32 nonstick_level_keys;
    u32 bit;

    if (state == NULL) {
        return;
    }

    nonstick_source_mask = source_mask & PS_APP_INPUT_NONSTICK_SOURCE_MASK;
    nonstick_level_keys = PsAppInput_MapNonStickSourceToGbaKeys(nonstick_source_mask) &
                          PS_APP_GBA_KEY_MASK_ACTION;
    state->pending_mapped_keys = nonstick_source_mask;

    for (bit = 0U; bit < PS_APP_GBA_KEY_COUNT; ++bit) {
        u8 raw_pressed;
        u8 debounced_pressed;
        u8 threshold;
        const u32 bit_mask = (1U << bit);

        raw_pressed = ((nonstick_level_keys & bit_mask) != 0U) ? 1U : 0U;
        debounced_pressed = ((state->debounced_nonstick_keys & bit_mask) != 0U) ? 1U : 0U;
        if (raw_pressed == debounced_pressed) {
            state->debounce_count[bit] = 0U;
            continue;
        }

        if (state->debounce_count[bit] < 0xFFU) {
            state->debounce_count[bit]++;
        }
        threshold = (raw_pressed != 0U) ?
                    (u8)PS_APP_INPUT_PRESS_DEBOUNCE_TICKS :
                    (u8)PS_APP_INPUT_RELEASE_DEBOUNCE_TICKS;
        if (threshold == 0U) {
            threshold = 1U;
        }
        if (state->debounce_count[bit] < threshold) {
            continue;
        }

        state->debounce_count[bit] = 0U;
        if (raw_pressed != 0U) {
            /* 仅在“去抖后的上升沿”装载一次短脉冲，按住不重触发。 */
            state->debounced_nonstick_keys |= bit_mask;
            state->short_pulse_ticks[bit] = (u8)PS_APP_INPUT_SHORT_PULSE_TICKS;
        } else {
            state->debounced_nonstick_keys &= ~bit_mask;
        }
    }
}

static void PsAppInput_RefreshMappedKeys(PsAppInputState *state,
                                         u32 source_mask,
                                         u8 consume_pulse_tick) {
    u32 conditioned_keys;
    u32 stick_keys;
    u32 dpad_keys;
    u32 nonstick_keys;
    u32 bit;

    if (state == NULL) {
        return;
    }

    stick_keys = PsAppInput_StickMappedKeysFromSource(source_mask);
    dpad_keys = PsAppInput_MapNonStickSourceToGbaKeys(source_mask & PS_APP_INPUT_DPAD_SOURCE_MASK) &
                PS_APP_GBA_KEY_MASK_DPAD;
    nonstick_keys = state->debounced_nonstick_keys & PS_APP_GBA_KEY_MASK_ACTION;
    conditioned_keys = (stick_keys | dpad_keys | nonstick_keys);

    for (bit = 0U; bit < PS_APP_GBA_KEY_COUNT; ++bit) {
        const u32 bit_mask = (1U << bit);
        if (((stick_keys & bit_mask) != 0U) ||
            ((PS_APP_GBA_KEY_MASK_DPAD & bit_mask) != 0U)) {
            /* 摇杆方向保持“按住即连续”行为。 */
            continue;
        }
        if (state->short_pulse_ticks[bit] > 0U) {
            conditioned_keys |= bit_mask;
            if (consume_pulse_tick != 0U) {
                state->short_pulse_ticks[bit]--;
            }
        }
    }

    if ((conditioned_keys & (PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT)) ==
        (PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT)) {
        conditioned_keys &= ~(PS_APP_GBA_KEY_LEFT | PS_APP_GBA_KEY_RIGHT);
    }
    if ((conditioned_keys & (PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN)) ==
        (PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN)) {
        conditioned_keys &= ~(PS_APP_GBA_KEY_UP | PS_APP_GBA_KEY_DOWN);
    }

    state->sampled_keys = (conditioned_keys & PS_APP_GBA_KEY_MASK_ALL);
}

static XStatus PsAppInput_UpdateFromReport(PsAppInputContext *ctx, const u8 *report, u32 report_len) {
    PsXinputPadState parsed;
    PsAppInputState *state;
    u32 copy_len;
    u32 raw_mapped_keys;
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL) || (report == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    if (state->enabled == 0U) {
        return XST_FAILURE;
    }

    if (PsAppInput_IsExpectedMainReport(state, report, report_len) == 0U) {
        state->parse_error_count++;
        return XST_FAILURE;
    }

    status = PsXinput_ParseInputReport(report, report_len, &parsed);
    if (status != XST_SUCCESS) {
        state->parse_error_count++;
        return status;
    }

    raw_mapped_keys = PsAppInput_MapXinputToGbaKeys(&parsed);
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
    state->source_snapshot_mask = PsAppInput_BuildSourceMaskFromPad(&parsed);
    state->raw_mapped_keys = raw_mapped_keys & PS_APP_GBA_KEY_MASK_ALL;
    state->pending_mapped_keys = state->source_snapshot_mask & PS_APP_INPUT_NONSTICK_SOURCE_MASK;
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
    PsAppInput_ResetKeyConditioner(state);
}

void PsAppInput_Service(PsAppInputContext *ctx) {
    u32 prev_sampled_keys;
    u32 curr_sampled_keys;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }
    if (ctx->state->enabled == 0U) {
        return;
    }

    prev_sampled_keys = ctx->state->sampled_keys & PS_APP_GBA_KEY_MASK_ALL;
    /* 行业常见做法：USB 回调只更新“最新采样”，按键去抖与边沿触发统一放到固定节拍，
     * 这样可以把抖动/并发时序影响收敛到一个状态机里，减少短按漏按和多按。 */
    PsAppInput_UpdatePulseAndDebounce(ctx->state, ctx->state->source_snapshot_mask);
    PsAppInput_RefreshMappedKeys(ctx->state,
                                 ctx->state->source_snapshot_mask,
                                 1U);
    curr_sampled_keys = ctx->state->sampled_keys & PS_APP_GBA_KEY_MASK_ALL;
    ctx->state->frame_press_keys |= (curr_sampled_keys & ~prev_sampled_keys);

    PsAppInput_BackendPoll(ctx);
}

void PsAppInput_PublishFrame(PsAppInputContext *ctx, u32 frame_token) {
    PsAppInputState *state;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    state = ctx->state;
    if (state->enabled == 0U) {
        return;
    }
    if ((state->frame_sync_ready != 0U) && (state->frame_sync_token == frame_token)) {
        return;
    }

    state->mapped_keys = (state->sampled_keys | state->frame_press_keys) & PS_APP_GBA_KEY_MASK_ALL;
    state->frame_press_keys = 0U;
    state->frame_sync_token = frame_token;
    state->frame_sync_ready = 1U;
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
    PsAppInput_ResetKeyConditioner(state);
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
    PsAppInput_ResetKeyConditioner(state);
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
    return (ctx->state->mapped_keys & PS_APP_GBA_KEY_MASK_ALL);
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
    xil_printf("[INPUT] reports=%u parse_err=%u unsupported=%u raw=0x%03x src_nonstick=0x%08x deb_nonstick=0x%03x mapped=0x%03x committed=0x%03x btn=0x%04x lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\r\n",
               (unsigned int)state->report_count,
               (unsigned int)state->parse_error_count,
               (unsigned int)state->unsupported_report_count,
               (unsigned int)(state->raw_mapped_keys & PS_APP_GBA_KEY_MASK_ALL),
               (unsigned int)(state->pending_mapped_keys & PS_APP_INPUT_NONSTICK_SOURCE_MASK),
               (unsigned int)(state->debounced_nonstick_keys & PS_APP_GBA_KEY_MASK_ALL),
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
