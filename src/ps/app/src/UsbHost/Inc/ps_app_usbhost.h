#ifndef PS_APP_USBHOST_H
#define PS_APP_USBHOST_H

#include "xstatus.h"

#include "UsbHost/Inc/ps_app_usbhost_context.h"

#ifdef __cplusplus
extern "C" {
#endif

XStatus PsAppUsbHost_Init(PsAppUsbHostContext *ctx);
void PsAppUsbHost_Service(PsAppUsbHostContext *ctx);
void PsAppUsbHost_PrintStatus(const PsAppUsbHostContext *ctx);
XStatus PsAppUsbHost_SetRumble(PsAppUsbHostContext *ctx, u8 large_motor, u8 small_motor);
void PsAppUsbHost_ForceRootHubScan(PsAppUsbHostContext *ctx);
XStatus PsAppUsbHost_PortReset(PsAppUsbHostContext *ctx);
XStatus PsAppUsbHost_SetVbusDrive(PsAppUsbHostContext *ctx, u8 enable);
XStatus PsAppUsbHost_DumpUlpi(PsAppUsbHostContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
