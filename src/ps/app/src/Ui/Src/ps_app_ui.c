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
#include "Ui/Assets/Fonts/ps_ui_font_fusion_12.h"
#include "Video/Inc/ps_app_video.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "lvgl.h"

#define PS_APP_UI_DRAW_BUFFER_LINES 24U
#define PS_APP_UI_TARGET_FRAME_IDX  0U

#define PS_APP_UI_PANEL_SHADOW_OFFSET 2
#define PS_APP_UI_BLINK_INTERVAL_MS   120U
#define PS_APP_UI_SWEEP_PERIOD_MS     720U
#define PS_APP_UI_SWEEP_WIDTH_PX      20U
#define PS_APP_UI_CURSOR_WIDTH_PX     6U
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
} PsAppUiGameEntry;

typedef struct {
    PsAppUiContext *ctx;
    PsAppUiGameEntry games[PS_APP_UI_MAX_GAMES];
    lv_obj_t *game_buttons[PS_APP_UI_MAX_GAMES];
    lv_display_t *display;
    lv_obj_t *screen;
    lv_obj_t *shell_panel;
    lv_obj_t *header_panel;
    lv_obj_t *title_label;
    lv_obj_t *meta_label;
    lv_obj_t *led;
    lv_obj_t *list_panel;
    lv_obj_t *list;
    lv_obj_t *status_panel;
    lv_obj_t *status_label;
    lv_obj_t *hint_label;
    lv_obj_t *about_panel;
    lv_obj_t *about_title_label;
    lv_obj_t *about_body_label;
    lv_obj_t *about_hint_label;
    lv_obj_t *sweep;
    lv_obj_t *cursor;
    u32 anim_elapsed_ms;
    u32 blink_elapsed_ms;
    u16 game_button_count;
    u8 blink_phase;
    u8 about_visible;
    u8 initialized;
} PsAppUiRuntime;

typedef struct {
    PsAppUiGameEntry *games;
    u32 count;
    u8 overflow;
} PsAppUiScanContext;

static PsAppUiRuntime s_ps_app_ui_runtime;
static u32 s_ps_app_ui_draw_buffer[PS_APP_HDMI_WIDTH * PS_APP_UI_DRAW_BUFFER_LINES];

static lv_color_t PsAppUi_HexColor(u32 rgb) {
    return lv_color_hex(rgb);
}

