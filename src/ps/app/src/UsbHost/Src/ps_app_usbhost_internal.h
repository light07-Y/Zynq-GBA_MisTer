#ifndef PS_APP_USBHOST_INTERNAL_H
#define PS_APP_USBHOST_INTERNAL_H

#include "UsbHost/Inc/ps_app_usbhost.h"

#ifdef __cplusplus
extern "C" {
#endif

XStatus PsAppUsbHost_InitImpl(PsAppUsbHostContext *ctx);
void PsAppUsbHost_ServiceImpl(PsAppUsbHostContext *ctx);
void PsAppUsbHost_PrintStatusImpl(const PsAppUsbHostContext *ctx);
XStatus PsAppUsbHost_SetRumbleImpl(PsAppUsbHostContext *ctx, u8 large_motor, u8 small_motor);
void PsAppUsbHost_ForceRootHubScanImpl(PsAppUsbHostContext *ctx);
XStatus PsAppUsbHost_PortResetImpl(PsAppUsbHostContext *ctx);
XStatus PsAppUsbHost_SetVbusDriveImpl(PsAppUsbHostContext *ctx, u8 enable);
XStatus PsAppUsbHost_DumpUlpiImpl(PsAppUsbHostContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
