#include "Ui/Inc/ps_app_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xil_cache.h"
#include "xil_printf.h"

#include "App/Inc/ps_app_runtime.h"
#include "Common/Inc/ps_project_config.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Input/Inc/ps_app_input.h"
#include "Input/Inc/ps_xinput_xbox360.h"
#include "Storage/Inc/ps_fatfs_storage.h"
#include "Ui/Assets/Icons/ps_app_ui_icons.h"
#include "Ui/Inc/ps_app_ui_theme.h"
#include "Video/Inc/ps_app_video.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "lvgl.h"

#define PS_APP_UI_DRAW_BUFFER_LINES 24U
#define PS_APP_UI_TARGET_FRAME_IDX  0U

#define PS_APP_UI_PANEL_SHADOW_OFFSET 2
#define PS_APP_UI_BLINK_INTERVAL_MS   520U
#define PS_APP_UI_SWEEP_PERIOD_MS     720U
#define PS_APP_UI_SWEEP_WIDTH_PX      20U
#define PS_APP_UI_CURSOR_WIDTH_PX     6U
#define PS_APP_UI_DETAIL_PATH_WIDTH   112
#define PS_APP_UI_DETAIL_FIELD_X      14
#define PS_APP_UI_DETAIL_VALUE_X      62
#define PS_APP_UI_DETAIL_ROW_PATH_Y   100
#define PS_APP_UI_DETAIL_ROW_SIZE_Y   124
#define PS_APP_UI_DETAIL_ROW_CODE_Y   148
#define PS_APP_UI_DETAIL_ROW_CRC_Y    178
#define PS_APP_UI_DETAIL_ROW_BIOS_Y   198
#define PS_APP_UI_DETAIL_ROW_SAVE_Y   220
#define PS_APP_UI_GBA_HEADER_PREVIEW_BYTES 0xC0U
#define PS_APP_UI_BJ_OFFSET_SEC       28800LL
#define PS_APP_UI_CLOCK_INVALID_UNIX  0xFFFFFFFFU
#define PS_APP_UI_EMPTY_LIST_TEXT_MAX 160U
/* STATUS0 低 10 位的物理按键定义与 GBA key bit 一致：
 * bit0=A, bit1=B, bit2=SELECT, bit3=START, bit4=RIGHT, bit5=LEFT, bit6=UP, bit7=DOWN, bit8=R, bit9=L */
#define PS_APP_UI_PHY_KEY_A      (1U << 0U)
#define PS_APP_UI_PHY_KEY_B      (1U << 1U)
#define PS_APP_UI_PHY_KEY_SELECT (1U << 2U)
#define PS_APP_UI_PHY_KEY_START  (1U << 3U)
#define PS_APP_UI_PHY_KEY_RIGHT  (1U << 4U)
#define PS_APP_UI_PHY_KEY_LEFT   (1U << 5U)
#define PS_APP_UI_PHY_KEY_UP     (1U << 6U)
#define PS_APP_UI_PHY_KEY_DOWN   (1U << 7U)

typedef struct {
    char name[PS_APP_UI_GAME_NAME_MAX_CHARS];
    char path[PS_APP_ROM_PATH_MAX_CHARS];
    u32 size_bytes;
    char game_code[5];
    char maker_code[3];
    u8 header_valid;
} PsAppUiGameEntry;

typedef enum {
    PS_APP_UI_STATUS_ROLE_INFO = 0U,
    PS_APP_UI_STATUS_ROLE_WARN = 1U,
    PS_APP_UI_STATUS_ROLE_PLAY = 2U
} PsAppUiStatusRole;

typedef struct {
    PsAppUiContext *ctx;
    PsAppUiGameEntry games[PS_APP_UI_MAX_GAMES];
    lv_obj_t *game_buttons[PS_APP_UI_MAX_GAMES];
    lv_obj_t *game_rails[PS_APP_UI_MAX_GAMES];
    lv_display_t *display;
    lv_obj_t *screen;
    lv_obj_t *shell_panel;
    lv_obj_t *header_panel;
    lv_obj_t *title_label;
    lv_obj_t *meta_label;
    lv_obj_t *clock_label;
    lv_obj_t *led;
    lv_obj_t *list_panel;
    lv_obj_t *list;
    lv_obj_t *detail_panel;
    lv_obj_t *detail_title_label;
    lv_obj_t *detail_path_label;
    lv_obj_t *detail_slot_label;
    lv_obj_t *detail_size_value_label;
    lv_obj_t *detail_code_value_label;
    lv_obj_t *detail_crc_value_label;
    lv_obj_t *detail_bios_value_label;
    lv_obj_t *detail_save_value_label;
    lv_obj_t *detail_hint_label;
    lv_obj_t *status_panel;
    lv_obj_t *status_icon_info;
    lv_obj_t *status_icon_warn;
    lv_obj_t *status_icon_play;
    lv_obj_t *status_label;
    lv_obj_t *hint_label;
    lv_obj_t *status_progress;
    lv_obj_t *status_progress_fill;
    lv_obj_t *about_panel;
    lv_obj_t *about_title_label;
    lv_obj_t *about_body_label;
    lv_obj_t *about_hint_label;
    lv_obj_t *sweep;
    lv_obj_t *cursor;
    u32 anim_elapsed_ms;
    u32 blink_elapsed_ms;
    u32 last_clock_unix;
    u16 game_button_count;
    FRESULT last_scan_fs_result;
    char empty_list_text[PS_APP_UI_EMPTY_LIST_TEXT_MAX];
    u8 blink_phase;
    u8 about_visible;
    u8 initialized;
    u8 last_scan_failed;
} PsAppUiRuntime;

typedef struct {
    PsAppUiGameEntry *games;
    u32 count;
    u8 overflow;
    u8 skipped_long_path;
} PsAppUiScanContext;

static PsAppUiRuntime s_ps_app_ui_runtime;
static u32 s_ps_app_ui_draw_buffer[PS_APP_HDMI_WIDTH * PS_APP_UI_DRAW_BUFFER_LINES];

static lv_color_t PsAppUi_HexColor(u32 rgb) {
    return PsAppUiTheme_Color(rgb);
}

static const lv_font_t *PsAppUi_Font(void) {
    return PsAppUiTheme_FontBody();
}

static void PsAppUi_SetTextStyle(lv_obj_t *obj, u32 color_rgb) {
    if (obj == NULL) {
        return;
    }

    PsAppUiTheme_SetText(obj, color_rgb);
}

static void PsAppUi_SetBoxStyle(lv_obj_t *obj,
                                u32 bg_rgb,
                                u32 border_rgb,
                                lv_coord_t border_width) {
    if (obj == NULL) {
        return;
    }

    PsAppUiTheme_SetBox(obj, bg_rgb, border_rgb, border_width);
}

static lv_obj_t *PsAppUi_CreateText(lv_obj_t *parent,
                                    const char *text,
                                    u32 color_rgb,
                                    lv_coord_t x,
                                    lv_coord_t y) {
    lv_obj_t *label;

    label = lv_label_create(parent);
    lv_label_set_text(label, (text != NULL) ? text : "");
    PsAppUi_SetTextStyle(label, color_rgb);
    lv_obj_set_pos(label, x, y);
    return label;
}

static void PsAppUi_CreateBitmapIcon(lv_obj_t *parent,
                                     lv_coord_t x,
                                     lv_coord_t y,
                                     const PsAppUiBitmapIcon *icon,
                                     lv_coord_t scale,
                                     u32 color_rgb) {
    u8 row;

    if ((parent == NULL) || (icon == NULL) || (icon->rows == NULL) || (scale <= 0)) {
        return;
    }

    for (row = 0U; row < icon->height; ++row) {
        u8 col;
        u8 run_start;
        u8 run_len;
        u16 bits;

        bits = icon->rows[row];
        run_start = 0U;
        run_len = 0U;
        for (col = 0U; col < icon->width; ++col) {
            u8 bit_on;

            bit_on = (u8)(((bits >> (15U - col)) & 1U) != 0U);
            if (bit_on != 0U) {
                if (run_len == 0U) {
                    run_start = col;
                }
                run_len++;
            }

            if (((bit_on == 0U) || (col == (u8)(icon->width - 1U))) && (run_len > 0U)) {
                lv_obj_t *px;

                px = lv_obj_create(parent);
                lv_obj_remove_style_all(px);
                lv_obj_set_size(px, (lv_coord_t)run_len * scale, scale);
                lv_obj_set_pos(px, x + ((lv_coord_t)run_start * scale), y + ((lv_coord_t)row * scale));
                PsAppUi_SetBoxStyle(px, color_rgb, color_rgb, 0);
                lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
                run_len = 0U;
            }
        }
    }
}

static void PsAppUi_CreateMonoBitmap(lv_obj_t *parent,
                                     lv_coord_t x,
                                     lv_coord_t y,
                                     const PsAppUiMonoBitmap *bitmap,
                                     lv_coord_t scale,
                                     u32 color_rgb) {
    u8 row;

    if ((parent == NULL) || (bitmap == NULL) || (bitmap->rows == NULL) ||
        (scale <= 0) || (bitmap->width == 0U) || (bitmap->width > 64U)) {
        return;
    }

    for (row = 0U; row < bitmap->height; ++row) {
        u8 col;
        u8 run_start;
        u8 run_len;
        u64 bits;

        bits = bitmap->rows[row];
        run_start = 0U;
        run_len = 0U;
        for (col = 0U; col < bitmap->width; ++col) {
            u8 bit_on;

            bit_on = (u8)(((bits >> (u8)(bitmap->width - 1U - col)) & 1ULL) != 0ULL);
            if (bit_on != 0U) {
                if (run_len == 0U) {
                    run_start = col;
                }
                run_len++;
            }

            if (((bit_on == 0U) || (col == (u8)(bitmap->width - 1U))) && (run_len > 0U)) {
                lv_obj_t *px;

                px = lv_obj_create(parent);
                lv_obj_remove_style_all(px);
                lv_obj_set_size(px, (lv_coord_t)run_len * scale, scale);
                lv_obj_set_pos(px, x + ((lv_coord_t)run_start * scale), y + ((lv_coord_t)row * scale));
                PsAppUi_SetBoxStyle(px, color_rgb, color_rgb, 0);
                lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
                run_len = 0U;
            }
        }
    }
}

static lv_obj_t *PsAppUi_CreateIconLayer(lv_obj_t *parent,
                                         lv_coord_t x,
                                         lv_coord_t y,
                                         const PsAppUiBitmapIcon *icon,
                                         u32 color_rgb) {
    lv_obj_t *layer;

    layer = lv_obj_create(parent);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, 16, 16);
    lv_obj_set_pos(layer, x, y);
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    PsAppUi_CreateBitmapIcon(layer, 0, 1, icon, 1, color_rgb);
    return layer;
}

