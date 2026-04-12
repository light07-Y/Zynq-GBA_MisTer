#ifndef PS_APP_DIAG_CONTEXT_H
#define PS_APP_DIAG_CONTEXT_H

#include "Common/Inc/ps_app_state.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Video/Inc/ps_app_video_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsAppDiagContext {
    PsGbaRegs *regs;
    PsHdmiVdma *vdma;
    const PsAppShadowConfig *config;
    const PsAppAudioState *audio;
    PsAppVideoContext *video;
    const PsAppRomState *rom;
    PsAppDiagState *diag;
} PsAppDiagContext;

#ifdef __cplusplus
}
#endif

#endif
