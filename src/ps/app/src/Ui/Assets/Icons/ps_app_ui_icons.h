#ifndef PS_APP_UI_ICONS_H
#define PS_APP_UI_ICONS_H

#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u8 width;
    u8 height;
    const u16 *rows;
} PsAppUiBitmapIcon;

typedef struct {
    u8 width;
    u8 height;
    const u64 *rows;
} PsAppUiMonoBitmap;

extern const PsAppUiBitmapIcon ps_app_ui_icon_cart;
extern const PsAppUiBitmapIcon ps_app_ui_icon_sd;
extern const PsAppUiBitmapIcon ps_app_ui_icon_clock;
extern const PsAppUiBitmapIcon ps_app_ui_icon_warn;
extern const PsAppUiBitmapIcon ps_app_ui_icon_info;
extern const PsAppUiBitmapIcon ps_app_ui_icon_play;
extern const PsAppUiMonoBitmap ps_app_ui_wordmark_pixel_gba;

#ifdef __cplusplus
}
#endif

#endif
