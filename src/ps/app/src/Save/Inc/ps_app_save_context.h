#ifndef PS_APP_SAVE_CONTEXT_H
#define PS_APP_SAVE_CONTEXT_H

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsGbaRegs PsGbaRegs;

typedef struct PsAppSaveContext {
    /* 依赖约定：
     * 这些指针均由 runtime 主上下文持有，save 模块只借用不释放。 */
    PsGbaRegs *regs;
    PsAppSaveState *state;
    const PsAppRomState *rom;
} PsAppSaveContext;

#ifdef __cplusplus
}
#endif

#endif
