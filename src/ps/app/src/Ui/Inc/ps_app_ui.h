#ifndef PS_APP_UI_H
#define PS_APP_UI_H

#include "xstatus.h"

#include "Ui/Inc/ps_app_ui_context.h"

#ifdef __cplusplus
extern "C" {
#endif

XStatus PsAppUi_Init(PsAppUiContext *ctx);
void PsAppUi_RequestRefresh(PsAppUiContext *ctx);
void PsAppUiTask(void *arg);

#ifdef __cplusplus
}
#endif

#endif
