#include "Input/Inc/ps_xinput_xbox360.h"

#include <string.h>

static u16 PsXinput_ReadLe16(const u8 *src) {
    return (u16)((u16)src[0] | ((u16)src[1] << 8));
}

static s16 PsXinput_ReadLeS16(const u8 *src) {
    return (s16)PsXinput_ReadLe16(src);
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
    const u8 *payload;
    u32 payload_len;

    if ((report == NULL) || (out_pad == NULL)) {
        return XST_INVALID_PARAM;
    }

    /* 8BitDo U2W 的 MI_00 主输入流与标准 XInput 风格一致，常见报文为：
     * 00 14 <buttons_le16> <lt> <rt> <lx> <ly> <rx> <ry> ...
     * 这里兼容带/不带前缀长度字节的几种主机侧抓包形态。 */
    if ((report_len >= 20U) &&
        (report[0] == PS_XINPUT_REPORT_ID_DEFAULT) &&
        (report[1] == PS_XINPUT_REPORT_SIZE_INPUT)) {
        payload = &report[2];
        payload_len = report_len - 2U;
    } else if ((report_len >= 19U) && (report[0] == PS_XINPUT_REPORT_SIZE_INPUT)) {
        payload = &report[1];
        payload_len = report_len - 1U;
    } else if (report_len >= 14U) {
        payload = report;
        payload_len = report_len;
    } else {
        return XST_INVALID_PARAM;
    }

    if (payload_len < 12U) {
        return XST_INVALID_PARAM;
    }

    out_pad->buttons = PsXinput_ReadLe16(&payload[0]);
    out_pad->lt = payload[2];
    out_pad->rt = payload[3];
    out_pad->lx = PsXinput_ReadLeS16(&payload[4]);
    out_pad->ly = PsXinput_ReadLeS16(&payload[6]);
    out_pad->rx = PsXinput_ReadLeS16(&payload[8]);
    out_pad->ry = PsXinput_ReadLeS16(&payload[10]);

    return XST_SUCCESS;
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
