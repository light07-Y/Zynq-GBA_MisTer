#ifndef PS_APP_SAVE_CONTEXT_H
#define PS_APP_SAVE_CONTEXT_H

#include "Common/Inc/ps_app_state.h"
#include "Gba/Inc/ps_gba_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsAppSaveContext {
    PsGbaRegs *regs;
    PsAppSaveState *state;
    const PsAppRomState *rom;
} PsAppSaveContext;

#ifdef __cplusplus
}
#endif

#endif
