#ifndef PS_APP_UI_THEME_H
#define PS_APP_UI_THEME_H

#include "lvgl.h"
#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS_APP_UI_COLOR_BG             0x101614U
#define PS_APP_UI_COLOR_BG_STRIPE      0x17201DU
#define PS_APP_UI_COLOR_SHELL          0xE6E0C8U
#define PS_APP_UI_COLOR_SHELL_DARK     0x292B25U
#define PS_APP_UI_COLOR_PANEL          0xF4EED7U
#define PS_APP_UI_COLOR_PANEL_DIM      0xD8D0B6U
#define PS_APP_UI_COLOR_PANEL_SHADOW   0x050807U
#define PS_APP_UI_COLOR_INK            0x1A211CU
#define PS_APP_UI_COLOR_INK_MUTED      0x526055U
#define PS_APP_UI_COLOR_DISPLAY        0x14221BU
#define PS_APP_UI_COLOR_DISPLAY_ALT    0x1A2C22U
#define PS_APP_UI_COLOR_GRID           0x244032U
#define PS_APP_UI_COLOR_GREEN          0x70D95EU
#define PS_APP_UI_COLOR_GREEN_DARK     0x347A36U
#define PS_APP_UI_COLOR_GREEN_SOFT     0xB9F59EU
#define PS_APP_UI_COLOR_AMBER          0xF0B84DU
#define PS_APP_UI_COLOR_RED            0xE05A4FU
#define PS_APP_UI_COLOR_TEXT_ON_DARK   0xF2F7E8U
#define PS_APP_UI_COLOR_TEXT_MUTED     0xA8BCA9U
#define PS_APP_UI_COLOR_LIST_ROW       0x1B2B22U
#define PS_APP_UI_COLOR_LIST_BORDER    0x314D3AU
#define PS_APP_UI_COLOR_LIST_SELECTED  0x315638U

#define PS_APP_UI_SPACE_1 4
#define PS_APP_UI_SPACE_2 8
#define PS_APP_UI_SPACE_3 12
#define PS_APP_UI_SPACE_4 16

lv_color_t PsAppUiTheme_Color(u32 rgb);
const lv_font_t *PsAppUiTheme_FontBody(void);
const lv_font_t *PsAppUiTheme_FontTitle(void);
void PsAppUiTheme_SetText(lv_obj_t *obj, u32 color_rgb);
void PsAppUiTheme_SetTextMuted(lv_obj_t *obj);
void PsAppUiTheme_SetTextAccent(lv_obj_t *obj);
void PsAppUiTheme_SetTextWarning(lv_obj_t *obj);
void PsAppUiTheme_SetTextOnDark(lv_obj_t *obj);
void PsAppUiTheme_SetBox(lv_obj_t *obj, u32 bg_rgb, u32 border_rgb, lv_coord_t border_width);
void PsAppUiTheme_SetPanel(lv_obj_t *obj);
void PsAppUiTheme_SetInset(lv_obj_t *obj);
void PsAppUiTheme_SetStatusCapsule(lv_obj_t *obj, u32 bg_rgb);

#ifdef __cplusplus
}
#endif

#endif
