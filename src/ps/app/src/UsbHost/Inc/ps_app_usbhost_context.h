#ifndef PS_APP_USBHOST_CONTEXT_H
#define PS_APP_USBHOST_CONTEXT_H

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PsAppInputContext;

typedef struct PsAppUsbHostContext {
    PsAppUsbHostState *state;
    struct PsAppInputContext *input;
} PsAppUsbHostContext;

#ifdef __cplusplus
}
#endif

#endif
