#ifndef PS_APP_UI_CONTEXT_H
#define PS_APP_UI_CONTEXT_H

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PsAppRuntimeContext;
struct PsAppVideoContext;

typedef struct PsAppUiContext {
    PsAppUiState *state;
    struct PsAppRuntimeContext *runtime;
    struct PsAppVideoContext *video;
} PsAppUiContext;

#ifdef __cplusplus
}
#endif

#endif
