#ifndef PS_APP_RUNTIME_CONTEXT_H
#define PS_APP_RUNTIME_CONTEXT_H

#include "xuartps.h"
#if defined(__has_include)
#if __has_include("xgpiops.h")
#include "xgpiops.h"
#define PS_APP_HAS_XGPIOPS_HEADER 1
#else
#define PS_APP_HAS_XGPIOPS_HEADER 0
#ifndef PS_APP_XGPIOPS_PLACEHOLDER_DEFINED
#define PS_APP_XGPIOPS_PLACEHOLDER_DEFINED 1
typedef struct XGpioPs {
    unsigned int _placeholder;
} XGpioPs;
#endif
#endif
#else
#include "xgpiops.h"
#define PS_APP_HAS_XGPIOPS_HEADER 1
#endif

#include "Audio/Inc/ps_audio_codec.h"
#include "Common/Inc/ps_app_state.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PsAppDiagContext;
struct PsAppSaveContext;
struct PsAppInputContext;
struct PsAppUsbHostContext;
struct PsAppVideoContext;
struct PsAppHdmiLinkContext;

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
    PsAppDiagState *diag;
    PsAppUsbHostState *usb_host;
    PsAppInputState *input;
    PsAppHdmiLinkState *hdmi;
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
