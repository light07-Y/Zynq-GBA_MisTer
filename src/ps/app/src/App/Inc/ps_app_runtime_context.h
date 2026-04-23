#ifndef PS_APP_RUNTIME_CONTEXT_H
#define PS_APP_RUNTIME_CONTEXT_H

#include "xuartps.h"
#include "Common/Inc/ps_xgpiops_compat.h"

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsAudioCodec PsAudioCodec;
typedef struct PsHdmiVdma PsHdmiVdma;
typedef struct PsGbaRegs PsGbaRegs;

struct PsAppDiagContext;
struct PsAppSaveContext;
struct PsAppInputContext;
struct PsAppUsbHostContext;
struct PsAppVideoContext;
struct PsAppHdmiLinkContext;
struct PsAppUiContext;

typedef struct PsAppRuntimeContext {
    PsAudioCodec *codec;
    PsHdmiVdma *vdma;
    PsGbaRegs *regs;
    XUartPs *uart;
    XGpioPs *ps_gpio;
    PsAppShadowConfig *config;
    PsAppAudioState *audio;
    PsAppVideoState *video;
    PsAppRomState *rom;
    PsAppSaveState *save;
    PsAppFeatureState *feature;
    PsAppDiagState *diag;
    PsAppStateFeatureConfig *state_feature;
    PsAppRtcPersistState *rtc;
    PsAppSensorState *sensor;
    PsAppUsbHostState *usb_host;
    PsAppInputState *input;
    PsAppHdmiLinkState *hdmi;
    PsAppUiState *ui;
    u8 ps_gpio_ready;
    u8 reserved0;
    u8 reserved1;
    u8 reserved2;
    u32 ps_btn_last_mask;
    struct PsAppSaveContext *save_ctx;
    struct PsAppInputContext *input_ctx;
    struct PsAppUsbHostContext *usb_host_ctx;
    struct PsAppVideoContext *video_ctx;
    struct PsAppDiagContext *diag_ctx;
    struct PsAppHdmiLinkContext *hdmi_ctx;
    struct PsAppUiContext *ui_ctx;
} PsAppRuntimeContext;

typedef struct PsAppConsoleContext {
    PsAppRuntimeContext *runtime;
    struct PsAppVideoContext *video;
    struct PsAppDiagContext *diag;
    XUartPs *uart;
} PsAppConsoleContext;

#ifdef __cplusplus
}
#endif

#endif
