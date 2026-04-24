#include "Input/Inc/ps_xinput_xbox360.h"

#include <string.h>

#define PS_XINPUT_KNOWN_BUTTON_MASK (PS_XINPUT_BUTTON_MASK_UP | PS_XINPUT_BUTTON_MASK_DOWN | \
                                     PS_XINPUT_BUTTON_MASK_LEFT | PS_XINPUT_BUTTON_MASK_RIGHT | \
                                     PS_XINPUT_BUTTON_MASK_START | PS_XINPUT_BUTTON_MASK_BACK | \
                                     PS_XINPUT_BUTTON_MASK_L3 | PS_XINPUT_BUTTON_MASK_R3 | \
                                     PS_XINPUT_BUTTON_MASK_LB | PS_XINPUT_BUTTON_MASK_RB | \
                                     PS_XINPUT_BUTTON_MASK_GUIDE | PS_XINPUT_BUTTON_MASK_A | \
                                     PS_XINPUT_BUTTON_MASK_B | PS_XINPUT_BUTTON_MASK_X | \
                                     PS_XINPUT_BUTTON_MASK_Y)

static u16 PsXinput_ReadLe16(const u8 *src) {
    return (u16)((u16)src[0] | ((u16)src[1] << 8));
}

static s16 PsXinput_ReadLeS16(const u8 *src) {
    return (s16)PsXinput_ReadLe16(src);
}

static XStatus PsXinput_ParsePayloadAt(const u8 *report,
                                       u32 report_len,
                                       u32 payload_offset,
                                       PsXinputPadState *out_pad) {
    if ((report == NULL) || (out_pad == NULL)) {
        return XST_INVALID_PARAM;
    }
    if ((payload_offset >= report_len) || ((report_len - payload_offset) < 12U)) {
        return XST_INVALID_PARAM;
    }

    out_pad->buttons = PsXinput_ReadLe16(&report[payload_offset]);
    out_pad->lt = report[payload_offset + 2U];
    out_pad->rt = report[payload_offset + 3U];
    out_pad->lx = PsXinput_ReadLeS16(&report[payload_offset + 4U]);
    out_pad->ly = PsXinput_ReadLeS16(&report[payload_offset + 6U]);
    out_pad->rx = PsXinput_ReadLeS16(&report[payload_offset + 8U]);
    out_pad->ry = PsXinput_ReadLeS16(&report[payload_offset + 10U]);
    return XST_SUCCESS;
}

static u8 PsXinput_IsLikelyPadState(const PsXinputPadState *pad) {
    if (pad == NULL) {
        return 0U;
    }
    if ((pad->buttons & (u16)(~PS_XINPUT_KNOWN_BUTTON_MASK)) != 0U) {
        return 0U;
    }
    return 1U;
}

static s16 PsXinput_ClampS16(s32 value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (s16)value;
}

static s16 PsXinput_SwitchAxis12ToS16(u16 raw, u8 invert)
{
    s32 signed_axis;

    if (raw > 4095U) {
        raw = 4095U;
    }

    if (invert != 0U) {
        signed_axis = ((s32)2048 - (s32)raw) * 16;
    } else {
        signed_axis = ((s32)raw - (s32)2048) * 16;
    }

    return PsXinput_ClampS16(signed_axis);
}

