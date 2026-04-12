#ifndef PS_APP_RUNTIME_H
#define PS_APP_RUNTIME_H

#include "xstatus.h"

#include "App/Inc/ps_app_runtime_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void PsAppRuntime_ApplyShadowConfig(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_InitUart(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_InitSystem(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_LoadRomFromSd(PsAppRuntimeContext *ctx, const char *requested_path);
XStatus PsAppRuntime_ProgramAudio(PsAppRuntimeContext *ctx);
void PsAppRuntime_Service(PsAppRuntimeContext *ctx);
void PsAppMonitorTask(void *arg);

#ifdef __cplusplus
}
#endif

#endif
