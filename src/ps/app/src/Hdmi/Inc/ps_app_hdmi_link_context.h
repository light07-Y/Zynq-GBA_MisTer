#ifndef PS_APP_HDMI_LINK_CONTEXT_H
#define PS_APP_HDMI_LINK_CONTEXT_H

#include "xil_types.h"
#include "xiicps.h"

#if defined(__has_include)
#if __has_include("xgpiops.h")
#include "xgpiops.h"
#define PS_APP_HDMI_HAS_XGPIOPS_HEADER 1
#else
#define PS_APP_HDMI_HAS_XGPIOPS_HEADER 0
#ifndef PS_APP_XGPIOPS_PLACEHOLDER_DEFINED
#define PS_APP_XGPIOPS_PLACEHOLDER_DEFINED 1
typedef struct XGpioPs {
    unsigned int _placeholder;
} XGpioPs;
#endif
#endif
#else
#include "xgpiops.h"
#define PS_APP_HDMI_HAS_XGPIOPS_HEADER 1
#endif

#include "Common/Inc/ps_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsAppHdmiLinkContext {
    PsAppHdmiLinkState *state;
    XGpioPs *ps_gpio;
    XIicPs i2c_ddc;
    u8 i2c_ready;
    u8 hpd_pin;
    u8 reserved0;
    u8 reserved1;
    u32 last_poll_tick;
} PsAppHdmiLinkContext;

#ifdef __cplusplus
}
#endif

#endif
