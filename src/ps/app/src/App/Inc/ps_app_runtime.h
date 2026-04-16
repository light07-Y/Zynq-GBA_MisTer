#ifndef PS_APP_RUNTIME_H
#define PS_APP_RUNTIME_H

#include "xstatus.h"

#include "App/Inc/ps_app_runtime_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void PsAppRuntime_ApplyShadowConfig(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_InitUart(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_InitPsGpio(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_InitSystem(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_LoadRomFromSd(PsAppRuntimeContext *ctx, const char *requested_path);
XStatus PsAppRuntime_ProgramAudio(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_ApplyAudioOutputState(PsAppRuntimeContext *ctx);
XStatus PsAppRuntime_SetAudioVolume(PsAppRuntimeContext *ctx, u32 volume_percent);
void PsAppRuntime_ReadPsButtonRawLevels(PsAppRuntimeContext *ctx,
                                        u32 *btn4_level,
                                        u32 *btn5_level,
                                        u32 *bank1_data,
                                        u32 *bank1_dir,
                                        u32 *bank1_outen,
                                        u32 *mio50_cfg,
                                        u32 *mio51_cfg);
u32 PsAppRuntime_ReadPsButtonMask(PsAppRuntimeContext *ctx);
void PsAppRuntime_Service(PsAppRuntimeContext *ctx);
void PsAppMonitorTask(void *arg);
void PsAppSaveTask(void *arg);
void PsAppVideoPresentTask(void *arg);

#ifdef __cplusplus
}
#endif

#endif
