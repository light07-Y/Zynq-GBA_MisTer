#ifndef PS_APP_INPUT_CONTEXT_H
#define PS_APP_INPUT_CONTEXT_H

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsAppInputContext {
    PsAppInputState *state;
} PsAppInputContext;

#ifdef __cplusplus
}
#endif

#endif