static size_t PsAppUi_Utf8CharLen(unsigned char lead) {
    if ((lead & 0x80U) == 0U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 1U;
}

static u8 PsAppUi_IsUtf8Continuation(unsigned char byte) {
    return (u8)((byte & 0xC0U) == 0x80U);
}

static size_t PsAppUi_Utf8SafePrefixLen(const char *src, size_t src_len, size_t max_len) {
    size_t pos;
    size_t limit;

    if (src == NULL) {
        return 0U;
    }

    limit = (src_len < max_len) ? src_len : max_len;
    pos = 0U;
    while (pos < limit) {
        size_t char_len;
        size_t idx;
        u8 valid;

        char_len = PsAppUi_Utf8CharLen((unsigned char)src[pos]);
        if ((pos + char_len) > limit) {
            break;
        }

        valid = 1U;
        for (idx = 1U; idx < char_len; ++idx) {
            if (PsAppUi_IsUtf8Continuation((unsigned char)src[pos + idx]) == 0U) {
                valid = 0U;
                break;
            }
        }
        pos += (valid != 0U) ? char_len : 1U;
    }

    return pos;
}

static void PsAppUi_CopyUtf8TextBounded(char *dst,
                                        size_t dst_size,
                                        const char *src,
                                        size_t src_len) {
    size_t copy_len;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    copy_len = PsAppUi_Utf8SafePrefixLen(src, src_len, dst_size - 1U);
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static void PsAppUi_CopyText(char *dst, size_t dst_size, const char *src) {
    PsAppUi_CopyUtf8TextBounded(dst, dst_size, src, (src != NULL) ? strlen(src) : 0U);
}

static const char *PsAppUi_StorageFailureText(FRESULT fs_result) {
    switch (fs_result) {
        case FR_NOT_READY:
            return "SD卡未插入或未就绪";
        case FR_NO_FILESYSTEM:
            return "SD卡未格式化或文件系统不支持";
        case FR_NO_PATH:
            return "缺少 0:/games 目录";
        case FR_DISK_ERR:
            return "SD卡读取错误";
        case FR_TIMEOUT:
        case FR_LOCKED:
            return "存储忙，请稍后重试";
        case FR_INVALID_DRIVE:
        case FR_NOT_ENABLED:
            return "SD卡驱动未就绪";
        case FR_INVALID_NAME:
            return "路径名无效";
        case FR_DENIED:
            return "SD卡访问被拒绝";
        case FR_OK:
            return "读取列表失败";
        default:
            return "存储异常";
    }
}

static void PsAppUi_SetEmptyListText(const char *text) {
    PsAppUi_CopyText(s_ps_app_ui_runtime.empty_list_text,
                     sizeof(s_ps_app_ui_runtime.empty_list_text),
                     text);
}

static void PsAppUi_SetScanDiagnostics(u8 failed, FRESULT fs_result) {
    s_ps_app_ui_runtime.last_scan_failed = failed;
    s_ps_app_ui_runtime.last_scan_fs_result = fs_result;
}

static u8 PsAppUi_StringFits(const char *text, size_t dst_size) {
    if ((text == NULL) || (dst_size == 0U)) {
        return 0U;
    }

    return (u8)(strlen(text) < dst_size);
}

static const char *PsAppUi_BaseName(const char *path) {
    const char *scan;
    const char *last_sep;

    if (path == NULL) {
        return "";
    }

    last_sep = path;
    scan = path;
    while (*scan != '\0') {
        if ((*scan == '/') || (*scan == '\\')) {
            last_sep = scan + 1;
        }
        scan++;
    }

    return last_sep;
}

static int PsAppUi_StrCaseCmp(const char *lhs, const char *rhs) {
    unsigned char lc;
    unsigned char rc;

    if (lhs == NULL) {
        lhs = "";
    }
    if (rhs == NULL) {
        rhs = "";
    }

    while ((*lhs != '\0') && (*rhs != '\0')) {
        lc = (unsigned char)tolower((unsigned char)*lhs);
        rc = (unsigned char)tolower((unsigned char)*rhs);
        if (lc != rc) {
            return (lc < rc) ? -1 : 1;
        }
        lhs++;
        rhs++;
    }

    if (*lhs == *rhs) {
        return 0;
    }

    return (*lhs == '\0') ? -1 : 1;
}

static u8 PsAppUi_HasGbaExtension(const char *name) {
    const char *dot;
    size_t ext_len;

    if (name == NULL) {
        return 0U;
    }

    dot = strrchr(name, '.');
    if (dot == NULL) {
        return 0U;
    }

    ext_len = strlen(dot);
    if (ext_len != 4U) {
        return 0U;
    }

    if ((tolower((unsigned char)dot[1]) == 'g') &&
        (tolower((unsigned char)dot[2]) == 'b') &&
        (tolower((unsigned char)dot[3]) == 'a') &&
        (dot[4] == '\0')) {
        return 1U;
    }

    return 0U;
}

static u8 PsAppUi_HeaderCodeIsPrintable(const char *text, size_t len) {
    size_t idx;

    if (text == NULL) {
        return 0U;
    }

    for (idx = 0U; idx < len; ++idx) {
        unsigned char ch;

        ch = (unsigned char)text[idx];
        if ((ch < 0x20U) || (ch > 0x7EU)) {
            return 0U;
        }
    }

    return 1U;
}

static void PsAppUi_ReadGameHeaderPreview(PsAppUiGameEntry *entry) {
    PsFatFsStorageReadResult read_result;
    u8 header[PS_APP_UI_GBA_HEADER_PREVIEW_BYTES];

    if ((entry == NULL) || (entry->path[0] == '\0')) {
        return;
    }

    memset(header, 0, sizeof(header));
    memset(&read_result, 0, sizeof(read_result));
    if ((PsFatFsStorage_ReadFilePrefix(entry->path,
                                       (UINTPTR)header,
                                       sizeof(header),
                                       &read_result) != XST_SUCCESS) ||
        (read_result.bytes_loaded < 0xB2U)) {
        entry->header_valid = 0U;
        entry->game_code[0] = '\0';
        entry->maker_code[0] = '\0';
        return;
    }

    memcpy(entry->game_code, &header[0xACU], 4U);
    entry->game_code[4] = '\0';
    memcpy(entry->maker_code, &header[0xB0U], 2U);
    entry->maker_code[2] = '\0';
    entry->header_valid =
        (u8)((PsAppUi_HeaderCodeIsPrintable(entry->game_code, 4U) != 0U) &&
             (PsAppUi_HeaderCodeIsPrintable(entry->maker_code, 2U) != 0U));
}

static void PsAppUi_FormatDisplayName(char *dst, size_t dst_size, const char *full_path) {
    const char *base;
    size_t len;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    base = PsAppUi_BaseName(full_path);
    len = strlen(base);
    if ((len > 4U) && (PsAppUi_HasGbaExtension(base) != 0U)) {
        len -= 4U;
    }

    PsAppUi_CopyUtf8TextBounded(dst, dst_size, base, len);
}

static int PsAppUi_GameEntryCompare(const void *lhs, const void *rhs) {
    const PsAppUiGameEntry *left;
    const PsAppUiGameEntry *right;
    int name_cmp;

    left = (const PsAppUiGameEntry *)lhs;
    right = (const PsAppUiGameEntry *)rhs;

    name_cmp = PsAppUi_StrCaseCmp(left->name, right->name);
    if (name_cmp != 0) {
        return name_cmp;
    }

    return PsAppUi_StrCaseCmp(left->path, right->path);
}

static u32 PsAppUi_StatusColor(const char *status_text) {
    if (status_text == NULL) {
        return PS_APP_UI_COLOR_INK;
    }

    if ((strstr(status_text, "失败") != NULL) ||
        (strstr(status_text, "错误") != NULL) ||
        (strstr(status_text, "异常") != NULL) ||
        (strstr(status_text, "未插入") != NULL)) {
        return PS_APP_UI_COLOR_RED;
    }
    if ((strstr(status_text, "扫描") != NULL) ||
        (strstr(status_text, "刷新") != NULL) ||
        (strstr(status_text, "加载") != NULL) ||
        (strstr(status_text, "未找到") != NULL) ||
        (strstr(status_text, "返回") != NULL)) {
        return PS_APP_UI_COLOR_AMBER;
    }

    return PS_APP_UI_COLOR_INK;
}

static PsAppUiStatusRole PsAppUi_StatusRoleFromText(const char *status_text) {
    if (status_text == NULL) {
        return PS_APP_UI_STATUS_ROLE_INFO;
    }

    if ((strstr(status_text, "失败") != NULL) ||
        (strstr(status_text, "错误") != NULL) ||
        (strstr(status_text, "异常") != NULL) ||
        (strstr(status_text, "未插入") != NULL) ||
        (strstr(status_text, "未找到") != NULL)) {
        return PS_APP_UI_STATUS_ROLE_WARN;
    }
    if ((strstr(status_text, "运行") != NULL) ||
        (strstr(status_text, "加载") != NULL)) {
        return PS_APP_UI_STATUS_ROLE_PLAY;
    }

    return PS_APP_UI_STATUS_ROLE_INFO;
}

static void PsAppUi_ShowStatusIcon(PsAppUiStatusRole role) {
    if (s_ps_app_ui_runtime.status_icon_info != NULL) {
        if (role == PS_APP_UI_STATUS_ROLE_INFO) {
            lv_obj_clear_flag(s_ps_app_ui_runtime.status_icon_info, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ps_app_ui_runtime.status_icon_info, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ps_app_ui_runtime.status_icon_warn != NULL) {
        if (role == PS_APP_UI_STATUS_ROLE_WARN) {
            lv_obj_clear_flag(s_ps_app_ui_runtime.status_icon_warn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ps_app_ui_runtime.status_icon_warn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ps_app_ui_runtime.status_icon_play != NULL) {
        if (role == PS_APP_UI_STATUS_ROLE_PLAY) {
            lv_obj_clear_flag(s_ps_app_ui_runtime.status_icon_play, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ps_app_ui_runtime.status_icon_play, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void PsAppUi_SetStatus(PsAppUiContext *ctx, const char *status_text) {
    PsAppUiStatusRole role;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    PsAppUi_CopyText(ctx->state->status_line, sizeof(ctx->state->status_line), status_text);
    role = PsAppUi_StatusRoleFromText(ctx->state->status_line);
    PsAppUi_ShowStatusIcon(role);
    if (s_ps_app_ui_runtime.status_label != NULL) {
        lv_obj_set_style_text_color(s_ps_app_ui_runtime.status_label,
                                    PsAppUi_HexColor(PsAppUi_StatusColor(ctx->state->status_line)),
                                    0);
        lv_label_set_text(s_ps_app_ui_runtime.status_label, ctx->state->status_line);
    }
}

static void PsAppUi_UpdateMetaLabels(PsAppUiContext *ctx) {
    char meta_text[96];

    if ((ctx == NULL) || (ctx->state == NULL) || (s_ps_app_ui_runtime.meta_label == NULL)) {
        return;
    }

    (void)snprintf(meta_text,
                   sizeof(meta_text),
                   "%u GAMES",
                   (unsigned int)ctx->state->game_count);
    lv_label_set_text(s_ps_app_ui_runtime.meta_label, meta_text);
}

static void PsAppUi_UnixToDateTime(s64 unix_sec,
                                   s64 tz_offset_sec,
                                   s32 *year_out,
                                   u8 *month_out,
                                   u8 *day_out,
                                   u8 *hour_out,
                                   u8 *min_out,
                                   u8 *sec_out) {
    s64 shifted_sec;
    s64 days;
    s64 rem;
    s64 z;
    s64 era;
    u32 doe;
    u32 yoe;
    s64 y;
    u32 doy;
    u32 mp;
    u32 day;
    s32 month;

    if ((year_out == NULL) || (month_out == NULL) || (day_out == NULL) ||
        (hour_out == NULL) || (min_out == NULL) || (sec_out == NULL)) {
        return;
    }

    shifted_sec = unix_sec + tz_offset_sec;
    days = shifted_sec / 86400LL;
    rem = shifted_sec % 86400LL;
    if (rem < 0LL) {
        rem += 86400LL;
        days -= 1LL;
    }

    z = days + 719468LL;
    era = (z >= 0LL) ? (z / 146097LL) : ((z - 146096LL) / 146097LL);
    doe = (u32)(z - era * 146097LL);
    yoe = (doe - (doe / 1460U) + (doe / 36524U) - (doe / 146096U)) / 365U;
    y = (s64)yoe + era * 400LL;
    doy = doe - (365U * yoe + (yoe / 4U) - (yoe / 100U));
    mp = (5U * doy + 2U) / 153U;
    day = doy - (153U * mp + 2U) / 5U + 1U;
    month = (s32)mp + (((s32)mp < 10) ? 3 : -9);
    if (month <= 2) {
        y += 1LL;
    }

    *year_out = (s32)y;
    *month_out = (u8)month;
    *day_out = (u8)day;
    *hour_out = (u8)(rem / 3600LL);
    *min_out = (u8)((rem % 3600LL) / 60LL);
    *sec_out = (u8)(rem % 60LL);
}

static void PsAppUi_UpdateClockLabel(PsAppUiContext *ctx) {
    u32 unix_sec;
    s32 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
    char clock_text[48];

    if (s_ps_app_ui_runtime.clock_label == NULL) {
        return;
    }
    if ((ctx == NULL) || (ctx->runtime == NULL) || (ctx->runtime->config == NULL) ||
        (ctx->runtime->rtc == NULL) || (ctx->runtime->rtc->model_ready == 0U)) {
        if (s_ps_app_ui_runtime.last_clock_unix != PS_APP_UI_CLOCK_INVALID_UNIX) {
            lv_label_set_text(s_ps_app_ui_runtime.clock_label, "BJ ----/--/-- --:--:--");
            s_ps_app_ui_runtime.last_clock_unix = PS_APP_UI_CLOCK_INVALID_UNIX;
        }
        return;
    }

    unix_sec = ctx->runtime->config->rtc_timestamp;
    if (unix_sec == s_ps_app_ui_runtime.last_clock_unix) {
        return;
    }

    PsAppUi_UnixToDateTime((s64)unix_sec,
                           PS_APP_UI_BJ_OFFSET_SEC,
                           &year,
                           &month,
                           &day,
                           &hour,
                           &minute,
                           &second);
    (void)snprintf(clock_text,
                   sizeof(clock_text),
                   "BJ %04d-%02u-%02u %02u:%02u:%02u",
                   (int)year,
                   (unsigned int)month,
                   (unsigned int)day,
                   (unsigned int)hour,
                   (unsigned int)minute,
                   (unsigned int)second);
    lv_label_set_text(s_ps_app_ui_runtime.clock_label, clock_text);
    s_ps_app_ui_runtime.last_clock_unix = unix_sec;
}

static lv_obj_t *PsAppUi_CreatePixelPanel(lv_obj_t *parent,
                                          lv_coord_t x,
                                          lv_coord_t y,
                                          lv_coord_t w,
                                          lv_coord_t h,
                                          u32 bg_rgb,
                                          u32 border_rgb,
                                          u32 shadow_rgb) {
    lv_obj_t *shadow;
    lv_obj_t *panel;

    shadow = lv_obj_create(parent);
    lv_obj_remove_style_all(shadow);
    lv_obj_set_size(shadow, w, h);
    lv_obj_set_pos(shadow, x + PS_APP_UI_PANEL_SHADOW_OFFSET, y + PS_APP_UI_PANEL_SHADOW_OFFSET);
    PsAppUi_SetBoxStyle(shadow, shadow_rgb, shadow_rgb, 0);
    lv_obj_clear_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);

    panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    PsAppUi_SetBoxStyle(panel, bg_rgb, border_rgb, 2);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    return panel;
}

static lv_obj_t *PsAppUi_CreatePixelBlock(lv_obj_t *parent,
                                          lv_coord_t x,
                                          lv_coord_t y,
                                          lv_coord_t w,
                                          lv_coord_t h,
                                          u32 bg_rgb,
                                          u32 border_rgb) {
    lv_obj_t *block;

    block = lv_obj_create(parent);
    lv_obj_remove_style_all(block);
    lv_obj_set_size(block, w, h);
    lv_obj_set_pos(block, x, y);
    PsAppUi_SetBoxStyle(block, bg_rgb, border_rgb, 2);
    lv_obj_set_scrollbar_mode(block, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    return block;
}

static void PsAppUi_BuildPixelDpad(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
    const lv_coord_t block = 14;

    (void)PsAppUi_CreatePixelBlock(parent, x + block, y, block, block, PS_APP_UI_COLOR_SHELL_DARK, 0x080A08);
    (void)PsAppUi_CreatePixelBlock(parent, x, y + block, block, block, PS_APP_UI_COLOR_SHELL_DARK, 0x080A08);
    (void)PsAppUi_CreatePixelBlock(parent, x + block, y + block, block, block, 0x3A4037, 0x080A08);
    (void)PsAppUi_CreatePixelBlock(parent, x + (block * 2), y + block, block, block, PS_APP_UI_COLOR_SHELL_DARK, 0x080A08);
    (void)PsAppUi_CreatePixelBlock(parent, x + block, y + (block * 2), block, block, PS_APP_UI_COLOR_SHELL_DARK, 0x080A08);
}

static void PsAppUi_BuildPixelActionButtons(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t *btn_a;
    lv_obj_t *btn_b;
    lv_obj_t *label_a;
    lv_obj_t *label_b;

    btn_b = PsAppUi_CreatePixelBlock(parent, x, y + 10, 18, 18, PS_APP_UI_COLOR_SHELL_DARK, 0x080A08);
    btn_a = PsAppUi_CreatePixelBlock(parent, x + 24, y, 18, 18, PS_APP_UI_COLOR_GREEN_DARK, 0x080A08);

    label_b = lv_label_create(btn_b);
    lv_label_set_text(label_b, "B");
    PsAppUi_SetTextStyle(label_b, PS_APP_UI_COLOR_TEXT_ON_DARK);
    lv_obj_center(label_b);

    label_a = lv_label_create(btn_a);
    lv_label_set_text(label_a, "A");
    PsAppUi_SetTextStyle(label_a, PS_APP_UI_COLOR_TEXT_ON_DARK);
    lv_obj_center(label_a);
}

static lv_obj_t *PsAppUi_CreateKeyHint(lv_obj_t *parent,
                                       lv_coord_t x,
                                       lv_coord_t y,
                                       const char *key_text,
                                       const char *action_text,
                                       u32 key_bg_rgb) {
    lv_obj_t *key_box;
    lv_obj_t *key_label;
    lv_obj_t *action_label;

    key_box = PsAppUi_CreatePixelBlock(parent, x, y, 42, 20, key_bg_rgb, PS_APP_UI_COLOR_SHELL_DARK);
    key_label = lv_label_create(key_box);
    lv_label_set_text(key_label, (key_text != NULL) ? key_text : "");
    PsAppUi_SetTextStyle(key_label, PS_APP_UI_COLOR_TEXT_ON_DARK);
    lv_obj_center(key_label);

    action_label = lv_label_create(parent);
    lv_label_set_text(action_label, (action_text != NULL) ? action_text : "");
    PsAppUi_SetTextStyle(action_label, PS_APP_UI_COLOR_INK);
    lv_obj_align_to(action_label, key_box, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    return key_box;
}

static void PsAppUi_ResetSweepObject(void) {
    s_ps_app_ui_runtime.sweep = NULL;
    s_ps_app_ui_runtime.cursor = NULL;
}

static void PsAppUi_ResetGameButtons(void) {
    memset(s_ps_app_ui_runtime.game_buttons, 0, sizeof(s_ps_app_ui_runtime.game_buttons));
    memset(s_ps_app_ui_runtime.game_rails, 0, sizeof(s_ps_app_ui_runtime.game_rails));
    s_ps_app_ui_runtime.game_button_count = 0U;
    PsAppUi_ResetSweepObject();
}

static void PsAppUi_StyleListButton(lv_obj_t *btn) {
    lv_obj_t *label;

    if (btn == NULL) {
        return;
    }

    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 28);
    lv_obj_set_flex_grow(btn, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, PsAppUi_HexColor(PS_APP_UI_COLOR_LIST_BORDER), 0);
    lv_obj_set_style_bg_color(btn, PsAppUi_HexColor(PS_APP_UI_COLOR_LIST_ROW), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn, PsAppUi_HexColor(PS_APP_UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_font(btn, PsAppUi_Font(), 0);
    lv_obj_set_style_pad_left(btn, 14, 0);
    lv_obj_set_style_pad_right(btn, 8, 0);
    lv_obj_set_style_pad_top(btn, 5, 0);
    lv_obj_set_style_pad_bottom(btn, 5, 0);

    label = lv_obj_get_child(btn, 0);
    if (label != NULL) {
        lv_obj_set_width(label, 312);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, PsAppUi_Font(), 0);
    }
}

static void PsAppUi_UpdateListVisuals(PsAppUiContext *ctx, u8 reset_sweep) {
    PsAppUiState *state;
    u32 idx;
    lv_obj_t *selected_btn;

    if ((ctx == NULL) || (ctx->state == NULL) || (s_ps_app_ui_runtime.list == NULL)) {
        return;
    }

    state = ctx->state;
    selected_btn = NULL;

    if (state->game_count == 0U) {
        if (s_ps_app_ui_runtime.sweep != NULL) {
            lv_obj_add_flag(s_ps_app_ui_runtime.sweep, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_ps_app_ui_runtime.cursor != NULL) {
            lv_obj_add_flag(s_ps_app_ui_runtime.cursor, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (state->selected_index >= state->game_count) {
        state->selected_index = (u8)(state->game_count - 1U);
    }

    /* 关键约束：
     * 不能再通过 lv_obj_get_child() 按索引遍历 lv_list 子对象。
     * lv_list 内部会创建滚动条/容器等附加对象，索引并不等于“第 N 个游戏按钮”，
     * 会导致选中态画到错误对象上，出现整块高亮或白条遮挡。 */
    for (idx = 0U; idx < (u32)state->game_count; ++idx) {
        lv_obj_t *btn;

        if (idx >= (u32)s_ps_app_ui_runtime.game_button_count) {
            break;
        }

        btn = s_ps_app_ui_runtime.game_buttons[idx];
        if ((btn == NULL) || (!lv_obj_is_valid(btn))) {
            continue;
        }

        PsAppUi_StyleListButton(btn);
        if ((idx < (u32)PS_APP_UI_MAX_GAMES) &&
            (s_ps_app_ui_runtime.game_rails[idx] != NULL) &&
            lv_obj_is_valid(s_ps_app_ui_runtime.game_rails[idx])) {
            lv_obj_add_flag(s_ps_app_ui_runtime.game_rails[idx], LV_OBJ_FLAG_HIDDEN);
        }
        if (idx == (u32)state->selected_index) {
            selected_btn = btn;
            lv_obj_set_style_bg_color(btn, PsAppUi_HexColor(PS_APP_UI_COLOR_LIST_SELECTED), 0);
            lv_obj_set_style_border_color(btn,
                                          PsAppUi_HexColor((s_ps_app_ui_runtime.blink_phase == 0U) ?
                                                          PS_APP_UI_COLOR_GREEN_SOFT :
                                                          PS_APP_UI_COLOR_GREEN),
                                          0);
            lv_obj_set_style_text_color(btn, PsAppUi_HexColor(PS_APP_UI_COLOR_TEXT_ON_DARK), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_pad_left(btn, 24, 0);
            if ((idx < (u32)PS_APP_UI_MAX_GAMES) &&
                (s_ps_app_ui_runtime.game_rails[idx] != NULL) &&
                lv_obj_is_valid(s_ps_app_ui_runtime.game_rails[idx])) {
                lv_obj_clear_flag(s_ps_app_ui_runtime.game_rails[idx], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (selected_btn == NULL) {
        if (s_ps_app_ui_runtime.sweep != NULL) {
            lv_obj_add_flag(s_ps_app_ui_runtime.sweep, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_ps_app_ui_runtime.cursor != NULL) {
            lv_obj_add_flag(s_ps_app_ui_runtime.cursor, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    /* Keep selected item visible. The selected row itself carries the pixel cursor treatment. */
    (void)reset_sweep;
    lv_obj_scroll_to_view(selected_btn, LV_ANIM_OFF);
    if (s_ps_app_ui_runtime.sweep != NULL) {
        lv_obj_add_flag(s_ps_app_ui_runtime.sweep, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ps_app_ui_runtime.cursor != NULL) {
        lv_obj_add_flag(s_ps_app_ui_runtime.cursor, LV_OBJ_FLAG_HIDDEN);
    }
}

static const char *PsAppUi_SaveKindName(u8 kind) {
    switch ((PsAppSaveKind)kind) {
        case PS_APP_SAVE_KIND_SRAM:
            return "SRAM";
        case PS_APP_SAVE_KIND_FLASH:
            return "FLASH";
        case PS_APP_SAVE_KIND_EEPROM:
            return "EEPROM";
        case PS_APP_SAVE_KIND_NONE:
        default:
            return "NONE";
    }
}

static const char *PsAppUi_BiosModeName(u8 mode) {
    switch ((PsAppBiosMode)mode) {
        case PS_APP_BIOS_MODE_EXTERNAL:
            return "EXT";
        case PS_APP_BIOS_MODE_FALLBACK:
            return "FALLBACK";
        case PS_APP_BIOS_MODE_INTERNAL:
        default:
            return "INT";
    }
}

static void PsAppUi_FormatBytes(char *dst, size_t dst_size, u32 bytes) {
    u32 mib100;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (bytes == 0U) {
        (void)snprintf(dst, dst_size, "--");
        return;
    }

    mib100 = (u32)(((u64)bytes * 100ULL) / (1024ULL * 1024ULL));
    (void)snprintf(dst,
                   dst_size,
                   "%u.%02u MiB",
                   (unsigned int)(mib100 / 100U),
                   (unsigned int)(mib100 % 100U));
}

static u8 PsAppUi_SelectedRomIsLoaded(PsAppUiContext *ctx, const char *selected_path) {
    if ((ctx == NULL) || (ctx->runtime == NULL) || (ctx->runtime->rom == NULL) ||
        (selected_path == NULL) || (selected_path[0] == '\0')) {
        return 0U;
    }

    if ((ctx->runtime->rom->loaded == 0U) || (ctx->runtime->rom->path[0] == '\0')) {
        return 0U;
    }

    return (u8)(strcmp(ctx->runtime->rom->path, selected_path) == 0 ? 1U : 0U);
}

static void PsAppUi_SetDetailPathText(const char *path_text) {
    lv_point_t text_size;
    const char *safe_text;

    if (s_ps_app_ui_runtime.detail_path_label == NULL) {
        return;
    }

    safe_text = (path_text != NULL) ? path_text : "";
    lv_text_get_size(&text_size,
                     safe_text,
                     PsAppUi_Font(),
                     0,
                     0,
                     LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    if (text_size.x > PS_APP_UI_DETAIL_PATH_WIDTH) {
        lv_label_set_long_mode(s_ps_app_ui_runtime.detail_path_label,
                               LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    } else {
        lv_label_set_long_mode(s_ps_app_ui_runtime.detail_path_label,
                               LV_LABEL_LONG_MODE_CLIP);
    }
    lv_obj_set_size(s_ps_app_ui_runtime.detail_path_label,
                    PS_APP_UI_DETAIL_PATH_WIDTH,
                    18);
    lv_label_set_text(s_ps_app_ui_runtime.detail_path_label, safe_text);
}

static lv_obj_t *PsAppUi_CreateDetailFieldLabel(lv_obj_t *parent,
                                                const char *field_text,
                                                lv_coord_t y) {
    lv_obj_t *label;

    label = PsAppUi_CreateText(parent,
                               field_text,
                               PS_APP_UI_COLOR_INK_MUTED,
                               PS_APP_UI_DETAIL_FIELD_X,
                               y);
    lv_obj_set_width(label, PS_APP_UI_DETAIL_VALUE_X - PS_APP_UI_DETAIL_FIELD_X - 4);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    return label;
}

static lv_obj_t *PsAppUi_CreateDetailValueLabel(lv_obj_t *parent,
                                                lv_coord_t y,
                                                u32 color_rgb) {
    lv_obj_t *label;

    label = lv_label_create(parent);
    lv_obj_set_width(label, PS_APP_UI_DETAIL_PATH_WIDTH);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    PsAppUi_SetTextStyle(label, color_rgb);
    lv_obj_set_pos(label, PS_APP_UI_DETAIL_VALUE_X, y);
    return label;
}

static void PsAppUi_UpdateDetailPanel(PsAppUiContext *ctx) {
    PsAppUiState *state;
    char slot_text[48];

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }
    if ((s_ps_app_ui_runtime.detail_title_label == NULL) ||
        (s_ps_app_ui_runtime.detail_path_label == NULL) ||
        (s_ps_app_ui_runtime.detail_slot_label == NULL) ||
        (s_ps_app_ui_runtime.detail_size_value_label == NULL) ||
        (s_ps_app_ui_runtime.detail_code_value_label == NULL) ||
        (s_ps_app_ui_runtime.detail_crc_value_label == NULL) ||
        (s_ps_app_ui_runtime.detail_bios_value_label == NULL) ||
        (s_ps_app_ui_runtime.detail_save_value_label == NULL) ||
        (s_ps_app_ui_runtime.detail_hint_label == NULL)) {
        return;
    }

    state = ctx->state;
    if (state->game_count == 0U) {
        if (s_ps_app_ui_runtime.last_scan_failed != 0U) {
            const char *reason;

            reason = PsAppUi_StorageFailureText(s_ps_app_ui_runtime.last_scan_fs_result);
            lv_label_set_text(s_ps_app_ui_runtime.detail_title_label, "STORAGE ERROR");
            PsAppUi_SetDetailPathText(PS_APP_GAMES_SD_DIR);
            lv_label_set_text(s_ps_app_ui_runtime.detail_slot_label, reason);
            lv_label_set_text(s_ps_app_ui_runtime.detail_size_value_label, "--");
            lv_label_set_text(s_ps_app_ui_runtime.detail_code_value_label, "--");
            lv_label_set_text(s_ps_app_ui_runtime.detail_crc_value_label, "--");
            lv_label_set_text(s_ps_app_ui_runtime.detail_bios_value_label, "READY");
            lv_label_set_text(s_ps_app_ui_runtime.detail_save_value_label, "CHECK SD");
            lv_label_set_text(s_ps_app_ui_runtime.detail_hint_label, "修复SD后按 X/START");
            return;
        }

        lv_label_set_text(s_ps_app_ui_runtime.detail_title_label, "NO CARTRIDGE");
        PsAppUi_SetDetailPathText("0:/games");
        lv_label_set_text(s_ps_app_ui_runtime.detail_slot_label, "EMPTY LIBRARY");
        lv_label_set_text(s_ps_app_ui_runtime.detail_size_value_label, "--");
        lv_label_set_text(s_ps_app_ui_runtime.detail_code_value_label, "--");
        lv_label_set_text(s_ps_app_ui_runtime.detail_crc_value_label, "--");
        lv_label_set_text(s_ps_app_ui_runtime.detail_bios_value_label, "READY");
        lv_label_set_text(s_ps_app_ui_runtime.detail_save_value_label, "WAITING FOR SD");
        lv_label_set_text(s_ps_app_ui_runtime.detail_hint_label, "放入游戏后按 X/START 刷新");
        return;
    }

    if (state->selected_index >= state->game_count) {
        state->selected_index = (u8)(state->game_count - 1U);
    }

    lv_label_set_text(s_ps_app_ui_runtime.detail_title_label,
                      s_ps_app_ui_runtime.games[state->selected_index].name);
    (void)snprintf(slot_text,
                   sizeof(slot_text),
                   "SLOT %03u / %03u",
                   (unsigned int)state->selected_index + 1U,
                   (unsigned int)state->game_count);
    lv_label_set_text(s_ps_app_ui_runtime.detail_slot_label, slot_text);
    PsAppUi_SetDetailPathText(s_ps_app_ui_runtime.games[state->selected_index].path);

    if (PsAppUi_SelectedRomIsLoaded(ctx, s_ps_app_ui_runtime.games[state->selected_index].path) != 0U) {
        char size_text[24];
        u32 crc32;
        const char *game_code;
        const char *maker_code;

        PsAppUi_FormatBytes(size_text, sizeof(size_text), ctx->runtime->rom->size_bytes);
        crc32 = (ctx->runtime->feature != NULL) ? ctx->runtime->feature->rom_crc32 : 0U;
        game_code = (ctx->runtime->rom->game_code[0] != '\0') ? ctx->runtime->rom->game_code : "....";
        maker_code = (ctx->runtime->rom->maker_code[0] != '\0') ? ctx->runtime->rom->maker_code : "..";
        (void)snprintf(slot_text, sizeof(slot_text), "%s / %s", game_code, maker_code);
        lv_label_set_text(s_ps_app_ui_runtime.detail_size_value_label, size_text);
        lv_label_set_text(s_ps_app_ui_runtime.detail_code_value_label, slot_text);
        (void)snprintf(slot_text, sizeof(slot_text), "%08X", (unsigned int)crc32);
        lv_label_set_text(s_ps_app_ui_runtime.detail_crc_value_label, slot_text);
        lv_label_set_text(s_ps_app_ui_runtime.detail_bios_value_label,
                          PsAppUi_BiosModeName(ctx->runtime->rom->bios_mode));
        (void)snprintf(slot_text,
                       sizeof(slot_text),
                       "%s %s",
                       (ctx->runtime->save != NULL) ? PsAppUi_SaveKindName(ctx->runtime->save->active_kind) : "NONE",
                       ((ctx->runtime->save != NULL) && (ctx->runtime->save->dirty != 0U)) ? "DIRTY" : "CLEAN");
        lv_label_set_text(s_ps_app_ui_runtime.detail_save_value_label, slot_text);
        lv_label_set_text(s_ps_app_ui_runtime.detail_hint_label, "A 重新启动此卡带");
    } else {
        PsAppUi_FormatBytes(slot_text,
                            sizeof(slot_text),
                            s_ps_app_ui_runtime.games[state->selected_index].size_bytes);
        lv_label_set_text(s_ps_app_ui_runtime.detail_size_value_label, slot_text);
        if (s_ps_app_ui_runtime.games[state->selected_index].header_valid != 0U) {
            (void)snprintf(slot_text,
                           sizeof(slot_text),
                           "%s / %s",
                           s_ps_app_ui_runtime.games[state->selected_index].game_code,
                           s_ps_app_ui_runtime.games[state->selected_index].maker_code);
            lv_label_set_text(s_ps_app_ui_runtime.detail_code_value_label, slot_text);
        } else {
            lv_label_set_text(s_ps_app_ui_runtime.detail_code_value_label, "HEADER N/A");
        }
        lv_label_set_text(s_ps_app_ui_runtime.detail_crc_value_label, "ON LAUNCH");
        lv_label_set_text(s_ps_app_ui_runtime.detail_bios_value_label, "READY");
        lv_label_set_text(s_ps_app_ui_runtime.detail_save_value_label, "AUTO DETECT");
        lv_label_set_text(s_ps_app_ui_runtime.detail_hint_label, "A 启动此卡带");
    }
}

static void PsAppUi_RebuildList(PsAppUiContext *ctx) {
    PsAppUiState *state;
    u32 idx;

    if ((ctx == NULL) || (ctx->state == NULL) || (s_ps_app_ui_runtime.list == NULL)) {
        return;
    }

    state = ctx->state;
    /* 每次重建列表都先清空容器，再重新登记按钮指针数组。
     * 这样 UpdateListVisuals 只会命中“本次创建”的有效按钮对象。 */
    lv_obj_clean(s_ps_app_ui_runtime.list);
    PsAppUi_ResetGameButtons();

    if (state->game_count == 0U) {
        lv_obj_t *empty_label;

        empty_label = lv_label_create(s_ps_app_ui_runtime.list);
        lv_obj_set_width(empty_label, lv_pct(100));
        lv_label_set_long_mode(empty_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(empty_label,
                          (s_ps_app_ui_runtime.empty_list_text[0] != '\0') ?
                          s_ps_app_ui_runtime.empty_list_text :
                          "没有找到 .gba 游戏\n请将 ROM 放入 0:/games 后刷新");
        PsAppUi_SetTextStyle(empty_label, PS_APP_UI_COLOR_TEXT_MUTED);
        lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty_label, 70, 0);
        lv_obj_set_style_pad_left(empty_label, 12, 0);
        lv_obj_set_style_pad_right(empty_label, 12, 0);
    } else {
        for (idx = 0U; idx < (u32)state->game_count; ++idx) {
            lv_obj_t *btn;
            lv_obj_t *rail;

            btn = lv_list_add_button(s_ps_app_ui_runtime.list, NULL, s_ps_app_ui_runtime.games[idx].name);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
            PsAppUi_StyleListButton(btn);
            rail = lv_obj_create(btn);
            lv_obj_remove_style_all(rail);
            lv_obj_set_size(rail, 4, 18);
            lv_obj_set_pos(rail, 8, 5);
            PsAppUi_SetBoxStyle(rail, PS_APP_UI_COLOR_GREEN_SOFT, PS_APP_UI_COLOR_GREEN_SOFT, 0);
            lv_obj_add_flag(rail, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
            s_ps_app_ui_runtime.game_buttons[idx] = btn;
            s_ps_app_ui_runtime.game_rails[idx] = rail;
            s_ps_app_ui_runtime.game_button_count = (u16)(idx + 1U);
        }
    }

    PsAppUi_UpdateMetaLabels(ctx);
    PsAppUi_UpdateListVisuals(ctx, 1U);
    PsAppUi_UpdateDetailPanel(ctx);
}

static u8 PsAppUi_ListDirectoryCallback(const char *name,
                                        const char *full_path,
                                        u8 is_dir,
                                        u32 size_bytes,
                                        void *user_ctx) {
    PsAppUiScanContext *scan_ctx;
    PsAppUiGameEntry *entry;

    if (is_dir != 0U) {
        return 1U;
    }

    if (PsAppUi_HasGbaExtension(name) == 0U) {
        return 1U;
    }

    scan_ctx = (PsAppUiScanContext *)user_ctx;
    if ((scan_ctx == NULL) || (scan_ctx->games == NULL)) {
        return 0U;
    }

    if (scan_ctx->count >= PS_APP_UI_MAX_GAMES) {
        scan_ctx->overflow = 1U;
        return 0U;
    }

    if ((full_path == NULL) || (PsAppUi_StringFits(full_path, sizeof(scan_ctx->games[0].path)) == 0U)) {
        scan_ctx->skipped_long_path = 1U;
        return 1U;
    }

    entry = &scan_ctx->games[scan_ctx->count];
    memset(entry, 0, sizeof(*entry));
    PsAppUi_FormatDisplayName(entry->name,
                              sizeof(entry->name),
                              (full_path != NULL) ? full_path : name);
    PsAppUi_CopyText(entry->path, sizeof(entry->path), full_path);
    entry->size_bytes = size_bytes;
    scan_ctx->count++;
    return 1U;
}

static XStatus PsAppUi_RefreshGames(PsAppUiContext *ctx) {
    PsAppUiScanContext scan_ctx;
    PsFatFsStorageListResult list_result;
    PsAppUiState *state;
    XStatus status;
    TickType_t retry_ticks;
    u8 scan_try;
    char status_text[PS_APP_UI_GAME_NAME_MAX_CHARS];
    u32 idx;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    scan_ctx.games = s_ps_app_ui_runtime.games;
    scan_ctx.count = 0U;
    scan_ctx.overflow = 0U;
    scan_ctx.skipped_long_path = 0U;
    memset(&list_result, 0, sizeof(list_result));
    PsAppUi_SetScanDiagnostics(0U, FR_OK);
    PsAppUi_SetEmptyListText("正在扫描 0:/games ...");

    retry_ticks = pdMS_TO_TICKS(120U);
    if (retry_ticks == 0U) {
        retry_ticks = 1U;
    }

    /* 扫描固定目录 0:/games，仅收集 .gba 文件。
     * 启动早期若出现“目录瞬态空视图”（visited=0 且 count=0），
     * 进行短延时重扫，覆盖 SD 初始化抖动窗口。 */
    status = XST_FAILURE;
    for (scan_try = 0U; scan_try < 3U; ++scan_try) {
        scan_ctx.count = 0U;
        scan_ctx.overflow = 0U;
        scan_ctx.skipped_long_path = 0U;
        memset(&list_result, 0, sizeof(list_result));

        status = PsFatFsStorage_ListDirectoryEx(PS_APP_GAMES_SD_DIR,
                                                PsAppUi_ListDirectoryCallback,
                                                &scan_ctx,
                                                &list_result);

        if ((status == XST_SUCCESS) &&
            (scan_ctx.count == 0U) &&
            (list_result.entries_visited == 0U) &&
            (scan_try < 2U)) {
            xil_printf("[UI] scan delayed-retry #%u path=%s\r\n",
                       (unsigned int)(scan_try + 1U),
                       PS_APP_GAMES_SD_DIR);
            vTaskDelay(retry_ticks);
            continue;
        }

        break;
    }

    state->game_count = (u8)((scan_ctx.count > 255U) ? 255U : scan_ctx.count);
    for (idx = 0U; idx < scan_ctx.count; ++idx) {
        PsAppUi_ReadGameHeaderPreview(&s_ps_app_ui_runtime.games[idx]);
    }
    /* 菜单展示按名称排序，保证每次刷新顺序稳定。 */
    if (scan_ctx.count > 1U) {
        qsort(s_ps_app_ui_runtime.games,
              scan_ctx.count,
              sizeof(s_ps_app_ui_runtime.games[0]),
              PsAppUi_GameEntryCompare);
    }

    /* 仅打印前 8 条样本，避免串口刷屏影响交互实时性。 */
    xil_printf("[UI] scan status=%d fs=%s count=%u visited=%u skip_long=%u overflow=%u\r\n",
               (int)status,
               PsFatFsStorage_StrError(list_result.fs_result),
               (unsigned int)scan_ctx.count,
               (unsigned int)list_result.entries_visited,
               (unsigned int)list_result.entries_skipped_long,
               (unsigned int)scan_ctx.overflow);
    for (idx = 0U; (idx < scan_ctx.count) && (idx < 8U); ++idx) {
        xil_printf("[UI] game[%u]=%s\r\n",
                   (unsigned int)idx,
                   s_ps_app_ui_runtime.games[idx].name);
    }
    if (scan_ctx.count > 8U) {
        xil_printf("[UI] ... (%u more)\r\n",
                   (unsigned int)(scan_ctx.count - 8U));
    }

    if (state->game_count == 0U) {
        state->selected_index = 0U;
    } else if (state->selected_index >= state->game_count) {
        state->selected_index = (u8)(state->game_count - 1U);
    }

    if (status == XST_SUCCESS) {
        PsAppUi_SetScanDiagnostics(0U, FR_OK);
        if (state->game_count == 0U) {
            PsAppUi_SetEmptyListText("没有找到 .gba 游戏\n请将 ROM 放入 0:/games 后刷新");
            PsAppUi_SetStatus(ctx, "未找到游戏");
        } else if (scan_ctx.skipped_long_path != 0U) {
            PsAppUi_SetEmptyListText("没有可显示的 .gba 游戏\n部分路径过长，已跳过");
            PsAppUi_SetStatus(ctx, "部分路径过长，已跳过");
        } else if (scan_ctx.overflow != 0U) {
            (void)snprintf(status_text,
                           sizeof(status_text),
                           "仅显示前%u项",
                           (unsigned int)PS_APP_UI_MAX_GAMES);
            PsAppUi_SetStatus(ctx, status_text);
        } else {
            PsAppUi_SetEmptyListText("");
            PsAppUi_SetStatus(ctx, "菜单就绪");
        }
    } else {
        const char *reason;

        reason = PsAppUi_StorageFailureText(list_result.fs_result);
        PsAppUi_SetScanDiagnostics(1U, list_result.fs_result);
        /* 扫描失败但已有部分结果时仍允许进入菜单，避免“全失败感知”。 */
        if (state->game_count > 0U) {
            (void)snprintf(status_text,
                           sizeof(status_text),
                           "菜单就绪(部分): %s",
                           reason);
            PsAppUi_SetStatus(ctx, status_text);
        } else {
            (void)snprintf(status_text,
                           sizeof(status_text),
                           "读取列表失败\n%s\n插入SD卡或修复后按 X/START 刷新",
                           reason);
            PsAppUi_SetEmptyListText(status_text);
            (void)snprintf(status_text,
                           sizeof(status_text),
                           "读取列表失败: %s",
                           reason);
            PsAppUi_SetStatus(ctx, status_text);
        }
    }

    state->refresh_requested = 0U;
    PsAppUi_RebuildList(ctx);
    return status;
}

static u8 PsAppUi_ButtonPressedEdge(u16 prev_buttons, u16 cur_buttons, u16 button_mask) {
    if (((cur_buttons & button_mask) != 0U) &&
        ((prev_buttons & button_mask) == 0U)) {
        return 1U;
    }

    return 0U;
}

static u32 PsAppUi_ReadPhysicalKeys(const PsAppRuntimeContext *runtime) {
    if ((runtime == NULL) || (runtime->regs == NULL)) {
        return 0U;
    }

    /* 板载按键来自 PL 侧状态寄存器，不依赖 USB 手柄链路。 */
    return PsGbaRegs_Read(runtime->regs, GBA_REG_STATUS0) & 0x3FFU;
}

static u16 PsAppUi_MapPhysicalKeysToMenuButtons(u32 physical_keys) {
    u16 buttons;

    buttons = 0U;

    if ((physical_keys & PS_APP_UI_PHY_KEY_UP) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_UP;
    }
    if ((physical_keys & PS_APP_UI_PHY_KEY_DOWN) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_DOWN;
    }
    if ((physical_keys & PS_APP_UI_PHY_KEY_LEFT) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_LEFT;
    }
    if ((physical_keys & PS_APP_UI_PHY_KEY_RIGHT) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_RIGHT;
    }
    if ((physical_keys & PS_APP_UI_PHY_KEY_A) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_A;
    }
    if ((physical_keys & PS_APP_UI_PHY_KEY_B) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_B;
    }
    /* 板载菜单映射：
     * START(物理 bit3) -> 刷新（等价 X）
     * SELECT(物理 bit2) -> 关于（等价 Y） */
    if ((physical_keys & PS_APP_UI_PHY_KEY_START) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_X;
    }
    if ((physical_keys & PS_APP_UI_PHY_KEY_SELECT) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_Y;
    }

    return buttons;
}

static u16 PsAppUi_GetMenuButtonMask(const PsAppRuntimeContext *runtime) {
    const PsAppInputState *input;
    u16 buttons;
    u32 src_mask;
    u32 physical_keys;

    if (runtime == NULL) {
        return 0U;
    }

    input = runtime->input;
    buttons = (input != NULL) ? input->buttons : 0U;
    src_mask = (input != NULL) ? input->source_snapshot_mask : 0U;
    physical_keys = PsAppUi_ReadPhysicalKeys(runtime);

    /* 兼容首进菜单阶段的方向输入来源差异：
     * 某些手柄/接收器在启动初期会把方向优先体现在左摇杆阈值位，
     * 而不是 XInput DPad 低 4 位。菜单统一把“摇杆方向位”折叠成 DPad 位，
     * 这样 UI 的上下选择逻辑不依赖具体报告形态。 */
    if ((src_mask & PS_APP_INPUT_SRC_MASK_LS_UP) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_UP;
    }
    if ((src_mask & PS_APP_INPUT_SRC_MASK_LS_DOWN) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_DOWN;
    }
    if ((src_mask & PS_APP_INPUT_SRC_MASK_LS_LEFT) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_LEFT;
    }
    if ((src_mask & PS_APP_INPUT_SRC_MASK_LS_RIGHT) != 0U) {
        buttons |= PS_XINPUT_BUTTON_MASK_RIGHT;
    }

    /* 菜单输入最终来源 = 手柄输入 OR 板载物理键输入。 */
    buttons |= PsAppUi_MapPhysicalKeysToMenuButtons(physical_keys);

    return buttons;
}

static void PsAppUi_ForceFullRedraw(void) {
    if ((s_ps_app_ui_runtime.display == NULL) || (s_ps_app_ui_runtime.screen == NULL)) {
        return;
    }

    /* GAME 态期间显示链路会持续写入视频帧。
     * 返回 MENU 后如果只靠 LVGL 的局部脏区刷新，可能残留黑底或旧帧碎片。
     * 这里强制把整屏标脏并立即刷新，确保菜单壳体/背景一次性完整恢复。 */
    lv_obj_invalidate(s_ps_app_ui_runtime.screen);
    lv_refr_now(s_ps_app_ui_runtime.display);
}

static void PsAppUi_EnsureMenuScreen(void) {
    if ((s_ps_app_ui_runtime.screen != NULL) &&
        (lv_screen_active() != s_ps_app_ui_runtime.screen)) {
        /* 从 GAME/LOADING 切回 MENU 时，显式切屏并触发整屏重绘。 */
        lv_screen_load(s_ps_app_ui_runtime.screen);
        PsAppUi_ForceFullRedraw();
    }
}

static void PsAppUi_SetAboutVisible(PsAppUiContext *ctx, u8 visible) {
    PsAppUiState *state;
    const char *resume_status;
    char resume_buffer[96];

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    if (s_ps_app_ui_runtime.about_panel == NULL) {
        return;
    }

    state = ctx->state;
    if (visible != 0U) {
        /* “关于”面板是菜单叠层：开启时仅接管菜单按键，不改变运行时状态机。 */
        lv_obj_clear_flag(s_ps_app_ui_runtime.about_panel, LV_OBJ_FLAG_HIDDEN);
        s_ps_app_ui_runtime.about_visible = 1U;
        PsAppUi_SetStatus(ctx, "关于界面: 查看作者与开源致谢信息");
    } else {
        lv_obj_add_flag(s_ps_app_ui_runtime.about_panel, LV_OBJ_FLAG_HIDDEN);
        s_ps_app_ui_runtime.about_visible = 0U;

        if (s_ps_app_ui_runtime.last_scan_failed != 0U) {
            (void)snprintf(resume_buffer,
                           sizeof(resume_buffer),
                           "读取列表失败: %s",
                           PsAppUi_StorageFailureText(s_ps_app_ui_runtime.last_scan_fs_result));
            resume_status = resume_buffer;
        } else if (state->game_count == 0U) {
            resume_status = "未找到游戏";
        } else {
            resume_status = "菜单就绪";
        }
        PsAppUi_SetStatus(ctx, resume_status);
    }
}

static void PsAppUi_ProcessMenuInput(PsAppUiContext *ctx) {
    PsAppUiState *state;
    PsAppRuntimeContext *runtime;
    u16 prev_buttons;
    u16 cur_buttons;
    u8 need_rebuild;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->runtime == NULL)) {
        return;
    }

    state = ctx->state;
    runtime = ctx->runtime;
    prev_buttons = (u16)state->last_buttons;
    cur_buttons = PsAppUi_GetMenuButtonMask(runtime);
    need_rebuild = 0U;

    if (PsAppUi_ButtonPressedEdge(prev_buttons, cur_buttons, PS_XINPUT_BUTTON_MASK_Y) != 0U) {
        PsAppUi_SetAboutVisible(ctx,
                                (s_ps_app_ui_runtime.about_visible == 0U) ? 1U : 0U);
    }

    if ((s_ps_app_ui_runtime.about_visible != 0U) &&
        ((PsAppUi_ButtonPressedEdge(prev_buttons, cur_buttons, PS_XINPUT_BUTTON_MASK_B) != 0U) ||
         (PsAppUi_ButtonPressedEdge(prev_buttons, cur_buttons, PS_XINPUT_BUTTON_MASK_BACK) != 0U))) {
        PsAppUi_SetAboutVisible(ctx, 0U);
    }

    if (s_ps_app_ui_runtime.about_visible != 0U) {
        /* 叠层开启时只允许关闭，不消费方向/A 启动，避免误进入游戏。 */
        state->last_buttons = cur_buttons;
        return;
    }

    if ((state->game_count > 0U) &&
        (PsAppUi_ButtonPressedEdge(prev_buttons, cur_buttons, PS_XINPUT_BUTTON_MASK_UP) != 0U)) {
        if (state->selected_index == 0U) {
            state->selected_index = (u8)(state->game_count - 1U);
        } else {
            state->selected_index--;
        }
        need_rebuild = 1U;
    }

    if ((state->game_count > 0U) &&
        (PsAppUi_ButtonPressedEdge(prev_buttons, cur_buttons, PS_XINPUT_BUTTON_MASK_DOWN) != 0U)) {
        state->selected_index = (u8)((state->selected_index + 1U) % state->game_count);
        need_rebuild = 1U;
    }

    if (PsAppUi_ButtonPressedEdge(prev_buttons, cur_buttons, PS_XINPUT_BUTTON_MASK_X) != 0U) {
        state->refresh_requested = 1U;
        PsAppUi_SetStatus(ctx, "正在刷新...");
        xil_printf("[UI] refresh requested by X/START\r\n");
    }

    if ((state->game_count > 0U) &&
        (PsAppUi_ButtonPressedEdge(prev_buttons, cur_buttons, PS_XINPUT_BUTTON_MASK_A) != 0U)) {
        state->launch_requested = 1U;
        state->launch_index = state->selected_index;
        state->mode = (u8)PS_APP_UI_MODE_LOADING;
        PsAppUi_CopyText(state->launch_path,
                         sizeof(state->launch_path),
                         s_ps_app_ui_runtime.games[state->launch_index].path);
        PsAppUi_SetStatus(ctx, "正在加载ROM...");
        need_rebuild = 1U;
    }

    state->last_buttons = cur_buttons;

    if (need_rebuild != 0U) {
        PsAppUi_RebuildList(ctx);
    }
}

static void PsAppUi_ProcessLaunch(PsAppUiContext *ctx) {
    PsAppUiState *state;
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->runtime == NULL)) {
        return;
    }

    state = ctx->state;
    if (state->launch_requested == 0U) {
        return;
    }

    state->launch_requested = 0U;
    /* 在真正加载 ROM 前先把“加载中”状态刷到屏幕上，
     * 避免用户感知为按键无响应。 */
    PsAppUi_EnsureMenuScreen();
    PsAppUi_RebuildList(ctx);
    lv_tick_inc(1U);
    (void)lv_timer_handler();

    status = PsAppRuntime_LoadRomFromSd(ctx->runtime, state->launch_path);
    if (status == XST_SUCCESS) {
        /* 成功进入 GAME：重置 LT 长按退出计时器与锁存位。 */
        PsAppUi_SetAboutVisible(ctx, 0U);
        state->mode = (u8)PS_APP_UI_MODE_GAME;
        state->lt_hold_ms = 0U;
        state->lt_exit_latched = 0U;
        state->board_exit_hold_ms = 0U;
        state->board_exit_latched = 0U;
        state->last_buttons = PsAppUi_GetMenuButtonMask(ctx->runtime);
        PsAppUi_SetStatus(ctx, "游戏运行中");
        PsAppUi_UpdateMetaLabels(ctx);
        xil_printf("[UI] launch ok: %s\r\n", state->launch_path);
    } else {
        /* 加载失败则回菜单并请求刷新列表，便于立即重试。 */
        state->mode = (u8)PS_APP_UI_MODE_MENU;
        state->refresh_requested = 1U;
        state->board_exit_hold_ms = 0U;
        state->board_exit_latched = 0U;
        PsAppUi_SetStatus(ctx, "加载失败");
        PsAppUi_RebuildList(ctx);
        xil_printf("[UI] launch failed: %s\r\n", state->launch_path);
    }
}

static void PsAppUi_ExitGameToMenu(PsAppUiContext *ctx, const char *trigger_name) {
    PsAppUiState *state;
    PsAppRuntimeContext *runtime;
    XStatus unload_status;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->runtime == NULL)) {
        return;
    }

    state = ctx->state;
    runtime = ctx->runtime;
    state->mode = (u8)PS_APP_UI_MODE_LOADING;
    PsAppUi_SetStatus(ctx, "正在返回菜单...");
    PsAppUi_EnsureMenuScreen();
    PsAppUi_RebuildList(ctx);
    lv_tick_inc(1U);
    (void)lv_timer_handler();

    unload_status = PsAppRuntime_UnloadRom(runtime);
    if (unload_status == XST_SUCCESS) {
        /* 必须走安全卸载后再回菜单，避免 save/rtc 状态丢失。 */
        state->mode = (u8)PS_APP_UI_MODE_MENU;
        state->refresh_requested = 1U;
        state->lt_hold_ms = 0U;
        state->lt_exit_latched = 0U;
        state->board_exit_hold_ms = 0U;
        state->board_exit_latched = 0U;
        state->last_buttons = PsAppUi_GetMenuButtonMask(runtime);
        PsAppUi_SetStatus(ctx, "已返回菜单");
        PsAppUi_RebuildList(ctx);
        /* 退出 GAME 后强制整屏重绘，消除局部刷新的残影。 */
        PsAppUi_ForceFullRedraw();
        xil_printf("[UI] %s exit: back to menu\r\n",
                   (trigger_name != NULL) ? trigger_name : "game");
    } else {
        state->mode = (u8)PS_APP_UI_MODE_GAME;
        PsAppUi_SetStatus(ctx, "退出失败");
        PsAppUi_UpdateMetaLabels(ctx);
        xil_printf("[UI] %s exit failed: %d\r\n",
                   (trigger_name != NULL) ? trigger_name : "game",
                   unload_status);
    }
}

static void PsAppUi_ProcessGameInput(PsAppUiContext *ctx) {
    PsAppUiState *state;
    PsAppRuntimeContext *runtime;
    u32 ps_btn_mask;
    u8 lt_value;
    u8 board_combo_pressed;
    u32 next_hold_ms;
    u8 lt_triggered;
    u8 board_triggered;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->runtime == NULL)) {
        return;
    }

    state = ctx->state;
    runtime = ctx->runtime;
    lt_value = (runtime->input != NULL) ? runtime->input->lt : 0U;
    ps_btn_mask = PsAppRuntime_ReadPsButtonMask(runtime);
    board_combo_pressed = (u8)(((ps_btn_mask & PS_APP_UI_BOARD_EXIT_BTN_MASK) ==
                                PS_APP_UI_BOARD_EXIT_BTN_MASK) ? 1U : 0U);
    lt_triggered = 0U;
    board_triggered = 0U;

    /* LT 需要“按到底并持续 5 秒”才触发退出，短按/抖动不会误退出。 */
    if (lt_value >= PS_APP_UI_LT_EXIT_THRESHOLD) {
        next_hold_ms = state->lt_hold_ms + PS_APP_UI_SERVICE_INTERVAL_MS;
        if (next_hold_ms > PS_APP_UI_LT_EXIT_HOLD_MS) {
            next_hold_ms = PS_APP_UI_LT_EXIT_HOLD_MS;
        }
        state->lt_hold_ms = next_hold_ms;

        if ((state->lt_hold_ms >= PS_APP_UI_LT_EXIT_HOLD_MS) &&
            (state->lt_exit_latched == 0U)) {
            state->lt_exit_latched = 1U;
            lt_triggered = 1U;
        }
    } else {
        /* LT 放开后清零计时与锁存，下一次长按才允许重新触发。 */
        state->lt_hold_ms = 0U;
        state->lt_exit_latched = 0U;
    }

    /* 板载系统键退出：BTN4+BTN5 同时按住 2 秒触发一次退出。
     * 该通道与 LT 长按并行存在，用于无手柄或手柄异常时兜底返回菜单。 */
    if (board_combo_pressed != 0U) {
        next_hold_ms = state->board_exit_hold_ms + PS_APP_UI_SERVICE_INTERVAL_MS;
        if (next_hold_ms > PS_APP_UI_BOARD_EXIT_HOLD_MS) {
            next_hold_ms = PS_APP_UI_BOARD_EXIT_HOLD_MS;
        }
        state->board_exit_hold_ms = next_hold_ms;

        if ((state->board_exit_hold_ms >= PS_APP_UI_BOARD_EXIT_HOLD_MS) &&
            (state->board_exit_latched == 0U)) {
            state->board_exit_latched = 1U;
            board_triggered = 1U;
        }
    } else {
        state->board_exit_hold_ms = 0U;
        state->board_exit_latched = 0U;
    }

    if (lt_triggered != 0U) {
        /* 两个退出条件同周期同时满足时，优先记录 LT 触发。 */
        PsAppUi_ExitGameToMenu(ctx, "LT hold");
    } else if (board_triggered != 0U) {
        PsAppUi_ExitGameToMenu(ctx, "BTN4+BTN5 hold");
    }
}

static void PsAppUi_SyncModeWithRuntime(PsAppUiContext *ctx) {
    PsAppUiState *state;
    PsAppRuntimeContext *runtime;
    u8 rom_loaded;
    u8 rom_loading;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->runtime == NULL) || (ctx->runtime->rom == NULL)) {
        return;
    }

    state = ctx->state;
    runtime = ctx->runtime;
    rom_loaded = runtime->rom->loaded;
    rom_loading = runtime->rom->is_loading;

    if (rom_loading != 0U) {
        return;
    }

    if (rom_loaded != 0U) {
        if (state->mode != (u8)PS_APP_UI_MODE_GAME) {
            /* 外部路径（如串口命令）加载 ROM 时，同步 UI 模式到 GAME。 */
            PsAppUi_SetAboutVisible(ctx, 0U);
            state->mode = (u8)PS_APP_UI_MODE_GAME;
            state->lt_hold_ms = 0U;
            state->lt_exit_latched = 0U;
            state->board_exit_hold_ms = 0U;
            state->board_exit_latched = 0U;
            state->last_buttons = PsAppUi_GetMenuButtonMask(runtime);
            PsAppUi_SetStatus(ctx, "游戏运行中");
            PsAppUi_UpdateMetaLabels(ctx);
        }
    } else if (state->mode == (u8)PS_APP_UI_MODE_GAME) {
        /* ROM 已卸载但 UI 仍在 GAME，兜底拉回 MENU。 */
        state->mode = (u8)PS_APP_UI_MODE_MENU;
        state->refresh_requested = 1U;
        state->launch_requested = 0U;
        state->lt_hold_ms = 0U;
        state->lt_exit_latched = 0U;
        state->board_exit_hold_ms = 0U;
        state->board_exit_latched = 0U;
        state->last_buttons = PsAppUi_GetMenuButtonMask(runtime);
        PsAppUi_SetStatus(ctx, "菜单就绪");
        PsAppUi_RebuildList(ctx);
        /* 同步回菜单时同样需要整屏重绘。 */
        PsAppUi_ForceFullRedraw();
    }
}

static void PsAppUi_UpdateAnimation(PsAppUiContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    s_ps_app_ui_runtime.anim_elapsed_ms += PS_APP_UI_SERVICE_INTERVAL_MS;
    s_ps_app_ui_runtime.blink_elapsed_ms += PS_APP_UI_SERVICE_INTERVAL_MS;

    if (s_ps_app_ui_runtime.blink_elapsed_ms >= PS_APP_UI_BLINK_INTERVAL_MS) {
        s_ps_app_ui_runtime.blink_elapsed_ms = 0U;
        s_ps_app_ui_runtime.blink_phase ^= 1U;
    }

    if (s_ps_app_ui_runtime.led != NULL) {
        if (s_ps_app_ui_runtime.blink_phase == 0U) {
            lv_obj_set_style_bg_color(s_ps_app_ui_runtime.led, PsAppUi_HexColor(PS_APP_UI_COLOR_GREEN), 0);
        } else {
            lv_obj_set_style_bg_color(s_ps_app_ui_runtime.led, PsAppUi_HexColor(PS_APP_UI_COLOR_GREEN_DARK), 0);
        }
    }

    if (s_ps_app_ui_runtime.status_progress != NULL) {
        if ((ctx->state->mode == (u8)PS_APP_UI_MODE_LOADING) ||
            (ctx->state->refresh_requested != 0U)) {
            lv_obj_clear_flag(s_ps_app_ui_runtime.status_progress, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ps_app_ui_runtime.status_progress, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_ps_app_ui_runtime.status_progress_fill != NULL) {
        lv_coord_t fill_w;

        if (ctx->state->mode == (u8)PS_APP_UI_MODE_LOADING) {
            fill_w = (lv_coord_t)(18 + ((s_ps_app_ui_runtime.anim_elapsed_ms / 40U) % 74U));
            lv_obj_set_style_bg_color(s_ps_app_ui_runtime.status_progress_fill,
                                      PsAppUi_HexColor(PS_APP_UI_COLOR_AMBER),
                                      0);
        } else if (ctx->state->refresh_requested != 0U) {
            fill_w = (lv_coord_t)(24 + ((s_ps_app_ui_runtime.anim_elapsed_ms / 60U) % 60U));
            lv_obj_set_style_bg_color(s_ps_app_ui_runtime.status_progress_fill,
                                      PsAppUi_HexColor(PS_APP_UI_COLOR_GREEN),
                                      0);
        } else {
            fill_w = (ctx->state->game_count == 0U) ? 24 : 90;
            lv_obj_set_style_bg_color(s_ps_app_ui_runtime.status_progress_fill,
                                      PsAppUi_HexColor((ctx->state->game_count == 0U) ?
                                                      PS_APP_UI_COLOR_AMBER :
                                                      PS_APP_UI_COLOR_GREEN_DARK),
                                      0);
        }
        lv_obj_set_width(s_ps_app_ui_runtime.status_progress_fill, fill_w);
    }

    PsAppUi_UpdateListVisuals(ctx, 0U);
}

static void PsAppUi_LvglFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    PsAppUiContext *ctx;
    PsHdmiVdma *vdma;
    int32_t clip_x1;
    int32_t clip_x2;
    int32_t clip_y1;
    int32_t clip_y2;
    u32 src_stride_bytes;
    const u8 *src_row;
    int32_t y;

    if ((disp == NULL) || (area == NULL) || (px_map == NULL)) {
        if (disp != NULL) {
            lv_display_flush_ready(disp);
        }
        return;
    }

    ctx = (PsAppUiContext *)lv_display_get_user_data(disp);
    if ((ctx == NULL) || (ctx->video == NULL) || (ctx->video->vdma == NULL)) {
        lv_display_flush_ready(disp);
        return;
    }

    vdma = ctx->video->vdma;
    if (vdma->is_ready == 0U) {
        lv_display_flush_ready(disp);
        return;
    }

    clip_x1 = area->x1;
    clip_x2 = area->x2;
    clip_y1 = area->y1;
    clip_y2 = area->y2;

    if (clip_x1 < 0) {
        clip_x1 = 0;
    }
    if (clip_y1 < 0) {
        clip_y1 = 0;
    }
    if (clip_x2 >= (int32_t)PS_APP_HDMI_WIDTH) {
        clip_x2 = (int32_t)PS_APP_HDMI_WIDTH - 1;
    }
    if (clip_y2 >= (int32_t)PS_APP_HDMI_HEIGHT) {
        clip_y2 = (int32_t)PS_APP_HDMI_HEIGHT - 1;
    }
    if ((clip_x1 > clip_x2) || (clip_y1 > clip_y2)) {
        lv_display_flush_ready(disp);
        return;
    }

    src_stride_bytes = (u32)(area->x2 - area->x1 + 1) * sizeof(u32);
    src_row = px_map +
              ((u32)(clip_y1 - area->y1) * src_stride_bytes) +
              ((u32)(clip_x1 - area->x1) * sizeof(u32));

    for (y = clip_y1; y <= clip_y2; ++y) {
        UINTPTR dst_addr;
        u32 row_bytes;

        dst_addr = vdma->frame_addrs[PS_APP_UI_TARGET_FRAME_IDX] +
                   ((UINTPTR)(u32)y * (UINTPTR)vdma->line_stride_bytes) +
                   ((UINTPTR)(u32)clip_x1 * sizeof(u32));
        row_bytes = (u32)(clip_x2 - clip_x1 + 1) * sizeof(u32);
        memcpy((void *)dst_addr, src_row, row_bytes);
        Xil_DCacheFlushRange((INTPTR)dst_addr, (INTPTR)row_bytes);
        src_row += src_stride_bytes;
    }

    (void)PsAppVideo_RequestFrame(ctx->video, PS_APP_UI_TARGET_FRAME_IDX);
    PsAppVideo_SyncDisplayFrame(ctx->video);
    lv_display_flush_ready(disp);
}

static void PsAppUi_BuildScreen(PsAppUiContext *ctx) {
    lv_obj_t *scanline;
    lv_obj_t *list_title;
    lv_obj_t *led_text;
    lv_obj_t *brand_plate;
    lv_obj_t *sd_capsule;
    lv_obj_t *sd_label;
    lv_obj_t *subtitle_label;
    lv_obj_t *about_content;
    u32 row;

    s_ps_app_ui_runtime.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_ps_app_ui_runtime.screen);
    lv_obj_set_size(s_ps_app_ui_runtime.screen, PS_APP_HDMI_WIDTH, PS_APP_HDMI_HEIGHT);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ps_app_ui_runtime.screen, PsAppUi_HexColor(PS_APP_UI_COLOR_BG), 0);
    lv_obj_set_style_radius(s_ps_app_ui_runtime.screen, 0, 0);
    lv_obj_clear_flag(s_ps_app_ui_runtime.screen, LV_OBJ_FLAG_SCROLLABLE);

    for (row = 0U; row < PS_APP_HDMI_HEIGHT; row += 24U) {
        scanline = lv_obj_create(s_ps_app_ui_runtime.screen);
        lv_obj_remove_style_all(scanline);
        lv_obj_set_size(scanline, PS_APP_HDMI_WIDTH, 1);
        lv_obj_set_pos(scanline, 0, (lv_coord_t)row);
        lv_obj_set_style_bg_opa(scanline, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(scanline, PsAppUi_HexColor(PS_APP_UI_COLOR_BG_STRIPE), 0);
        lv_obj_set_style_radius(scanline, 0, 0);
        lv_obj_clear_flag(scanline, LV_OBJ_FLAG_SCROLLABLE);
    }

    s_ps_app_ui_runtime.shell_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.screen,
                                                                12,
                                                                10,
                                                                616,
                                                                460,
                                                                PS_APP_UI_COLOR_SHELL,
                                                                PS_APP_UI_COLOR_SHELL_DARK,
                                                                PS_APP_UI_COLOR_PANEL_SHADOW);
    (void)PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.shell_panel,
                                   14,
                                   14,
                                   588,
                                   432,
                                   PS_APP_UI_COLOR_PANEL_DIM,
                                   0x9B967FU);
    (void)PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.shell_panel,
                                   18,
                                   18,
                                   580,
                                   6,
                                   0xFFF8DFU,
                                   PS_APP_UI_COLOR_PANEL_DIM);

    s_ps_app_ui_runtime.header_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.shell_panel,
                                                                 28,
                                                                 26,
                                                                 560,
                                                                 44,
                                                                 PS_APP_UI_COLOR_PANEL,
                                                                 PS_APP_UI_COLOR_SHELL_DARK,
                                                                 0x9B967FU);

    brand_plate = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.header_panel,
                                           10,
                                           6,
                                           174,
                                           32,
                                           PS_APP_UI_COLOR_GREEN,
                                           PS_APP_UI_COLOR_SHELL_DARK);
    PsAppUi_CreateMonoBitmap(brand_plate,
                             12,
                             6,
                             &ps_app_ui_wordmark_pixel_gba,
                             3,
                             PS_APP_UI_COLOR_INK);

    s_ps_app_ui_runtime.title_label = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_label_set_text(s_ps_app_ui_runtime.title_label, "GAME LIBRARY");
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.title_label, PS_APP_UI_COLOR_INK);
    lv_obj_set_pos(s_ps_app_ui_runtime.title_label, 202, 7);

    subtitle_label = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_label_set_text(subtitle_label, "FPGA CART SYSTEM");
    PsAppUi_SetTextStyle(subtitle_label, PS_APP_UI_COLOR_INK_MUTED);
    lv_obj_set_pos(subtitle_label, 202, 24);

    s_ps_app_ui_runtime.meta_label = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.meta_label, 96);
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.meta_label, PS_APP_UI_COLOR_INK_MUTED);
    lv_obj_set_style_text_align(s_ps_app_ui_runtime.meta_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_ps_app_ui_runtime.meta_label, 384, 7);

    s_ps_app_ui_runtime.clock_label = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.clock_label, 168);
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.clock_label, PS_APP_UI_COLOR_INK);
    lv_obj_set_style_text_align(s_ps_app_ui_runtime.clock_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_ps_app_ui_runtime.clock_label, 368, 24);
    lv_label_set_text(s_ps_app_ui_runtime.clock_label, "BJ ----/--/-- --:--:--");
    s_ps_app_ui_runtime.last_clock_unix = PS_APP_UI_CLOCK_INVALID_UNIX;
    PsAppUi_CreateBitmapIcon(s_ps_app_ui_runtime.header_panel,
                             350,
                             24,
                             &ps_app_ui_icon_clock,
                             1,
                             PS_APP_UI_COLOR_INK_MUTED);

    s_ps_app_ui_runtime.led = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.header_panel,
                                                        532,
                                                        7,
                                                        12,
                                                        12,
                                                        PS_APP_UI_COLOR_GREEN,
                                                        PS_APP_UI_COLOR_GREEN_DARK);
    led_text = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_label_set_text(led_text, "LIVE");
    PsAppUi_SetTextStyle(led_text, PS_APP_UI_COLOR_INK_MUTED);
    lv_obj_set_pos(led_text, 496, 7);

    s_ps_app_ui_runtime.list_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.shell_panel,
                                                               28,
                                                               84,
                                                               360,
                                                               270,
                                                               PS_APP_UI_COLOR_DISPLAY,
                                                               0x080D0AU,
                                                               PS_APP_UI_COLOR_PANEL_SHADOW);
    list_title = lv_label_create(s_ps_app_ui_runtime.list_panel);
    lv_label_set_text(list_title, "PIXEL GBA LIBRARY");
    PsAppUi_SetTextStyle(list_title, PS_APP_UI_COLOR_GREEN_SOFT);
    lv_obj_set_pos(list_title, 34, 8);
    PsAppUi_CreateBitmapIcon(s_ps_app_ui_runtime.list_panel,
                             14,
                             7,
                             &ps_app_ui_icon_cart,
                             1,
                             PS_APP_UI_COLOR_GREEN_SOFT);
    sd_capsule = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.list_panel,
                                          238,
                                          8,
                                          96,
                                          18,
                                          PS_APP_UI_COLOR_DISPLAY_ALT,
                                          PS_APP_UI_COLOR_GRID);
    PsAppUi_CreateBitmapIcon(sd_capsule,
                             8,
                             1,
                             &ps_app_ui_icon_sd,
                             1,
                             PS_APP_UI_COLOR_TEXT_MUTED);
    sd_label = lv_label_create(sd_capsule);
    lv_label_set_text(sd_label, "0:/games");
    PsAppUi_SetTextStyle(sd_label, PS_APP_UI_COLOR_TEXT_MUTED);
    lv_obj_align(sd_label, LV_ALIGN_LEFT_MID, 30, 0);

    s_ps_app_ui_runtime.list = lv_list_create(s_ps_app_ui_runtime.list_panel);
    lv_obj_remove_style_all(s_ps_app_ui_runtime.list);
    lv_obj_set_size(s_ps_app_ui_runtime.list, 336, 210);
    lv_obj_set_pos(s_ps_app_ui_runtime.list, 12, 34);
    lv_obj_set_layout(s_ps_app_ui_runtime.list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ps_app_ui_runtime.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ps_app_ui_runtime.list,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.list, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ps_app_ui_runtime.list, PsAppUi_HexColor(0x0D1712), 0);
    lv_obj_set_style_border_width(s_ps_app_ui_runtime.list, 2, 0);
    lv_obj_set_style_border_color(s_ps_app_ui_runtime.list, PsAppUi_HexColor(PS_APP_UI_COLOR_GRID), 0);
    lv_obj_set_style_radius(s_ps_app_ui_runtime.list, 0, 0);
    lv_obj_set_style_pad_all(s_ps_app_ui_runtime.list, 6, 0);
    lv_obj_set_style_pad_row(s_ps_app_ui_runtime.list, 5, 0);
    lv_obj_set_scrollbar_mode(s_ps_app_ui_runtime.list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_ps_app_ui_runtime.list, PsAppUi_HexColor(PS_APP_UI_COLOR_GREEN_DARK), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_ps_app_ui_runtime.list, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_ps_app_ui_runtime.list, 0, LV_PART_SCROLLBAR);

    s_ps_app_ui_runtime.detail_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.shell_panel,
                                                                 400,
                                                                 84,
                                                                 188,
                                                                 270,
                                                                 PS_APP_UI_COLOR_PANEL,
                                                                 PS_APP_UI_COLOR_SHELL_DARK,
                                                                 0x9B967FU);
    PsAppUi_CreateBitmapIcon(s_ps_app_ui_runtime.detail_panel,
                             14,
                             14,
                             &ps_app_ui_icon_cart,
                             2,
                             PS_APP_UI_COLOR_GREEN_DARK);
    (void)PsAppUi_CreateText(s_ps_app_ui_runtime.detail_panel,
                             "CARTRIDGE DETAIL",
                             PS_APP_UI_COLOR_INK_MUTED,
                             54,
                             14);
    s_ps_app_ui_runtime.detail_slot_label = lv_label_create(s_ps_app_ui_runtime.detail_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.detail_slot_label, 118);
    lv_label_set_long_mode(s_ps_app_ui_runtime.detail_slot_label, LV_LABEL_LONG_DOT);
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.detail_slot_label, PS_APP_UI_COLOR_INK);
    lv_obj_set_pos(s_ps_app_ui_runtime.detail_slot_label, 54, 30);

    s_ps_app_ui_runtime.detail_title_label = lv_label_create(s_ps_app_ui_runtime.detail_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.detail_title_label, 160);
    lv_label_set_long_mode(s_ps_app_ui_runtime.detail_title_label, LV_LABEL_LONG_DOT);
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.detail_title_label, PS_APP_UI_COLOR_INK);
    lv_obj_set_pos(s_ps_app_ui_runtime.detail_title_label, PS_APP_UI_DETAIL_FIELD_X, 64);
    (void)PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.detail_panel,
                                   PS_APP_UI_DETAIL_FIELD_X,
                                   88,
                                   160,
                                   2,
                                   PS_APP_UI_COLOR_GREEN_DARK,
                                   PS_APP_UI_COLOR_GREEN_DARK);

    (void)PsAppUi_CreateDetailFieldLabel(s_ps_app_ui_runtime.detail_panel,
                                         "PATH",
                                         PS_APP_UI_DETAIL_ROW_PATH_Y);
    s_ps_app_ui_runtime.detail_path_label = lv_label_create(s_ps_app_ui_runtime.detail_panel);
    lv_label_set_long_mode(s_ps_app_ui_runtime.detail_path_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_size(s_ps_app_ui_runtime.detail_path_label, PS_APP_UI_DETAIL_PATH_WIDTH, 18);
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.detail_path_label, PS_APP_UI_COLOR_INK_MUTED);
    lv_obj_set_pos(s_ps_app_ui_runtime.detail_path_label, PS_APP_UI_DETAIL_VALUE_X, PS_APP_UI_DETAIL_ROW_PATH_Y);

    (void)PsAppUi_CreateDetailFieldLabel(s_ps_app_ui_runtime.detail_panel,
                                         "SIZE",
                                         PS_APP_UI_DETAIL_ROW_SIZE_Y);
    s_ps_app_ui_runtime.detail_size_value_label =
        PsAppUi_CreateDetailValueLabel(s_ps_app_ui_runtime.detail_panel,
                                       PS_APP_UI_DETAIL_ROW_SIZE_Y,
                                       PS_APP_UI_COLOR_INK);

    (void)PsAppUi_CreateDetailFieldLabel(s_ps_app_ui_runtime.detail_panel,
                                         "CODE",
                                         PS_APP_UI_DETAIL_ROW_CODE_Y);
    s_ps_app_ui_runtime.detail_code_value_label =
        PsAppUi_CreateDetailValueLabel(s_ps_app_ui_runtime.detail_panel,
                                       PS_APP_UI_DETAIL_ROW_CODE_Y,
                                       PS_APP_UI_COLOR_INK);

    (void)PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.detail_panel,
                                   PS_APP_UI_DETAIL_FIELD_X,
                                   168,
                                   160,
                                   2,
                                   PS_APP_UI_COLOR_PANEL_DIM,
                                   PS_APP_UI_COLOR_PANEL_DIM);

    (void)PsAppUi_CreateDetailFieldLabel(s_ps_app_ui_runtime.detail_panel,
                                         "CRC",
                                         PS_APP_UI_DETAIL_ROW_CRC_Y);
    s_ps_app_ui_runtime.detail_crc_value_label =
        PsAppUi_CreateDetailValueLabel(s_ps_app_ui_runtime.detail_panel,
                                       PS_APP_UI_DETAIL_ROW_CRC_Y,
                                       PS_APP_UI_COLOR_INK);

    (void)PsAppUi_CreateDetailFieldLabel(s_ps_app_ui_runtime.detail_panel,
                                         "BIOS",
                                         PS_APP_UI_DETAIL_ROW_BIOS_Y);
    s_ps_app_ui_runtime.detail_bios_value_label =
        PsAppUi_CreateDetailValueLabel(s_ps_app_ui_runtime.detail_panel,
                                       PS_APP_UI_DETAIL_ROW_BIOS_Y,
                                       PS_APP_UI_COLOR_INK_MUTED);

    (void)PsAppUi_CreateDetailFieldLabel(s_ps_app_ui_runtime.detail_panel,
                                         "SAVE",
                                         PS_APP_UI_DETAIL_ROW_SAVE_Y);
    s_ps_app_ui_runtime.detail_save_value_label =
        PsAppUi_CreateDetailValueLabel(s_ps_app_ui_runtime.detail_panel,
                                       PS_APP_UI_DETAIL_ROW_SAVE_Y,
                                       PS_APP_UI_COLOR_INK_MUTED);

    PsAppUi_CreateBitmapIcon(s_ps_app_ui_runtime.detail_panel,
                             PS_APP_UI_DETAIL_FIELD_X,
                             242,
                             &ps_app_ui_icon_play,
                             1,
                             PS_APP_UI_COLOR_GREEN_DARK);
    s_ps_app_ui_runtime.detail_hint_label = lv_label_create(s_ps_app_ui_runtime.detail_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.detail_hint_label, 136);
    lv_label_set_long_mode(s_ps_app_ui_runtime.detail_hint_label, LV_LABEL_LONG_DOT);
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.detail_hint_label, PS_APP_UI_COLOR_INK);
    lv_obj_set_pos(s_ps_app_ui_runtime.detail_hint_label, PS_APP_UI_DETAIL_VALUE_X, 244);

    s_ps_app_ui_runtime.status_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.shell_panel,
                                                                 28,
                                                                 364,
                                                                 560,
                                                                 42,
                                                                 PS_APP_UI_COLOR_PANEL,
                                                                 PS_APP_UI_COLOR_SHELL_DARK,
                                                                 0x9B967FU);

    s_ps_app_ui_runtime.status_label = lv_label_create(s_ps_app_ui_runtime.status_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.status_label, 392);
    lv_label_set_long_mode(s_ps_app_ui_runtime.status_label, LV_LABEL_LONG_DOT);
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.status_label, PS_APP_UI_COLOR_INK);
    lv_obj_set_pos(s_ps_app_ui_runtime.status_label, 34, 13);
    s_ps_app_ui_runtime.status_icon_info = PsAppUi_CreateIconLayer(s_ps_app_ui_runtime.status_panel,
                                                                   12,
                                                                   7,
                                                                   &ps_app_ui_icon_info,
                                                                   PS_APP_UI_COLOR_INK_MUTED);
    s_ps_app_ui_runtime.status_icon_warn = PsAppUi_CreateIconLayer(s_ps_app_ui_runtime.status_panel,
                                                                   12,
                                                                   7,
                                                                   &ps_app_ui_icon_warn,
                                                                   PS_APP_UI_COLOR_AMBER);
    s_ps_app_ui_runtime.status_icon_play = PsAppUi_CreateIconLayer(s_ps_app_ui_runtime.status_panel,
                                                                   12,
                                                                   7,
                                                                   &ps_app_ui_icon_play,
                                                                   PS_APP_UI_COLOR_GREEN_DARK);
    PsAppUi_ShowStatusIcon(PS_APP_UI_STATUS_ROLE_INFO);

    s_ps_app_ui_runtime.hint_label = lv_label_create(s_ps_app_ui_runtime.status_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.hint_label, 1);
    lv_label_set_text(s_ps_app_ui_runtime.hint_label, "");
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.hint_label, PS_APP_UI_COLOR_INK_MUTED);
    lv_obj_set_pos(s_ps_app_ui_runtime.hint_label, 34, 24);
    lv_obj_add_flag(s_ps_app_ui_runtime.hint_label, LV_OBJ_FLAG_HIDDEN);

    s_ps_app_ui_runtime.status_progress = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.status_panel,
                                                                    440,
                                                                    14,
                                                                    96,
                                                                    12,
                                                                    PS_APP_UI_COLOR_PANEL_DIM,
                                                                    PS_APP_UI_COLOR_SHELL_DARK);
    s_ps_app_ui_runtime.status_progress_fill = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.status_progress,
                                                                         2,
                                                                         2,
                                                                         12,
                                                                         8,
                                                                         PS_APP_UI_COLOR_GREEN_DARK,
                                                                         PS_APP_UI_COLOR_GREEN_DARK);
    lv_obj_add_flag(s_ps_app_ui_runtime.status_progress, LV_OBJ_FLAG_HIDDEN);

    PsAppUi_BuildPixelDpad(s_ps_app_ui_runtime.shell_panel, 48, 410);
    PsAppUi_BuildPixelActionButtons(s_ps_app_ui_runtime.shell_panel, 528, 410);

    brand_plate = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.shell_panel,
                                           128,
                                           416,
                                           360,
                                           26,
                                           PS_APP_UI_COLOR_PANEL,
                                           PS_APP_UI_COLOR_SHELL_DARK);
    (void)PsAppUi_CreateKeyHint(brand_plate, 8, 3, "上下", "选择", PS_APP_UI_COLOR_SHELL_DARK);
    (void)PsAppUi_CreateKeyHint(brand_plate, 96, 3, "A", "启动", PS_APP_UI_COLOR_GREEN_DARK);
    (void)PsAppUi_CreateKeyHint(brand_plate, 184, 3, "X", "刷新", PS_APP_UI_COLOR_AMBER);
    (void)PsAppUi_CreateKeyHint(brand_plate, 272, 3, "Y", "关于", PS_APP_UI_COLOR_SHELL_DARK);

    /* “关于”叠层：默认隐藏，按 Y 显示/关闭。
     * 注意 CreatePixelPanel 会创建“shadow + panel”两个兄弟对象；
     * 若只隐藏 panel，会遗留一块阴影遮罩。这里加一层 about_panel 作为父容器，
     * 把 shadow/panel 都挂在容器内，隐藏容器即可整体隐藏。 */
    s_ps_app_ui_runtime.about_panel = lv_obj_create(s_ps_app_ui_runtime.shell_panel);
    lv_obj_remove_style_all(s_ps_app_ui_runtime.about_panel);
    lv_obj_set_size(s_ps_app_ui_runtime.about_panel, 536, 272);
    lv_obj_set_pos(s_ps_app_ui_runtime.about_panel, 42, 80);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.about_panel, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_ps_app_ui_runtime.about_panel, LV_OBJ_FLAG_SCROLLABLE);

    about_content = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.about_panel,
                                             0,
                                             0,
                                             534,
                                             270,
                                             PS_APP_UI_COLOR_DISPLAY,
                                             PS_APP_UI_COLOR_GREEN_SOFT,
                                             PS_APP_UI_COLOR_PANEL_SHADOW);
    s_ps_app_ui_runtime.about_title_label = lv_label_create(about_content);
    lv_label_set_text(s_ps_app_ui_runtime.about_title_label, "PIXEL GBA / SYSTEM INFO");
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.about_title_label, PS_APP_UI_COLOR_GREEN_SOFT);
    lv_obj_set_pos(s_ps_app_ui_runtime.about_title_label, 42, 14);
    PsAppUi_CreateBitmapIcon(about_content,
                             18,
                             13,
                             &ps_app_ui_icon_info,
                             1,
                             PS_APP_UI_COLOR_GREEN_SOFT);
    (void)PsAppUi_CreatePixelBlock(about_content,
                                   18,
                                   36,
                                   498,
                                   2,
                                   PS_APP_UI_COLOR_GRID,
                                   PS_APP_UI_COLOR_GRID);

    s_ps_app_ui_runtime.about_body_label = lv_label_create(about_content);
    lv_obj_set_width(s_ps_app_ui_runtime.about_body_label, 296);
    lv_label_set_long_mode(s_ps_app_ui_runtime.about_body_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_ps_app_ui_runtime.about_body_label,
                      "SYSTEM\n"
                      "LVGL 9.5 / FreeRTOS\n"
                      "FatFs UTF-8 LFN / XRGB8888\n"
                      "PROJECT\n"
                      "Zynq GBA / MiSTer-Vivado\n"
                      "PIXEL GBA handheld menu\n"
                      "潘子彧 / 上海电机学院 2022级");
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.about_body_label, PS_APP_UI_COLOR_TEXT_ON_DARK);
    lv_obj_set_pos(s_ps_app_ui_runtime.about_body_label, 18, 54);

    (void)PsAppUi_CreatePixelBlock(about_content,
                                   334,
                                   54,
                                   2,
                                   150,
                                   PS_APP_UI_COLOR_GRID,
                                   PS_APP_UI_COLOR_GRID);

    {
        lv_obj_t *credits_label;

        credits_label = lv_label_create(about_content);
        lv_obj_set_width(credits_label, 162);
        lv_label_set_long_mode(credits_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(credits_label,
                          "CREDITS\n"
                          "LVGL graphics\n"
                          "FreeRTOS scheduling\n"
                          "FatFs filesystem\n"
                          "CherryUSB host stack\n"
                          "Xilinx Vitis drivers\n"
                          "Fusion Pixel Font");
        PsAppUi_SetTextStyle(credits_label, PS_APP_UI_COLOR_TEXT_ON_DARK);
        lv_obj_set_pos(credits_label, 354, 54);
    }

    (void)PsAppUi_CreatePixelBlock(about_content,
                                   18,
                                   214,
                                   498,
                                   2,
                                   PS_APP_UI_COLOR_GRID,
                                   PS_APP_UI_COLOR_GRID);

    s_ps_app_ui_runtime.about_hint_label = lv_label_create(about_content);
    lv_obj_set_width(s_ps_app_ui_runtime.about_hint_label, 498);
    lv_label_set_long_mode(s_ps_app_ui_runtime.about_hint_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_ps_app_ui_runtime.about_hint_label, "Y/SELECT 关闭    B 返回");
    PsAppUi_SetTextStyle(s_ps_app_ui_runtime.about_hint_label, PS_APP_UI_COLOR_AMBER);
    lv_obj_set_pos(s_ps_app_ui_runtime.about_hint_label, 18, 228);

    lv_obj_add_flag(s_ps_app_ui_runtime.about_panel, LV_OBJ_FLAG_HIDDEN);
    s_ps_app_ui_runtime.about_visible = 0U;

    PsAppUi_UpdateMetaLabels(ctx);
    PsAppUi_UpdateClockLabel(ctx);
    PsAppUi_SetStatus(ctx, "菜单就绪");
    PsAppUi_RebuildList(ctx);
    lv_screen_load(s_ps_app_ui_runtime.screen);
}