static void PsXinput_MapSwitchButtons(u8 b0, u8 b1, u8 b2, PsXinputPadState *out_pad)
{
    u16 buttons = 0U;

    if ((b2 & (1U << 1U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_UP;
    }
    if ((b2 & (1U << 0U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_DOWN;
    }
    if ((b2 & (1U << 3U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_LEFT;
    }
    if ((b2 & (1U << 2U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_RIGHT;
    }
    if ((b1 & (1U << 1U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_START;
    }
    if ((b1 & (1U << 0U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_BACK;
    }
    if ((b2 & (1U << 6U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_LB;
    }
    if ((b0 & (1U << 6U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_RB;
    }
    if ((b1 & (1U << 4U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_GUIDE;
    }
    if ((b1 & (1U << 3U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_L3;
    }
    if ((b1 & (1U << 2U)) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_R3;
    }

    /* Switch HID 形态按手柄丝印语义映射，避免 A/B/X/Y 标签反向。 */
    if ((b0 & (1U << 2U)) != 0U) { /* Switch B (bottom) */
        buttons |= PS_XINPUT_BUTTON_MASK_B;
    }
    if ((b0 & (1U << 3U)) != 0U) { /* Switch A (right) */
        buttons |= PS_XINPUT_BUTTON_MASK_A;
    }
    if ((b0 & (1U << 0U)) != 0U) { /* Switch Y (left) */
        buttons |= PS_XINPUT_BUTTON_MASK_Y;
    }
    if ((b0 & (1U << 1U)) != 0U) { /* Switch X (top) */
        buttons |= PS_XINPUT_BUTTON_MASK_X;
    }

    out_pad->buttons = buttons;
    out_pad->lt = ((b2 & (1U << 7U)) != 0U) ? 255U : 0U;
    out_pad->rt = ((b0 & (1U << 7U)) != 0U) ? 255U : 0U;
}

static XStatus PsXinput_ParseSwitchPackedReport(const u8 *report, u32 report_len, PsXinputPadState *out_pad)
{
    u16 lx_raw;
    u16 ly_raw;
    u16 rx_raw;
    u16 ry_raw;

    if ((report == NULL) || (out_pad == NULL) || (report_len < 12U)) {
        return XST_INVALID_PARAM;
    }

    PsXinput_MapSwitchButtons(report[3], report[4], report[5], out_pad);

    lx_raw = (u16)report[6] | ((u16)(report[7] & 0x0FU) << 8U);
    ly_raw = ((u16)(report[7] >> 4U) & 0x0FU) | ((u16)report[8] << 4U);
    rx_raw = (u16)report[9] | ((u16)(report[10] & 0x0FU) << 8U);
    ry_raw = ((u16)(report[10] >> 4U) & 0x0FU) | ((u16)report[11] << 4U);

    out_pad->lx = PsXinput_SwitchAxis12ToS16(lx_raw, 0U);
    out_pad->ly = PsXinput_SwitchAxis12ToS16(ly_raw, 1U);
    out_pad->rx = PsXinput_SwitchAxis12ToS16(rx_raw, 0U);
    out_pad->ry = PsXinput_SwitchAxis12ToS16(ry_raw, 1U);

    return XST_SUCCESS;
}

u8 PsXinput_IsInterfaceCompatible(u8 interface_class,
                                  u8 interface_subclass,
                                  u8 interface_protocol) {
    if ((interface_class == PS_XINPUT_INTERFACE_CLASS) &&
        (interface_subclass == PS_XINPUT_INTERFACE_SUBCLASS) &&
        (interface_protocol == PS_XINPUT_INTERFACE_PROTOCOL)) {
        return 1U;
    }
    return 0U;
}

u8 PsXinput_IsLikely8BitDo(u16 vendor_id, u16 product_id) {
    if (vendor_id != 0x2DC8U) {
        return 0U;
    }

    /* 这里集中维护 8BitDo 接收器的 PID，后续 USB Host 会据此走专用
     * startup sideband 流程；310B 是本次协议分析确认过的 U2W PC 接收器。 */
    if ((product_id == 0x3106U) ||
        (product_id == 0x3109U) ||
        (product_id == 0x310BU) ||
        (product_id == 0x3110U) ||
        (product_id == 0x3111U)) {
        return 1U;
    }

    return 0U;
}

XStatus PsXinput_ParseInputReport(const u8 *report, u32 report_len, PsXinputPadState *out_pad) {
    PsXinputPadState parsed;
    u32 idx;

    if ((report == NULL) || (out_pad == NULL)) {
        return XST_INVALID_PARAM;
    }

    /*
     * Switch Pro HID 兼容：
     * 0x30/0x21 为常见完整状态包。
     * 0x3F 简化报告在 G30S TE 接收器上布局不同，会造成空闲假按键，
     * 已请求 0x30 full report mode 后这里不再消费 0x3F。
     * 注意要优先于通用 XInput 解析，否则 0x30 包会被误按 XInput 偏移解释。
     */
    if ((report_len >= 13U) &&
        ((report[0] == 0x30U) || (report[0] == 0x21U))) {
        return PsXinput_ParseSwitchPackedReport(report, report_len, out_pad);
    }

    /* 8BitDo U2W 的 MI_00 主输入流与标准 XInput 风格一致，常见报文为：
     * 00 14 <buttons_le16> <lt> <rt> <lx> <ly> <rx> <ry> ...
     * 这里兼容带/不带前缀长度字节的几种主机侧抓包形态。 */
    if ((report_len >= 20U) &&
        (report[0] == PS_XINPUT_REPORT_ID_DEFAULT) &&
        (report[1] == PS_XINPUT_REPORT_SIZE_INPUT)) {
        return PsXinput_ParsePayloadAt(report, report_len, 2U, out_pad);
    }

    if ((report_len >= 19U) && (report[0] == PS_XINPUT_REPORT_SIZE_INPUT)) {
        return PsXinput_ParsePayloadAt(report, report_len, 1U, out_pad);
    }

    if ((report_len >= 12U) && (report_len <= PS_XINPUT_MAX_REPORT_BYTES)) {
        return PsXinput_ParsePayloadAt(report, report_len, 0U, out_pad);
    }

    /* 兜底扫描：兼容“前缀字节变化、总长度 20B/32B、主 payload 偏移”的 XInput 变体。 */
    for (idx = 0U; idx + 13U <= report_len; ++idx) {
        if (report[idx] != PS_XINPUT_REPORT_SIZE_INPUT) {
            continue;
        }
        if (PsXinput_ParsePayloadAt(report, report_len, idx + 1U, &parsed) != XST_SUCCESS) {
            continue;
        }
        if (PsXinput_IsLikelyPadState(&parsed) == 0U) {
            continue;
        }
        *out_pad = parsed;
        return XST_SUCCESS;
    }

    for (idx = 0U; idx + 14U <= report_len; ++idx) {
        if ((report[idx] != PS_XINPUT_REPORT_ID_DEFAULT) ||
            (report[idx + 1U] != PS_XINPUT_REPORT_SIZE_INPUT)) {
            continue;
        }
        if (PsXinput_ParsePayloadAt(report, report_len, idx + 2U, &parsed) != XST_SUCCESS) {
            continue;
        }
        if (PsXinput_IsLikelyPadState(&parsed) == 0U) {
            continue;
        }
        *out_pad = parsed;
        return XST_SUCCESS;
    }

    return XST_INVALID_PARAM;
}

void PsXinput_BuildRumbleOutputReport(u8 large_motor,
                                      u8 small_motor,
                                      u8 out_report[PS_XINPUT_REPORT_SIZE_OUTPUT]) {
    if (out_report == NULL) {
        return;
    }

    out_report[0] = PS_XINPUT_REPORT_ID_DEFAULT;
    out_report[1] = PS_XINPUT_REPORT_SIZE_OUTPUT;
    out_report[2] = 0x00U;
    out_report[3] = large_motor;
    out_report[4] = small_motor;
    out_report[5] = 0x00U;
    out_report[6] = 0x00U;
    out_report[7] = 0x00U;
}

void PsXinput_BuildLedOutputReport(u8 led_pattern,
                                   u8 out_report[PS_XINPUT_REPORT_SIZE_OUTPUT]) {
    if (out_report == NULL) {
        return;
    }

    memset(out_report, 0, PS_XINPUT_REPORT_SIZE_OUTPUT);
    out_report[0] = 0x01U;
    out_report[1] = PS_XINPUT_REPORT_SIZE_OUTPUT;
    out_report[2] = led_pattern;
}
