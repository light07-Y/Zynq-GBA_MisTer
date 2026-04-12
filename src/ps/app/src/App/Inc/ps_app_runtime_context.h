#ifndef PS_APP_RUNTIME_CONTEXT_H
#define PS_APP_RUNTIME_CONTEXT_H

#include "xuartps.h"

#include "Audio/Inc/ps_audio_codec.h"
#include "Common/Inc/ps_app_state.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PsAppDiagContext;
struct PsAppVideoContext;

typedef struct PsAppRuntimeContext {
    PsAudioCodec *codec;
    PsHdmiVdma *vdma;
    PsGbaRegs *regs;
    XUartPs *uart;
    PsAppShadowConfig *config;
    PsAppAudioState *audio;
    PsAppVideoState *video;
    PsAppRomState *rom;
    PsAppDiagState *diag;
    struct PsAppVideoContext *video_ctx;
    struct PsAppDiagContext *diag_ctx;
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
