#ifndef PS_APP_DIAG_CONTEXT_H
#define PS_APP_DIAG_CONTEXT_H

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsGbaRegs PsGbaRegs;
typedef struct PsHdmiVdma PsHdmiVdma;
typedef struct PsAppVideoContext PsAppVideoContext;

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
