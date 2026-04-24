#include "Ui/Assets/Icons/ps_app_ui_icons.h"

static const u16 s_icon_cart_rows[14] = {
    0x0FF0U, 0x1008U, 0x17E8U, 0x1428U, 0x17E8U, 0x1008U, 0x3FFCU,
    0x4002U, 0x5FFAU, 0x524AU, 0x524AU, 0x5FFAU, 0x4002U, 0x7FFEU,
};

static const u16 s_icon_sd_rows[14] = {
    0x1FF0U, 0x2010U, 0x2810U, 0x2810U, 0x2010U, 0x2FF0U, 0x2800U,
    0x2FF0U, 0x2008U, 0x2AA8U, 0x2AA8U, 0x2FF8U, 0x2008U, 0x3FF8U,
};

static const u16 s_icon_clock_rows[14] = {
    0x0FF0U, 0x300CU, 0x4002U, 0x47E2U, 0x4422U, 0x4422U, 0x47E2U,
    0x4022U, 0x4022U, 0x4002U, 0x4002U, 0x300CU, 0x0FF0U, 0x0000U,
};

static const u16 s_icon_warn_rows[14] = {
    0x0180U, 0x0240U, 0x0240U, 0x0420U, 0x05A0U, 0x0810U, 0x0990U,
    0x1008U, 0x1188U, 0x2004U, 0x2004U, 0x2184U, 0x4002U, 0x7FFEU,
};

static const u16 s_icon_info_rows[14] = {
    0x0FF0U, 0x300CU, 0x4002U, 0x43C2U, 0x43C2U, 0x4002U, 0x4182U,
    0x4182U, 0x4182U, 0x4182U, 0x43C2U, 0x300CU, 0x0FF0U, 0x0000U,
};

static const u16 s_icon_play_rows[14] = {
    0x1000U, 0x1800U, 0x1C00U, 0x1E00U, 0x1F00U, 0x1F80U, 0x1FC0U,
    0x1FC0U, 0x1F80U, 0x1F00U, 0x1E00U, 0x1C00U, 0x1800U, 0x1000U,
};

static const u64 s_wordmark_pixel_gba_rows[7] = {
    0x1E7D17D00F78EULL,
    0x1110A41010451ULL,
    0x1110441010451ULL,
    0x1E1047901379FULL,
    0x1010441011451ULL,
    0x1010A41011451ULL,
    0x107D17DF0E791ULL,
};

const PsAppUiBitmapIcon ps_app_ui_icon_cart = {15U, 14U, s_icon_cart_rows};
const PsAppUiBitmapIcon ps_app_ui_icon_sd = {15U, 14U, s_icon_sd_rows};
const PsAppUiBitmapIcon ps_app_ui_icon_clock = {15U, 14U, s_icon_clock_rows};
const PsAppUiBitmapIcon ps_app_ui_icon_warn = {15U, 14U, s_icon_warn_rows};
const PsAppUiBitmapIcon ps_app_ui_icon_info = {15U, 14U, s_icon_info_rows};
const PsAppUiBitmapIcon ps_app_ui_icon_play = {15U, 14U, s_icon_play_rows};
const PsAppUiMonoBitmap ps_app_ui_wordmark_pixel_gba = {49U, 7U, s_wordmark_pixel_gba_rows};
