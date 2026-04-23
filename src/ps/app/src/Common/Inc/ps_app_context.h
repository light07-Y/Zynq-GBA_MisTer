#ifndef PS_APP_CONTEXT_H
#define PS_APP_CONTEXT_H

#include "Audio/Inc/ps_audio_codec.h"
#include "App/Inc/ps_app_runtime_context.h"
#include "Diagnostics/Inc/ps_app_diag_context.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Hdmi/Inc/ps_app_hdmi_link_context.h"
#include "Input/Inc/ps_app_input_context.h"
#include "Save/Inc/ps_app_save_context.h"
#include "Ui/Inc/ps_app_ui_context.h"
#include "UsbHost/Inc/ps_app_usbhost_context.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Video/Inc/ps_app_video_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PsAudioCodec codec;
    PsHdmiVdma vdma;
    PsGbaRegs regs;
    XUartPs uart;
    XGpioPs ps_gpio;
    PsAppShadowConfig config;
    PsAppAudioState audio;
    PsAppVideoState video;
    PsAppRomState rom;
    PsAppSaveState save;
    PsAppFeatureState feature;
    PsAppDiagState diag;
    PsAppStateFeatureConfig state_feature;
    PsAppRtcPersistState rtc;
    PsAppSensorState sensor;
    PsAppUsbHostState usb_host;
    PsAppInputState input;
    PsAppHdmiLinkState hdmi;
    PsAppUiState ui;
    PsAppVideoContext video_ctx;
    PsAppDiagContext diag_ctx;
    PsAppSaveContext save_ctx;
    PsAppUsbHostContext usb_host_ctx;
    PsAppInputContext input_ctx;
    PsAppHdmiLinkContext hdmi_ctx;
    PsAppUiContext ui_ctx;
    PsAppRuntimeContext runtime_ctx;
    PsAppConsoleContext console_ctx;
} PsAppContext;

void PsAppContext_Init(PsAppContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