XStatus PsAppUi_Init(PsAppUiContext *ctx) {
    lv_display_t *display;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->runtime == NULL) || (ctx->video == NULL)) {
        return XST_INVALID_PARAM;
    }

    if (s_ps_app_ui_runtime.initialized != 0U) {
        s_ps_app_ui_runtime.ctx = ctx;
        return XST_SUCCESS;
    }

    memset(&s_ps_app_ui_runtime, 0, sizeof(s_ps_app_ui_runtime));
    s_ps_app_ui_runtime.ctx = ctx;
    lv_init();

    display = lv_display_create((int32_t)PS_APP_HDMI_WIDTH, (int32_t)PS_APP_HDMI_HEIGHT);
    if (display == NULL) {
        return XST_FAILURE;
    }

    s_ps_app_ui_runtime.display = display;
    lv_display_set_default(display);
    lv_display_set_user_data(display, ctx);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,
                           s_ps_app_ui_draw_buffer,
                           NULL,
                           sizeof(s_ps_app_ui_draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, PsAppUi_LvglFlushCb);

    PsAppUi_BuildScreen(ctx);
    s_ps_app_ui_runtime.initialized = 1U;
    return XST_SUCCESS;
}

void PsAppUi_RequestRefresh(PsAppUiContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    ctx->state->refresh_requested = 1U;
}

