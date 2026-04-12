#ifndef PS_APP_SAVE_H
#define PS_APP_SAVE_H

#include "xstatus.h"

#include "Save/Inc/ps_app_save_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void PsAppSave_Reset(PsAppSaveContext *ctx);
XStatus PsAppSave_PrepareForRom(PsAppSaveContext *ctx, const char *rom_path);
void PsAppSave_Service(PsAppSaveContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
