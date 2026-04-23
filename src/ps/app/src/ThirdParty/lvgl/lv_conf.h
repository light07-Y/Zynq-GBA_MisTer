#ifndef LV_CONF_H
#define LV_CONF_H

/* Core display/memory tuning for Zynq PS + HDMI XRGB8888 */
#define LV_COLOR_DEPTH 32
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (96U * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE 0
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 130

/* Keep runtime deterministic and lean */
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_USE_ASSERT_STR 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0
#define LV_USE_LAYER_DEBUG 0
#define LV_USE_PARALLEL_DRAW_DEBUG 0

/* Disable heavy/non-target integrations */
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0
#define LV_USE_LIBPNG 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_LIBWEBP 0
#define LV_USE_SVG 0
#define LV_USE_GSTREAMER 0
#define LV_USE_GLTF 0

/* Disable desktop/OS specific display/input drivers */
#define LV_USE_SDL 0
#define LV_USE_X11 0
#define LV_USE_WAYLAND 0
#define LV_USE_LINUX_FBDEV 0
#define LV_USE_LIBINPUT 0
#define LV_USE_EVDEV 0

/* Disable test/debug helpers not needed on target */
#define LV_USE_SYSMON 0
#define LV_USE_MONKEY 0
#define LV_USE_TEST 0

/* Keep built-in font set minimal */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_UNSCII_8 0
#define LV_FONT_UNSCII_16 0

/* Use project-integrated pixel font as default UI font */
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(ps_ui_font_fusion_12)
#define LV_FONT_DEFAULT &ps_ui_font_fusion_12

/* Keep theme deterministic: UI styles are fully owned by app code */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#include "lv_conf_template.h"

#endif /* LV_CONF_H */
