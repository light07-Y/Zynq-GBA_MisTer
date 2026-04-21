#ifndef PS_APP_VIDEO_CONTEXT_H
#define PS_APP_VIDEO_CONTEXT_H

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsHdmiVdma PsHdmiVdma;
typedef struct PsGbaRegs PsGbaRegs;

typedef struct PsAppVideoContext {
    PsHdmiVdma *vdma;
    PsGbaRegs *regs;
    PsAppVideoState *state;
    const PsAppRomState *rom;
} PsAppVideoContext;

#ifdef __cplusplus
}
#endif

#endif
