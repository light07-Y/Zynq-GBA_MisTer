#include "UsbHost/Inc/ps_app_usbhost.h"

#include "UsbHost/Src/ps_app_usbhost_internal.h"

static XStatus PsAppUsbHostFacade_ValidateStateContext(const PsAppUsbHostContext *ctx)
{
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }
    return XST_SUCCESS;
}

static XStatus PsAppUsbHostFacade_ValidateInputContext(const PsAppUsbHostContext *ctx)
{
    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->input == NULL)) {
        return XST_INVALID_PARAM;
    }
    return XST_SUCCESS;
}

XStatus PsAppUsbHost_Init(PsAppUsbHostContext *ctx)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateInputContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }
    return PsAppUsbHost_InitImpl(ctx);
}

XStatus PsAppUsbHost_ServiceChecked(PsAppUsbHostContext *ctx)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateStateContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }

    PsAppUsbHost_ServiceImpl(ctx);
    return XST_SUCCESS;
}

void PsAppUsbHost_Service(PsAppUsbHostContext *ctx)
{
    (void)PsAppUsbHost_ServiceChecked(ctx);
}

XStatus PsAppUsbHost_PrintStatusChecked(const PsAppUsbHostContext *ctx)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateStateContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }

    PsAppUsbHost_PrintStatusImpl(ctx);
    return XST_SUCCESS;
}

void PsAppUsbHost_PrintStatus(const PsAppUsbHostContext *ctx)
{
    (void)PsAppUsbHost_PrintStatusChecked(ctx);
}

XStatus PsAppUsbHost_SetRumble(PsAppUsbHostContext *ctx, u8 large_motor, u8 small_motor)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateInputContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }
    return PsAppUsbHost_SetRumbleImpl(ctx, large_motor, small_motor);
}

XStatus PsAppUsbHost_ForceRootHubScanChecked(PsAppUsbHostContext *ctx)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateStateContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }

    PsAppUsbHost_ForceRootHubScanImpl(ctx);
    return XST_SUCCESS;
}

void PsAppUsbHost_ForceRootHubScan(PsAppUsbHostContext *ctx)
{
    (void)PsAppUsbHost_ForceRootHubScanChecked(ctx);
}

XStatus PsAppUsbHost_PortReset(PsAppUsbHostContext *ctx)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateStateContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }
    return PsAppUsbHost_PortResetImpl(ctx);
}

XStatus PsAppUsbHost_SetVbusDrive(PsAppUsbHostContext *ctx, u8 enable)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateStateContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }
    return PsAppUsbHost_SetVbusDriveImpl(ctx, enable);
}

XStatus PsAppUsbHost_DumpUlpi(PsAppUsbHostContext *ctx)
{
    XStatus status;

    status = PsAppUsbHostFacade_ValidateStateContext(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }
    return PsAppUsbHost_DumpUlpiImpl(ctx);
}
