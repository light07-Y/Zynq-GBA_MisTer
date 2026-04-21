#ifndef PS_APP_CONSOLE_INTERNAL_H
#define PS_APP_CONSOLE_INTERNAL_H

#include "App/Inc/ps_app_console.h"
#include "App/Inc/ps_app_runtime.h"

int PsAppConsole_ParseOnOff(const char *s, u32 *value_out);
int PsAppConsole_ParseU32Value(const char *s, u32 *value_out);
int PsAppConsole_ParseHexBytes(const char *text,
                               u8 *out_buf,
                               u32 out_buf_size,
                               u32 *out_len);
const char *PsAppConsole_BiosModeName(u8 bios_mode);

u8 PsAppConsole_HandleIoCommands(PsAppConsoleContext *ctx, const char *cmd);
u8 PsAppConsole_HandleDiagnosticCommands(PsAppConsoleContext *ctx, const char *cmd);
u8 PsAppConsole_HandleRuntimeCommands(PsAppConsoleContext *ctx, const char *cmd);
u8 PsAppConsole_HandleMediaCommands(PsAppConsoleContext *ctx, const char *cmd);

void PsAppConsole_PrintDiagnosticHelp(void);
void PsAppConsole_PrintRuntimeHelp(void);
void PsAppConsole_PrintMediaHelp(void);

#endif
