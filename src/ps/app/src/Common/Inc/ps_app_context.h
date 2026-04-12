#ifndef PS_APP_CONTEXT_H
#define PS_APP_CONTEXT_H

#include "App/Inc/ps_app_runtime_context.h"
#include "Diagnostics/Inc/ps_app_diag_context.h"
#include "Video/Inc/ps_app_video_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PsAudioCodec codec;
    PsHdmiVdma vdma;
    PsGbaRegs regs;
    XUartPs uart;
    PsAppShadowConfig config;
    PsAppAudioState audio;
    PsAppVideoState video;
    PsAppRomState rom;
    PsAppDiagState diag;
    PsAppVideoContext video_ctx;
    PsAppDiagContext diag_ctx;
    PsAppRuntimeContext runtime_ctx;
    PsAppConsoleContext console_ctx;
} PsAppContext;

void PsAppContext_Init(PsAppContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
