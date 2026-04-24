#include "Ui/Inc/ps_app_ui_theme.h"

#include "Ui/Assets/Fonts/ps_ui_font_fusion_12.h"

lv_color_t PsAppUiTheme_Color(u32 rgb) {
    return lv_color_hex(rgb);
}

const lv_font_t *PsAppUiTheme_FontBody(void) {
    return &ps_ui_font_fusion_12;
}

const lv_font_t *PsAppUiTheme_FontTitle(void) {
    return &ps_ui_font_fusion_12;
}

void PsAppUiTheme_SetText(lv_obj_t *obj, u32 color_rgb) {
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_text_font(obj, PsAppUiTheme_FontBody(), 0);
    lv_obj_set_style_text_color(obj, PsAppUiTheme_Color(color_rgb), 0);
}

void PsAppUiTheme_SetTextMuted(lv_obj_t *obj) {
    PsAppUiTheme_SetText(obj, PS_APP_UI_COLOR_INK_MUTED);
}

void PsAppUiTheme_SetTextAccent(lv_obj_t *obj) {
    PsAppUiTheme_SetText(obj, PS_APP_UI_COLOR_GREEN_SOFT);
}

void PsAppUiTheme_SetTextWarning(lv_obj_t *obj) {
    PsAppUiTheme_SetText(obj, PS_APP_UI_COLOR_AMBER);
}

void PsAppUiTheme_SetTextOnDark(lv_obj_t *obj) {
    PsAppUiTheme_SetText(obj, PS_APP_UI_COLOR_TEXT_ON_DARK);
}

void PsAppUiTheme_SetBox(lv_obj_t *obj, u32 bg_rgb, u32 border_rgb, lv_coord_t border_width) {
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, PsAppUiTheme_Color(bg_rgb), 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, PsAppUiTheme_Color(border_rgb), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void PsAppUiTheme_SetPanel(lv_obj_t *obj) {
    PsAppUiTheme_SetBox(obj,
                        PS_APP_UI_COLOR_PANEL,
                        PS_APP_UI_COLOR_SHELL_DARK,
                        2);
}

void PsAppUiTheme_SetInset(lv_obj_t *obj) {
    PsAppUiTheme_SetBox(obj,
                        PS_APP_UI_COLOR_DISPLAY,
                        PS_APP_UI_COLOR_GRID,
                        2);
}

void PsAppUiTheme_SetStatusCapsule(lv_obj_t *obj, u32 bg_rgb) {
    PsAppUiTheme_SetBox(obj,
                        bg_rgb,
                        PS_APP_UI_COLOR_SHELL_DARK,
                        2);
}
