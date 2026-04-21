#ifndef PS_XGPIOPS_COMPAT_H
#define PS_XGPIOPS_COMPAT_H

#if defined(__has_include)
#if __has_include("xgpiops.h")
#include "xgpiops.h"
#define PS_XGPIOPS_COMPAT_AVAILABLE 1
#else
#define PS_XGPIOPS_COMPAT_AVAILABLE 0
typedef struct XGpioPs {
    unsigned int _placeholder;
} XGpioPs;
#endif
#else
#include "xgpiops.h"
#define PS_XGPIOPS_COMPAT_AVAILABLE 1
#endif

#endif
