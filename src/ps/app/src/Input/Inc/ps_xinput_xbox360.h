#ifndef PS_XINPUT_XBOX360_H
#define PS_XINPUT_XBOX360_H

#include "xil_types.h"
#include "xstatus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS_XINPUT_INTERFACE_CLASS      0xFFU
#define PS_XINPUT_INTERFACE_SUBCLASS   0x5DU
#define PS_XINPUT_INTERFACE_PROTOCOL   0x01U

#define PS_XINPUT_REPORT_ID_DEFAULT    0x00U
#define PS_XINPUT_REPORT_SIZE_INPUT    0x14U
#define PS_XINPUT_REPORT_SIZE_OUTPUT   0x08U
#define PS_XINPUT_MAX_REPORT_BYTES     32U

#define PS_XINPUT_BUTTON_MASK_UP       (1U << 0)
#define PS_XINPUT_BUTTON_MASK_DOWN     (1U << 1)
#define PS_XINPUT_BUTTON_MASK_LEFT     (1U << 2)
#define PS_XINPUT_BUTTON_MASK_RIGHT    (1U << 3)
#define PS_XINPUT_BUTTON_MASK_START    (1U << 4)
#define PS_XINPUT_BUTTON_MASK_BACK     (1U << 5)
#define PS_XINPUT_BUTTON_MASK_L3       (1U << 6)
#define PS_XINPUT_BUTTON_MASK_R3       (1U << 7)
#define PS_XINPUT_BUTTON_MASK_LB       (1U << 8)
#define PS_XINPUT_BUTTON_MASK_RB       (1U << 9)
#define PS_XINPUT_BUTTON_MASK_GUIDE    (1U << 10)
#define PS_XINPUT_BUTTON_MASK_A        (1U << 12)
#define PS_XINPUT_BUTTON_MASK_B        (1U << 13)
#define PS_XINPUT_BUTTON_MASK_X        (1U << 14)
#define PS_XINPUT_BUTTON_MASK_Y        (1U << 15)

typedef struct PsXinputPadState {
    u16 buttons;
    u8 lt;
    u8 rt;
    s16 lx;
    s16 ly;
    s16 rx;
    s16 ry;
} PsXinputPadState;

u8 PsXinput_IsInterfaceCompatible(u8 interface_class,
                                  u8 interface_subclass,
                                  u8 interface_protocol);
u8 PsXinput_IsLikely8BitDo(u16 vendor_id, u16 product_id);
XStatus PsXinput_ParseInputReport(const u8 *report, u32 report_len, PsXinputPadState *out_pad);
void PsXinput_BuildRumbleOutputReport(u8 large_motor,
                                      u8 small_motor,
                                      u8 out_report[PS_XINPUT_REPORT_SIZE_OUTPUT]);
void PsXinput_BuildLedOutputReport(u8 led_pattern,
                                   u8 out_report[PS_XINPUT_REPORT_SIZE_OUTPUT]);

#ifdef __cplusplus
}
#endif

#endif
