#ifndef PS_APP_HDMI_LINK_H
#define PS_APP_HDMI_LINK_H

#include "xstatus.h"

#include "Hdmi/Inc/ps_app_hdmi_link_context.h"

#ifdef __cplusplus
extern "C" {
#endif

XStatus PsAppHdmiLink_Init(PsAppHdmiLinkContext *ctx);
void PsAppHdmiLink_Service(PsAppHdmiLinkContext *ctx);
u8 PsAppHdmiLink_PresentEnabled(const PsAppHdmiLinkContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