static void PsAppUi_CopyText(char *dst, size_t dst_size, const char *src) {
    size_t src_len;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    src_len = strlen(src);
    if (src_len >= dst_size) {
        src_len = dst_size - 1U;
    }

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

static const char *PsAppUi_ModeName(PsAppUiMode mode) {
    switch (mode) {
        case PS_APP_UI_MODE_LOADING:
            return "加载中";
        case PS_APP_UI_MODE_GAME:
            return "游戏中";
        case PS_APP_UI_MODE_MENU:
        default:
            return "菜单";
    }
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

    if (len >= dst_size) {
        len = dst_size - 1U;
    }

    memcpy(dst, base, len);
    dst[len] = '\0';
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

static void PsAppUi_SetStatus(PsAppUiContext *ctx, const char *status_text) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    PsAppUi_CopyText(ctx->state->status_line, sizeof(ctx->state->status_line), status_text);
    if (s_ps_app_ui_runtime.status_label != NULL) {
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
                   "模式: %s  游戏数: %u",
                   PsAppUi_ModeName((PsAppUiMode)ctx->state->mode),
                   (unsigned int)ctx->state->game_count);
    lv_label_set_text(s_ps_app_ui_runtime.meta_label, meta_text);
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
    lv_obj_set_style_bg_opa(shadow, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(shadow, PsAppUi_HexColor(shadow_rgb), 0);
    lv_obj_set_style_radius(shadow, 0, 0);
    lv_obj_clear_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);

    panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, PsAppUi_HexColor(bg_rgb), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, PsAppUi_HexColor(border_rgb), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
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
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(block, PsAppUi_HexColor(bg_rgb), 0);
    lv_obj_set_style_border_width(block, 2, 0);
    lv_obj_set_style_border_color(block, PsAppUi_HexColor(border_rgb), 0);
    lv_obj_set_style_border_opa(block, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(block, 0, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    lv_obj_set_scrollbar_mode(block, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    return block;
}

static void PsAppUi_BuildPixelDpad(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
    const lv_coord_t block = 14;

    (void)PsAppUi_CreatePixelBlock(parent, x + block, y, block, block, 0x303B53, 0x121A29);
    (void)PsAppUi_CreatePixelBlock(parent, x, y + block, block, block, 0x303B53, 0x121A29);
    (void)PsAppUi_CreatePixelBlock(parent, x + block, y + block, block, block, 0x3C4A67, 0x121A29);
    (void)PsAppUi_CreatePixelBlock(parent, x + (block * 2), y + block, block, block, 0x303B53, 0x121A29);
    (void)PsAppUi_CreatePixelBlock(parent, x + block, y + (block * 2), block, block, 0x303B53, 0x121A29);
}

static void PsAppUi_BuildPixelActionButtons(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t *btn_a;
    lv_obj_t *btn_b;
    lv_obj_t *label_a;
    lv_obj_t *label_b;

    btn_b = PsAppUi_CreatePixelBlock(parent, x, y + 10, 18, 18, 0x2E3A52, 0x121A29);
    btn_a = PsAppUi_CreatePixelBlock(parent, x + 24, y, 18, 18, 0x2E3A52, 0x121A29);

    label_b = lv_label_create(btn_b);
    lv_label_set_text(label_b, "B");
    lv_obj_set_style_text_font(label_b, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(label_b, PsAppUi_HexColor(0xDDE8FF), 0);
    lv_obj_center(label_b);

    label_a = lv_label_create(btn_a);
    lv_label_set_text(label_a, "A");
    lv_obj_set_style_text_font(label_a, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(label_a, PsAppUi_HexColor(0xDDE8FF), 0);
    lv_obj_center(label_a);
}

static void PsAppUi_ResetSweepObject(void) {
    s_ps_app_ui_runtime.sweep = NULL;
    s_ps_app_ui_runtime.cursor = NULL;
}

static void PsAppUi_ResetGameButtons(void) {
    memset(s_ps_app_ui_runtime.game_buttons, 0, sizeof(s_ps_app_ui_runtime.game_buttons));
    s_ps_app_ui_runtime.game_button_count = 0U;
    PsAppUi_ResetSweepObject();
}

static void PsAppUi_StyleListButton(lv_obj_t *btn) {
    if (btn == NULL) {
        return;
    }

    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 24);
    lv_obj_set_flex_grow(btn, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, PsAppUi_HexColor(0x30405E), 0);
    lv_obj_set_style_bg_color(btn, PsAppUi_HexColor(0x1C2740), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn, PsAppUi_HexColor(0xE4ECFF), 0);
    lv_obj_set_style_text_font(btn, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_right(btn, 8, 0);
    lv_obj_set_style_pad_top(btn, 4, 0);
    lv_obj_set_style_pad_bottom(btn, 4, 0);
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
        if (idx == (u32)state->selected_index) {
            selected_btn = btn;
            if (s_ps_app_ui_runtime.blink_phase == 0U) {
                lv_obj_set_style_bg_color(btn, PsAppUi_HexColor(0x4A6C9E), 0);
                lv_obj_set_style_border_color(btn, PsAppUi_HexColor(0xCDE1FF), 0);
            } else {
                lv_obj_set_style_bg_color(btn, PsAppUi_HexColor(0x3C5E8F), 0);
                lv_obj_set_style_border_color(btn, PsAppUi_HexColor(0xA8CBFF), 0);
            }
            lv_obj_set_style_text_color(btn, PsAppUi_HexColor(0xFFFFFF), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_pad_left(btn, 18, 0);
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

    /* Keep selected item visible, but avoid separate white overlay objects.
     * The previous cursor/sweep overlay could occlude title text on low-height rows. */
    (void)reset_sweep;
    lv_obj_scroll_to_view(selected_btn, LV_ANIM_OFF);
    if (s_ps_app_ui_runtime.sweep != NULL) {
        lv_obj_add_flag(s_ps_app_ui_runtime.sweep, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ps_app_ui_runtime.cursor != NULL) {
        lv_obj_add_flag(s_ps_app_ui_runtime.cursor, LV_OBJ_FLAG_HIDDEN);
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
        lv_label_set_text(empty_label, "0:/games 下没有 .gba 游戏");
        lv_obj_set_style_text_color(empty_label, PsAppUi_HexColor(0xD8E5FF), 0);
        lv_obj_set_style_text_font(empty_label, &ps_ui_font_fusion_12, 0);
        lv_obj_set_style_pad_all(empty_label, 8, 0);
    } else {
        for (idx = 0U; idx < (u32)state->game_count; ++idx) {
            lv_obj_t *btn;

            btn = lv_list_add_button(s_ps_app_ui_runtime.list, NULL, s_ps_app_ui_runtime.games[idx].name);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
            PsAppUi_StyleListButton(btn);
            s_ps_app_ui_runtime.game_buttons[idx] = btn;
            s_ps_app_ui_runtime.game_button_count = (u16)(idx + 1U);
        }
    }

    PsAppUi_UpdateMetaLabels(ctx);
    PsAppUi_UpdateListVisuals(ctx, 1U);
}

static u8 PsAppUi_ListDirectoryCallback(const char *name,
                                        const char *full_path,
                                        u8 is_dir,
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

    entry = &scan_ctx->games[scan_ctx->count];
    PsAppUi_FormatDisplayName(entry->name,
                              sizeof(entry->name),
                              (full_path != NULL) ? full_path : name);
    PsAppUi_CopyText(entry->path, sizeof(entry->path), full_path);
    scan_ctx->count++;
    return 1U;
}

static XStatus PsAppUi_RefreshGames(PsAppUiContext *ctx) {
    PsAppUiScanContext scan_ctx;
    PsAppUiState *state;
    XStatus status;
    char status_text[PS_APP_UI_GAME_NAME_MAX_CHARS];
    u32 idx;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_INVALID_PARAM;
    }

    state = ctx->state;
    scan_ctx.games = s_ps_app_ui_runtime.games;
    scan_ctx.count = 0U;
    scan_ctx.overflow = 0U;

    /* 扫描固定目录 0:/games，仅收集 .gba 文件。 */
    status = PsFatFsStorage_ListDirectory(PS_APP_GAMES_SD_DIR,
                                          PsAppUi_ListDirectoryCallback,
                                          &scan_ctx);

    state->game_count = (u8)((scan_ctx.count > 255U) ? 255U : scan_ctx.count);
    /* 菜单展示按名称排序，保证每次刷新顺序稳定。 */
    if (scan_ctx.count > 1U) {
        qsort(s_ps_app_ui_runtime.games,
              scan_ctx.count,
              sizeof(s_ps_app_ui_runtime.games[0]),
              PsAppUi_GameEntryCompare);
    }

    /* 仅打印前 8 条样本，避免串口刷屏影响交互实时性。 */
    xil_printf("[UI] scan status=%d count=%u overflow=%u\r\n",
               (int)status,
               (unsigned int)scan_ctx.count,
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
        if (state->game_count == 0U) {
            PsAppUi_SetStatus(ctx, "未找到游戏");
        } else if (scan_ctx.overflow != 0U) {
            (void)snprintf(status_text,
                           sizeof(status_text),
                           "仅显示前%u项",
                           (unsigned int)PS_APP_UI_MAX_GAMES);
            PsAppUi_SetStatus(ctx, status_text);
        } else {
            PsAppUi_SetStatus(ctx, "菜单就绪");
        }
    } else {
        /* 扫描失败但已有部分结果时仍允许进入菜单，避免“全失败感知”。 */
        if (state->game_count > 0U) {
            PsAppUi_SetStatus(ctx, "菜单就绪(部分)");
        } else {
            PsAppUi_SetStatus(ctx, "读取列表失败");
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

        if (state->game_count == 0U) {
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
            lv_obj_set_style_bg_color(s_ps_app_ui_runtime.led, PsAppUi_HexColor(0x54D675), 0);
        } else {
            lv_obj_set_style_bg_color(s_ps_app_ui_runtime.led, PsAppUi_HexColor(0x2D8E49), 0);
        }
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
    lv_obj_t *brand_label;
    lv_obj_t *about_content;
    u32 row;

    s_ps_app_ui_runtime.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_ps_app_ui_runtime.screen);
    lv_obj_set_size(s_ps_app_ui_runtime.screen, PS_APP_HDMI_WIDTH, PS_APP_HDMI_HEIGHT);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ps_app_ui_runtime.screen, PsAppUi_HexColor(0x677290), 0);
    lv_obj_set_style_radius(s_ps_app_ui_runtime.screen, 0, 0);
    lv_obj_clear_flag(s_ps_app_ui_runtime.screen, LV_OBJ_FLAG_SCROLLABLE);

    for (row = 0U; row < PS_APP_HDMI_HEIGHT; row += 16U) {
        scanline = lv_obj_create(s_ps_app_ui_runtime.screen);
        lv_obj_remove_style_all(scanline);
        lv_obj_set_size(scanline, PS_APP_HDMI_WIDTH, 2);
        lv_obj_set_pos(scanline, 0, (lv_coord_t)row);
        lv_obj_set_style_bg_opa(scanline, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(scanline, PsAppUi_HexColor(0x75809F), 0);
        lv_obj_set_style_radius(scanline, 0, 0);
        lv_obj_clear_flag(scanline, LV_OBJ_FLAG_SCROLLABLE);
    }

    s_ps_app_ui_runtime.shell_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.screen,
                                                                8,
                                                                8,
                                                                624,
                                                                464,
                                                                0xA6B0CB,
                                                                0x2E3958,
                                                                0x586584);
    (void)PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.shell_panel,
                                   12,
                                   12,
                                   600,
                                   440,
                                   0x929DBD,
                                   0x7380A2);
    (void)PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.shell_panel,
                                   16,
                                   14,
                                   592,
                                   6,
                                   0xD5DCEE,
                                   0x9FA9C5);

    s_ps_app_ui_runtime.header_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.shell_panel,
                                                                 20,
                                                                 20,
                                                                 584,
                                                                 60,
                                                                 0xDEE4F3,
                                                                 0x2A3552,
                                                                 0x5F6B8B);

    s_ps_app_ui_runtime.title_label = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_label_set_text(s_ps_app_ui_runtime.title_label, "Zynq GBA 启动菜单");
    lv_obj_set_style_text_font(s_ps_app_ui_runtime.title_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(s_ps_app_ui_runtime.title_label, PsAppUi_HexColor(0x1F2A44), 0);
    lv_obj_align(s_ps_app_ui_runtime.title_label, LV_ALIGN_TOP_LEFT, 10, 6);

    s_ps_app_ui_runtime.meta_label = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_obj_set_style_text_font(s_ps_app_ui_runtime.meta_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(s_ps_app_ui_runtime.meta_label, PsAppUi_HexColor(0x1F2A44), 0);
    lv_obj_align(s_ps_app_ui_runtime.meta_label, LV_ALIGN_BOTTOM_LEFT, 10, -6);

    s_ps_app_ui_runtime.led = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.header_panel,
                                                        546,
                                                        8,
                                                        12,
                                                        12,
                                                        0x54D675,
                                                        0x1D5C2D);
    led_text = lv_label_create(s_ps_app_ui_runtime.header_panel);
    lv_label_set_text(led_text, "MENU");
    lv_obj_set_style_text_font(led_text, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(led_text, PsAppUi_HexColor(0x2A3B5E), 0);
    lv_obj_set_pos(led_text, 494, 8);

    s_ps_app_ui_runtime.list_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.shell_panel,
                                                               20,
                                                               88,
                                                               584,
                                                               244,
                                                               0x2A3650,
                                                               0x12192A,
                                                               0x485779);
    list_title = lv_label_create(s_ps_app_ui_runtime.list_panel);
    lv_label_set_text(list_title, "游戏列表  X/START刷新  Y/SELECT关于");
    lv_obj_set_style_text_font(list_title, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(list_title, PsAppUi_HexColor(0xC7D9FC), 0);
    lv_obj_set_pos(list_title, 10, 4);

    s_ps_app_ui_runtime.list = lv_list_create(s_ps_app_ui_runtime.list_panel);
    lv_obj_remove_style_all(s_ps_app_ui_runtime.list);
    lv_obj_set_size(s_ps_app_ui_runtime.list, 568, 210);
    lv_obj_set_pos(s_ps_app_ui_runtime.list, 8, 26);
    lv_obj_set_layout(s_ps_app_ui_runtime.list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ps_app_ui_runtime.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ps_app_ui_runtime.list,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.list, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ps_app_ui_runtime.list, PsAppUi_HexColor(0x1A2336), 0);
    lv_obj_set_style_border_width(s_ps_app_ui_runtime.list, 2, 0);
    lv_obj_set_style_border_color(s_ps_app_ui_runtime.list, PsAppUi_HexColor(0x3F4F6C), 0);
    lv_obj_set_style_radius(s_ps_app_ui_runtime.list, 0, 0);
    lv_obj_set_style_pad_all(s_ps_app_ui_runtime.list, 6, 0);
    lv_obj_set_style_pad_row(s_ps_app_ui_runtime.list, 4, 0);
    lv_obj_set_scrollbar_mode(s_ps_app_ui_runtime.list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_ps_app_ui_runtime.list, PsAppUi_HexColor(0x4B5C80), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_ps_app_ui_runtime.list, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_ps_app_ui_runtime.list, 0, LV_PART_SCROLLBAR);

    s_ps_app_ui_runtime.status_panel = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.shell_panel,
                                                                 20,
                                                                 338,
                                                                 584,
                                                                 68,
                                                                 0xDEE4F3,
                                                                 0x2A3552,
                                                                 0x5F6B8B);

    s_ps_app_ui_runtime.status_label = lv_label_create(s_ps_app_ui_runtime.status_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.status_label, 568);
    lv_obj_set_style_text_font(s_ps_app_ui_runtime.status_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(s_ps_app_ui_runtime.status_label, PsAppUi_HexColor(0x1F2A44), 0);
    lv_obj_set_pos(s_ps_app_ui_runtime.status_label, 8, 6);

    s_ps_app_ui_runtime.hint_label = lv_label_create(s_ps_app_ui_runtime.status_panel);
    lv_obj_set_width(s_ps_app_ui_runtime.hint_label, 568);
    lv_label_set_text(s_ps_app_ui_runtime.hint_label,
                      "方向键上下 A启动 X/START刷新 Y/SELECT关于\n"
                      "LT长按5秒 / BTN4+BTN5长按2秒退出游戏");
    lv_obj_set_style_text_font(s_ps_app_ui_runtime.hint_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(s_ps_app_ui_runtime.hint_label, PsAppUi_HexColor(0x2A3D63), 0);
    lv_obj_set_pos(s_ps_app_ui_runtime.hint_label, 8, 34);

    PsAppUi_BuildPixelDpad(s_ps_app_ui_runtime.shell_panel, 56, 414);
    PsAppUi_BuildPixelActionButtons(s_ps_app_ui_runtime.shell_panel, 522, 412);

    brand_plate = PsAppUi_CreatePixelBlock(s_ps_app_ui_runtime.shell_panel,
                                           240,
                                           418,
                                           144,
                                           24,
                                           0xDEE4F3,
                                           0x2A3552);
    brand_label = lv_label_create(brand_plate);
    lv_label_set_text(brand_label, "PIXEL GBA");
    lv_obj_set_style_text_font(brand_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(brand_label, PsAppUi_HexColor(0x2A3D63), 0);
    lv_obj_center(brand_label);

    /* “关于”叠层：默认隐藏，按 Y 显示/关闭。
     * 注意 CreatePixelPanel 会创建“shadow + panel”两个兄弟对象；
     * 若只隐藏 panel，会遗留一块阴影遮罩。这里加一层 about_panel 作为父容器，
     * 把 shadow/panel 都挂在容器内，隐藏容器即可整体隐藏。 */
    s_ps_app_ui_runtime.about_panel = lv_obj_create(s_ps_app_ui_runtime.shell_panel);
    lv_obj_remove_style_all(s_ps_app_ui_runtime.about_panel);
    lv_obj_set_size(s_ps_app_ui_runtime.about_panel, 558, 306);
    lv_obj_set_pos(s_ps_app_ui_runtime.about_panel, 34, 96);
    lv_obj_set_style_bg_opa(s_ps_app_ui_runtime.about_panel, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_ps_app_ui_runtime.about_panel, LV_OBJ_FLAG_SCROLLABLE);

    about_content = PsAppUi_CreatePixelPanel(s_ps_app_ui_runtime.about_panel,
                                             0,
                                             0,
                                             556,
                                             304,
                                             0x1A2338,
                                             0xC9DBFF,
                                             0x41547D);
    s_ps_app_ui_runtime.about_title_label = lv_label_create(about_content);
    lv_label_set_text(s_ps_app_ui_runtime.about_title_label, "关于/致谢");
    lv_obj_set_style_text_font(s_ps_app_ui_runtime.about_title_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(s_ps_app_ui_runtime.about_title_label, PsAppUi_HexColor(0xD8E6FF), 0);
    lv_obj_set_pos(s_ps_app_ui_runtime.about_title_label, 10, 8);

    s_ps_app_ui_runtime.about_body_label = lv_label_create(about_content);
    lv_obj_set_width(s_ps_app_ui_runtime.about_body_label, 532);
    lv_label_set_long_mode(s_ps_app_ui_runtime.about_body_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_ps_app_ui_runtime.about_body_label,
                      "作者: 上海电机学院2022级\n"
                      "计算机科学与技术专业学生 潘子彧\n"
                      "\n"
                      "致谢开源库与代码:\n"
                      "1. LVGL 图形界面库\n"
                      "2. FreeRTOS 任务调度\n"
                      "3. FatFs 文件系统\n"
                      "4. CherryUSB USB Host 协议栈\n"
                      "5. Xilinx embeddedsw / Vitis 驱动代码\n"
                      "\n"
                      "相关信息请查看项目源码与对应 LICENSE。");
    lv_obj_set_style_text_font(s_ps_app_ui_runtime.about_body_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(s_ps_app_ui_runtime.about_body_label, PsAppUi_HexColor(0xD8E6FF), 0);
    lv_obj_set_pos(s_ps_app_ui_runtime.about_body_label, 10, 30);

    s_ps_app_ui_runtime.about_hint_label = lv_label_create(about_content);
    lv_label_set_text(s_ps_app_ui_runtime.about_hint_label, "Y/SELECT关闭  B返回");
    lv_obj_set_style_text_font(s_ps_app_ui_runtime.about_hint_label, &ps_ui_font_fusion_12, 0);
    lv_obj_set_style_text_color(s_ps_app_ui_runtime.about_hint_label, PsAppUi_HexColor(0x8FB8FF), 0);
    lv_obj_align(s_ps_app_ui_runtime.about_hint_label, LV_ALIGN_BOTTOM_LEFT, 10, -8);

    lv_obj_add_flag(s_ps_app_ui_runtime.about_panel, LV_OBJ_FLAG_HIDDEN);
    s_ps_app_ui_runtime.about_visible = 0U;

    PsAppUi_UpdateMetaLabels(ctx);
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