void PsAppUiTask(void *arg) {
    PsAppUiContext *ctx;
    TickType_t delay_ticks;
    PsAppUiState *state;
    XStatus init_status;

    ctx = (PsAppUiContext *)arg;
    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->runtime == NULL)) {
        xil_printf("[UI] invalid context\r\n");
        vTaskDelete(NULL);
        return;
    }

    init_status = PsAppUi_Init(ctx);
    if (init_status != XST_SUCCESS) {
        xil_printf("[UI] init failed: %d\r\n", init_status);
        vTaskDelete(NULL);
        return;
    }

    state = ctx->state;
    state->mode = (u8)PS_APP_UI_MODE_MENU;
    state->refresh_requested = 1U;
    state->launch_requested = 0U;
    state->lt_hold_ms = 0U;
    state->lt_exit_latched = 0U;
    state->board_exit_hold_ms = 0U;
    state->board_exit_latched = 0U;
    state->last_buttons = PsAppUi_GetMenuButtonMask(ctx->runtime);
    s_ps_app_ui_runtime.anim_elapsed_ms = 0U;
    s_ps_app_ui_runtime.blink_elapsed_ms = 0U;
    s_ps_app_ui_runtime.blink_phase = 0U;

    PsAppUi_SetStatus(ctx, "扫描游戏中...");
    PsAppUi_RebuildList(ctx);

    delay_ticks = pdMS_TO_TICKS(PS_APP_UI_SERVICE_INTERVAL_MS);
    if (delay_ticks == 0U) {
        delay_ticks = 1U;
    }

    for (;;) {
        PsAppUi_SyncModeWithRuntime(ctx);

        if ((state->mode == (u8)PS_APP_UI_MODE_MENU) ||
            (state->mode == (u8)PS_APP_UI_MODE_LOADING)) {
            PsAppUi_EnsureMenuScreen();
            PsAppUi_UpdateClockLabel(ctx);
        }

        if (state->mode == (u8)PS_APP_UI_MODE_MENU) {
            if (state->refresh_requested != 0U) {
                (void)PsAppUi_RefreshGames(ctx);
            }
            PsAppUi_ProcessMenuInput(ctx);
            PsAppUi_UpdateAnimation(ctx);
        } else if (state->mode == (u8)PS_APP_UI_MODE_LOADING) {
            PsAppUi_ProcessLaunch(ctx);
        } else {
            PsAppUi_ProcessGameInput(ctx);
        }

        lv_tick_inc(PS_APP_UI_SERVICE_INTERVAL_MS);
        if ((state->mode == (u8)PS_APP_UI_MODE_MENU) ||
            (state->mode == (u8)PS_APP_UI_MODE_LOADING)) {
            (void)lv_timer_handler();
        }

        vTaskDelay(delay_ticks);
    }
}
