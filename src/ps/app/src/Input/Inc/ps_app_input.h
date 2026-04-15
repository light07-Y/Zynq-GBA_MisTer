#ifndef PS_APP_INPUT_H
#define PS_APP_INPUT_H

#include "xstatus.h"

#include "Input/Inc/ps_app_input_context.h"
#include "Input/Inc/ps_xinput_xbox360.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsAppInputUsbDeviceInfo {
    u16 vendor_id;
    u16 product_id;
    u8 interface_number;
    u8 interface_class;
    u8 interface_subclass;
    u8 interface_protocol;
    u8 ep_in_addr;
    u8 ep_out_addr;
    u8 ep_in_interval_ms;
    u8 ep_out_interval_ms;
} PsAppInputUsbDeviceInfo;

/* 输入源位图：
 * [0..15] 直接复用 XInput buttons 位；
 * [16..] 为触发键/摇杆阈值后的数字化方向。 */
#define PS_APP_INPUT_SRC_MASK_LT        (1UL << 16U)
#define PS_APP_INPUT_SRC_MASK_RT        (1UL << 17U)
#define PS_APP_INPUT_SRC_MASK_LS_RIGHT  (1UL << 18U)
#define PS_APP_INPUT_SRC_MASK_LS_LEFT   (1UL << 19U)
#define PS_APP_INPUT_SRC_MASK_LS_UP     (1UL << 20U)
#define PS_APP_INPUT_SRC_MASK_LS_DOWN   (1UL << 21U)

void PsAppInput_Init(PsAppInputContext *ctx);
void PsAppInput_Service(PsAppInputContext *ctx);
void PsAppInput_PublishFrame(PsAppInputContext *ctx, u32 frame_token);

XStatus PsAppInput_OnUsbXInputAttached(PsAppInputContext *ctx,
                                       const PsAppInputUsbDeviceInfo *info);
void PsAppInput_OnUsbDetached(PsAppInputContext *ctx);
XStatus PsAppInput_OnInterruptInReport(PsAppInputContext *ctx,
                                       const u8 *report,
                                       u32 report_len);
XStatus PsAppInput_InjectRawReport(PsAppInputContext *ctx,
                                   const u8 *report,
                                   u32 report_len);

u8 PsAppInput_ShouldOverrideKeys(const PsAppInputContext *ctx);
u8 PsAppInput_IsReleasePending(const PsAppInputContext *ctx);
void PsAppInput_ClearReleasePending(PsAppInputContext *ctx);
u32 PsAppInput_GetMappedKeys(const PsAppInputContext *ctx);
void PsAppInput_BuildRumbleReport(u8 large_motor,
                                  u8 small_motor,
                                  u8 out_report[PS_XINPUT_REPORT_SIZE_OUTPUT]);
void PsAppInput_PrintStatus(const PsAppInputContext *ctx);

void PsAppInput_BackendPoll(PsAppInputContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
