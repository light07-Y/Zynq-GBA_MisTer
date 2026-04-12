#ifndef PS_APP_VIDEO_CONTEXT_H
#define PS_APP_VIDEO_CONTEXT_H

#include "Common/Inc/ps_app_state.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"

#ifdef __cplusplus
extern "C" {
#endif

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
