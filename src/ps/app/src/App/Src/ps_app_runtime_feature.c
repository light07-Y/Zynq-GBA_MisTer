#include "App/Inc/ps_app_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"
#include "sleep.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xiltimer.h"

#include "Gba/Inc/ps_gba_regs.h"
#include "Input/Inc/ps_xinput_xbox360.h"
#include "Save/Inc/ps_app_save.h"
#include "Storage/Inc/ps_fatfs_storage.h"
#include "UsbHost/Inc/ps_app_usbhost.h"

/*
 * PS 侧缓存上限。注意：core 侧 gba_cheats.vhd 当前 CHEATCOUNT=32。
 * 因此 PS 可以维护更多条目并持久化到 .gcht，但单次回放到 core 后，
 * 真正常驻并参与每帧执行的条目数量受 core 上限限制。
 */
#define PS_APP_FEATURE_CHEAT_MAX_ENTRIES 256U
#define PS_APP_FEATURE_REWIND_SLOT       7U
#define PS_APP_FEATURE_CHEAT_OP_EQ       0U
#define PS_APP_FEATURE_CHEAT_OP_NE       1U
#define PS_APP_FEATURE_CHEAT_OP_LT       2U
#define PS_APP_FEATURE_CHEAT_OP_LE       3U
#define PS_APP_FEATURE_CHEAT_OP_GT       4U
#define PS_APP_FEATURE_CHEAT_OP_GE       5U
#define PS_APP_FEATURE_RTC2_MAGIC        0x32435452U /* "RTC2" */
#define PS_APP_FEATURE_RTC2_VERSION      2U
#define PS_APP_FEATURE_RTC2_FLAG_HAS_HISTORY  (1U << 0U)
#define PS_APP_FEATURE_RTC2_FLAG_HAS_CAL      (1U << 1U)
#define PS_APP_FEATURE_CKPT_MAGIC        0x434B5054U
#define PS_APP_FEATURE_RUMBLE_LARGE_ON   0xC0U
#define PS_APP_FEATURE_RUMBLE_SMALL_ON   0x60U
#define PS_APP_FEATURE_TRIGGER_THRESH    80U
#define PS_APP_FEATURE_SENSOR_DEADZONE   4096
#define PS_APP_FEATURE_SENSOR_ECHO_RETRY 6U
#define PS_APP_FEATURE_RTC_PERSIST_INTERVAL_MS 30000U
#define PS_APP_FEATURE_RTC_SEED_PATH     "0:/rtc/time_seed.txt"
#define PS_APP_FEATURE_RTC_GLOBAL_PATH   "0:/rtc/system.rtc2"
#define PS_APP_FEATURE_RTC_DEFAULT_SYNC_UNCERT_S 1U
#define PS_APP_FEATURE_RTC_DEFAULT_SEED_UNCERT_S 5U
#define PS_APP_FEATURE_RTC_PPM_ABS_MAX_PPB 200000
#define PS_APP_FEATURE_RTC_SIGMA_MIN_PPB 5000U
#define PS_APP_FEATURE_RTC_SIGMA_MAX_PPB 200000U
#define PS_APP_FEATURE_RTC_DEFAULT_SIGMA_PPB 50000U
#define PS_APP_FEATURE_RTC_CAL_MIN_INTERVAL_US (300ULL * 1000000ULL)
#define PS_APP_FEATURE_RTC_JITTER_US     10000ULL
#define PS_APP_FEATURE_RTC_UNCERT_MAX_S  2592000U
#define PS_APP_FEATURE_RTC_BJ_OFFSET_SEC 28800LL

#define PS_APP_GBA_KEY_SELECT_BIT (1U << 2U)
#define PS_APP_GBA_KEY_RIGHT_BIT  (1U << 4U)
#define PS_APP_GBA_KEY_LEFT_BIT   (1U << 5U)
#define PS_APP_GBA_KEY_UP_BIT     (1U << 6U)
#define PS_APP_GBA_KEY_DOWN_BIT   (1U << 7U)

typedef struct {
    u32 magic;
    u32 head;
} PsAppCheckpointMeta;

typedef struct {
    u32 magic;
    u16 version;
    u16 flags;
    u32 saved_unix;
    u32 savedtime_lo;
    u16 savedtime_hi;
    u16 reserved0;
    s32 ppm_cal_ppb;
    u32 ppm_sigma_ppb;
    u32 last_cal_unix;
    u32 base_uncert_s;
    u32 crc32;
} PsAppRtcPersistDataV2;

static PsGbaCheatCodeWords s_ps_app_feature_cheat_entries[PS_APP_FEATURE_CHEAT_MAX_ENTRIES];
static u32 s_ps_app_feature_cheat_count = 0U;
/*
 * 重要说明（防踩坑）：
 * "cons" 控制台任务栈空间有限。历史版本在 SaveCheatsToFile() 里使用了
 * 10KB 级别的局部 text_buf，执行 "cheat add ..." 时会在控制台任务上下文内
 * 触发栈溢出（[RTOS][FATAL] stack overflow task=cons）。
 *
 * 这里将大缓冲改为静态区，避免大块临时内存占用任务栈。
 * cheat 文件读写由运行时路径串行触发，不做并发重入设计。
 */
static char s_ps_app_feature_cheat_text_buf[PS_APP_FEATURE_CHEAT_MAX_ENTRIES * 40U];

static u32 PsAppRuntimeFeature_NowMs(void) {
    TickType_t ticks;

    ticks = xTaskGetTickCount();
    return (u32)(ticks * portTICK_PERIOD_MS);
}

static void PsAppRuntimeFeature_DelayMs(u32 ms) {
    TickType_t delay_ticks;

    if (ms == 0U) {
        return;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        delay_ticks = pdMS_TO_TICKS(ms);
        if (delay_ticks == 0U) {
            delay_ticks = 1U;
        }
        vTaskDelay(delay_ticks);
        return;
    }

    usleep(ms * 1000U);
}

static s8 PsAppRuntimeFeature_S16ToS8(s16 value) {
    s16 shifted;

    if ((value > -PS_APP_FEATURE_SENSOR_DEADZONE) &&
        (value < PS_APP_FEATURE_SENSOR_DEADZONE)) {
        return 0;
    }

    shifted = value >> 8;
    if (shifted > 127) {
        shifted = 127;
    } else if (shifted < -128) {
        shifted = -128;
    }
    return (s8)shifted;
}

static u64 PsAppRuntimeFeature_NowMetUs(void) {
    /* 使用 FreeRTOS tick 作为 MET 的统一时基，避免平台计数频率宏不一致导致
     * UTC 估计“慢走”（现场已观测到约 4x 误差）。
     * 这里做一个轻量 64-bit 扩展，跨 TickType_t 回绕后仍保持单调。 */
    static TickType_t s_last_ticks = 0U;
    static u64 s_tick_wrap_base = 0ULL;
    TickType_t now_ticks;
    u64 ticks_ext;

    taskENTER_CRITICAL();
    now_ticks = xTaskGetTickCount();
    if (now_ticks < s_last_ticks) {
        s_tick_wrap_base += (1ULL << (sizeof(TickType_t) * 8U));
    }
    s_last_ticks = now_ticks;
    ticks_ext = s_tick_wrap_base + (u64)now_ticks;
    taskEXIT_CRITICAL();

    return ticks_ext * (u64)portTICK_PERIOD_MS * 1000ULL;
}

static s32 PsAppRuntimeFeature_ClampPpmPpb(s32 ppm_ppb) {
    if (ppm_ppb > (s32)PS_APP_FEATURE_RTC_PPM_ABS_MAX_PPB) {
        return (s32)PS_APP_FEATURE_RTC_PPM_ABS_MAX_PPB;
    }
    if (ppm_ppb < -(s32)PS_APP_FEATURE_RTC_PPM_ABS_MAX_PPB) {
        return -(s32)PS_APP_FEATURE_RTC_PPM_ABS_MAX_PPB;
    }
    return ppm_ppb;
}

static u32 PsAppRuntimeFeature_ClampSigmaPpb(u32 sigma_ppb) {
    if (sigma_ppb < PS_APP_FEATURE_RTC_SIGMA_MIN_PPB) {
        return PS_APP_FEATURE_RTC_SIGMA_MIN_PPB;
    }
    if (sigma_ppb > PS_APP_FEATURE_RTC_SIGMA_MAX_PPB) {
        return PS_APP_FEATURE_RTC_SIGMA_MAX_PPB;
    }
    return sigma_ppb;
}

static u32 PsAppRuntimeFeature_ClampUncertS(u32 uncert_s) {
    if (uncert_s > PS_APP_FEATURE_RTC_UNCERT_MAX_S) {
        return PS_APP_FEATURE_RTC_UNCERT_MAX_S;
    }
    return uncert_s;
}

static u64 PsAppRuntimeFeature_SecondsToUs(u32 seconds) {
    return (u64)seconds * 1000000ULL;
}

static u32 PsAppRuntimeFeature_UsToUnixSeconds(u64 utc_us) {
    u64 unix_sec = utc_us / 1000000ULL;
    if (unix_sec > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFU;
    }
    return (u32)unix_sec;
}

static void PsAppRuntimeFeature_UnixToDateTime(s64 unix_sec,
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
    u8 hour;
    u8 minute;
    u8 second;

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
    doe = (u32)(z - (era * 146097LL));
    yoe = (doe - (doe / 1460U) + (doe / 36524U) - (doe / 146096U)) / 365U;
    y = (s64)yoe + (era * 400LL);
    doy = doe - (365U * yoe + (yoe / 4U) - (yoe / 100U));
    mp = (5U * doy + 2U) / 153U;
    day = doy - (153U * mp + 2U) / 5U + 1U;
    month = (s32)mp + (((s32)mp < 10) ? 3 : -9);
    if (month <= 2) {
        y += 1LL;
    }

    hour = (u8)(rem / 3600LL);
    minute = (u8)((rem % 3600LL) / 60LL);
    second = (u8)(rem % 60LL);

    *year_out = (s32)y;
    *month_out = (u8)month;
    *day_out = (u8)day;
    *hour_out = hour;
    *min_out = minute;
    *sec_out = second;
}

static void PsAppRuntimeFeature_PrintRtcUnixDual(const char *tag, u32 unix_sec) {
    s32 utc_year;
    s32 bj_year;
    u8 utc_month;
    u8 utc_day;
    u8 utc_hour;
    u8 utc_min;
    u8 utc_sec;
    u8 bj_month;
    u8 bj_day;
    u8 bj_hour;
    u8 bj_min;
    u8 bj_sec;

    PsAppRuntimeFeature_UnixToDateTime((s64)unix_sec,
                                       0LL,
                                       &utc_year,
                                       &utc_month,
                                       &utc_day,
                                       &utc_hour,
                                       &utc_min,
                                       &utc_sec);
    PsAppRuntimeFeature_UnixToDateTime((s64)unix_sec,
                                       PS_APP_FEATURE_RTC_BJ_OFFSET_SEC,
                                       &bj_year,
                                       &bj_month,
                                       &bj_day,
                                       &bj_hour,
                                       &bj_min,
                                       &bj_sec);

    xil_printf("[RTC] %s unix=%u UTC=%04d-%02u-%02u %02u:%02u:%02u BJ=%04d-%02u-%02u %02u:%02u:%02u\r\n",
               tag != NULL ? tag : "time",
               (unsigned int)unix_sec,
               (int)utc_year,
               (unsigned int)utc_month,
               (unsigned int)utc_day,
               (unsigned int)utc_hour,
               (unsigned int)utc_min,
               (unsigned int)utc_sec,
               (int)bj_year,
               (unsigned int)bj_month,
               (unsigned int)bj_day,
               (unsigned int)bj_hour,
               (unsigned int)bj_min,
               (unsigned int)bj_sec);
}

static s64 PsAppRuntimeFeature_ElapsedPpbToUs(u64 elapsed_us, s32 ppb) {
    s64 sec_term;
    s64 frac_term;

    sec_term = ((s64)(elapsed_us / 1000000ULL) * (s64)ppb) / 1000LL;
    frac_term = (((s64)(elapsed_us % 1000000ULL)) * (s64)ppb) / 1000000000LL;
    return sec_term + frac_term;
}

static u64 PsAppRuntimeFeature_ElapsedSigmaToUs(u64 elapsed_us, u32 sigma_ppb) {
    u64 sec_term;
    u64 frac_term;

    sec_term = ((elapsed_us / 1000000ULL) * (u64)sigma_ppb) / 1000ULL;
    frac_term = ((elapsed_us % 1000000ULL) * (u64)sigma_ppb) / 1000000000ULL;
    return sec_term + frac_term;
}

static u64 PsAppRuntimeFeature_RtcEstimateUtcUs(const PsAppRtcPersistState *rtc, u64 now_met_us) {
    u64 elapsed_us;
    u64 utc_us;
    s64 drift_us;

    if ((rtc == NULL) || (rtc->model_ready == 0U)) {
        return 0ULL;
    }

    elapsed_us = now_met_us - rtc->anchor_met_us;
    utc_us = rtc->anchor_utc_us + elapsed_us;
    drift_us = PsAppRuntimeFeature_ElapsedPpbToUs(elapsed_us, rtc->ppm_cal_ppb);
    if (drift_us >= 0) {
        utc_us += (u64)drift_us;
    } else {
        u64 drift_abs = (u64)(-drift_us);
        if (utc_us > drift_abs) {
            utc_us -= drift_abs;
        } else {
            utc_us = 0ULL;
        }
    }
    return utc_us;
}

static u64 PsAppRuntimeFeature_RtcEstimateUncertaintyUs(const PsAppRtcPersistState *rtc, u64 now_met_us) {
    u64 elapsed_us;
    u64 uncert_us;

    if ((rtc == NULL) || (rtc->model_ready == 0U) || (rtc->uncert_infinite != 0U)) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    elapsed_us = now_met_us - rtc->anchor_met_us;
    uncert_us = rtc->base_uncert_us;
    uncert_us += PsAppRuntimeFeature_ElapsedSigmaToUs(elapsed_us, rtc->ppm_sigma_ppb);
    uncert_us += PS_APP_FEATURE_RTC_JITTER_US;
    return uncert_us;
}

static void PsAppRuntimeFeature_RtcModelInit(PsAppRuntimeContext *ctx, u64 anchor_met_us, u64 anchor_utc_us) {
    if ((ctx == NULL) || (ctx->rtc == NULL)) {
        return;
    }

    ctx->rtc->model_ready = 1U;
    ctx->rtc->anchor_met_us = anchor_met_us;
    ctx->rtc->anchor_utc_us = anchor_utc_us;
    ctx->rtc->last_current_unix = PsAppRuntimeFeature_UsToUnixSeconds(anchor_utc_us);
    ctx->rtc->last_uncert_us = 0U;
}

static void PsAppRuntimeFeature_WriteSensorInputRaw(PsAppRuntimeContext *ctx) {
    if ((ctx == NULL) || (ctx->feature == NULL) || (ctx->regs == NULL)) {
        return;
    }

    PsGbaRegs_SetSensorInput(ctx->regs,
                             ctx->feature->solar_level,
                             ctx->feature->tilt_x,
                             ctx->feature->tilt_y);

    if (ctx->sensor != NULL) {
        ctx->sensor->solar = (u8)(ctx->feature->solar_level & 0x7U);
        ctx->sensor->tilt_x = ctx->feature->tilt_x;
        ctx->sensor->tilt_y = ctx->feature->tilt_y;
        ctx->sensor->sensor_update_count++;
    }
}

static void PsAppRuntimeFeature_WriteSolarAndVerify(PsAppRuntimeContext *ctx, u8 verbose_ok_log) {
    u8 desired_solar;
    u8 reg_solar;
    u8 echo_solar;
    u32 status;
    u32 sensor_raw;
    u32 retry;

    if ((ctx == NULL) || (ctx->feature == NULL) || (ctx->regs == NULL)) {
        return;
    }

    desired_solar = (u8)(ctx->feature->solar_level & 0x7U);
    echo_solar = 0xFFU;

    PsAppRuntimeFeature_WriteSensorInputRaw(ctx);

    sensor_raw = PsGbaRegs_Read(ctx->regs, GBA_REG_SENSOR_INPUT);
    reg_solar = (u8)(sensor_raw & 0x7U);
    for (retry = 0U; retry < PS_APP_FEATURE_SENSOR_ECHO_RETRY; ++retry) {
        status = PsGbaRegs_ReadFeatureStatus(ctx->regs);
        echo_solar = (u8)((status & GBA_FEATURE_STATUS_SOLAR_ECHO_MASK) >>
                          GBA_FEATURE_STATUS_SOLAR_ECHO_SHIFT);
        if (echo_solar == desired_solar) {
            break;
        }
    }

    if ((reg_solar != desired_solar) || (echo_solar != desired_solar)) {
        /* 兜底重写一次：规避极端时序下的寄存器采样不一致。 */
        PsAppRuntimeFeature_WriteSensorInputRaw(ctx);
        sensor_raw = PsGbaRegs_Read(ctx->regs, GBA_REG_SENSOR_INPUT);
        reg_solar = (u8)(sensor_raw & 0x7U);
        status = PsGbaRegs_ReadFeatureStatus(ctx->regs);
        echo_solar = (u8)((status & GBA_FEATURE_STATUS_SOLAR_ECHO_MASK) >>
                          GBA_FEATURE_STATUS_SOLAR_ECHO_SHIFT);

        xil_printf("[SENSOR] sync des=%u reg=%u echo=%u ctrl_gpio=%u\r\n",
                   (unsigned int)desired_solar,
                   (unsigned int)reg_solar,
                   (unsigned int)echo_solar,
                   (unsigned int)((ctx->config != NULL) &&
                                  ((ctx->config->ctrl & GBA_CTRL_SPECIAL_GPIO) != 0U)));
        return;
    }

    if (verbose_ok_log != 0U) {
        xil_printf("[SENSOR] solar=%u reg=%u echo=%u\r\n",
                   (unsigned int)desired_solar,
                   (unsigned int)reg_solar,
                   (unsigned int)echo_solar);
    }
}

static void PsAppRuntimeFeature_EnsureSolarGpioRouting(PsAppRuntimeContext *ctx) {
    if ((ctx == NULL) || (ctx->rom == NULL) || (ctx->config == NULL)) {
        return;
    }
    if (ctx->rom->quirk_solar == 0U) {
        return;
    }

    if ((ctx->config->ctrl & GBA_CTRL_SPECIAL_GPIO) == 0U) {
        /* Boktai 类 ROM 的光照传感器经由 gamepak GPIO 地址窗口访问，
         * 若 SPECIAL_GPIO 位被覆盖清零，游戏会表现为“传感器无响应”。 */
        ctx->config->ctrl |= GBA_CTRL_SPECIAL_GPIO;
        PsAppRuntime_ApplyShadowConfig(ctx);
        xil_printf("[SENSOR] force special-gpio route\r\n");
    }
}

static u8 PsAppRuntimeFeature_EnsureFeatureCtrl(PsAppRuntimeContext *ctx) {
    u32 feature_ctrl;

    if ((ctx == NULL) || (ctx->feature == NULL)) {
        return 0U;
    }

    feature_ctrl = 0U;
    if (ctx->feature->rewind_enabled != 0U) {
        feature_ctrl |= GBA_FEATURE_CTRL_REWIND_ON;
    }
    if (ctx->feature->cheats_enabled != 0U) {
        feature_ctrl |= GBA_FEATURE_CTRL_CHEATS_ENABLED;
    }
    feature_ctrl |= (GBA_FEATURE_CTRL_RUMBLE_FORWARD_EN |
                     GBA_FEATURE_CTRL_SENSOR_AUTO_EN |
                     GBA_FEATURE_CTRL_RTC_AUTO_UPDATE_EN);
    if (ctx->feature->rewind_active != 0U) {
        feature_ctrl |= GBA_FEATURE_CTRL_REWIND_ACTIVE_REQ;
    }

    PsGbaRegs_SetFeatureCtrl(ctx->regs, feature_ctrl);
    PsGbaRegs_SetSavestateSlot(ctx->regs, ctx->feature->savestate_slot);
    PsAppRuntimeFeature_WriteSensorInputRaw(ctx);
    return 1U;
}

static u32 PsAppRuntimeFeature_Crc32(const u8 *data, u32 bytes) {
    u32 crc;
    u32 idx;
    u32 bit;

    if ((data == NULL) || (bytes == 0U)) {
        return 0U;
    }

    crc = 0xFFFFFFFFU;
    for (idx = 0U; idx < bytes; ++idx) {
        crc ^= data[idx];
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static void PsAppRuntimeFeature_BuildRomId(PsAppRuntimeContext *ctx) {
    const u8 *rom_ptr;
    u32 crc32;

    if ((ctx == NULL) || (ctx->feature == NULL) || (ctx->rom == NULL)) {
        return;
    }

    rom_ptr = (const u8 *)PS_APP_GBA_ROM_REGION_BASE_ADDR;
    crc32 = PsAppRuntimeFeature_Crc32(rom_ptr, ctx->rom->size_bytes);
    ctx->feature->rom_crc32 = crc32;

    snprintf(ctx->feature->rom_id,
             sizeof(ctx->feature->rom_id),
             "%s_%08X",
             (ctx->rom->game_code[0] != '\0') ? ctx->rom->game_code : "UNKN",
             (unsigned int)crc32);
}

static void PsAppRuntimeFeature_BuildSavestateDir(const PsAppRuntimeContext *ctx,
                                                  char *out_path,
                                                  u32 out_size) {
    if ((ctx == NULL) || (ctx->feature == NULL) || (out_path == NULL) || (out_size == 0U)) {
        return;
    }

    snprintf(out_path,
             out_size,
             "%s/%s",
             PS_APP_SAVESTATE_SD_DIR,
             ctx->feature->rom_id);
}

static void PsAppRuntimeFeature_BuildSavestatePath(const PsAppRuntimeContext *ctx,
                                                   u32 slot,
                                                   char *out_path,
                                                   u32 out_size) {
    char dir_path[PS_APP_ROM_PATH_MAX_CHARS];
    char suffix[20];
    size_t dir_len;
    size_t suffix_len;

    if ((out_path == NULL) || (out_size == 0U)) {
        return;
    }
    out_path[0] = '\0';
    PsAppRuntimeFeature_BuildSavestateDir(ctx, dir_path, sizeof(dir_path));
    if (snprintf(suffix, sizeof(suffix), "/slot%u.ss", (unsigned int)slot) <= 0) {
        return;
    }

    dir_len = strlen(dir_path);
    suffix_len = strlen(suffix);
    if ((dir_len + suffix_len) >= (size_t)out_size) {
        return;
    }

    memcpy(out_path, dir_path, dir_len);
    memcpy(out_path + dir_len, suffix, suffix_len + 1U);
}

static void PsAppRuntimeFeature_BuildCheckpointPath(const PsAppRuntimeContext *ctx,
                                                    u32 index,
                                                    char *out_path,
                                                    u32 out_size) {
    char dir_path[PS_APP_ROM_PATH_MAX_CHARS];
    char suffix[20];
    size_t dir_len;
    size_t suffix_len;

    if ((out_path == NULL) || (out_size == 0U)) {
        return;
    }
    out_path[0] = '\0';
    PsAppRuntimeFeature_BuildSavestateDir(ctx, dir_path, sizeof(dir_path));
    if (snprintf(suffix, sizeof(suffix), "/ckpt%03u.ss", (unsigned int)index) <= 0) {
        return;
    }

    dir_len = strlen(dir_path);
    suffix_len = strlen(suffix);
    if ((dir_len + suffix_len) >= (size_t)out_size) {
        return;
    }

    memcpy(out_path, dir_path, dir_len);
    memcpy(out_path + dir_len, suffix, suffix_len + 1U);
}

static void PsAppRuntimeFeature_BuildCheckpointMetaPath(const PsAppRuntimeContext *ctx,
                                                        char *out_path,
                                                        u32 out_size) {
    char dir_path[PS_APP_ROM_PATH_MAX_CHARS];
    const char *suffix = "/ckpt.meta";
    size_t dir_len;
    size_t suffix_len;

    if ((out_path == NULL) || (out_size == 0U)) {
        return;
    }
    out_path[0] = '\0';
    PsAppRuntimeFeature_BuildSavestateDir(ctx, dir_path, sizeof(dir_path));
    dir_len = strlen(dir_path);
    suffix_len = strlen(suffix);
    if ((dir_len + suffix_len) >= (size_t)out_size) {
        return;
    }

    memcpy(out_path, dir_path, dir_len);
    memcpy(out_path + dir_len, suffix, suffix_len + 1U);
}

static void PsAppRuntimeFeature_BuildCheatPath(const PsAppRuntimeContext *ctx,
                                               char *out_path,
                                               u32 out_size) {
    if ((ctx == NULL) || (ctx->feature == NULL) || (out_path == NULL) || (out_size == 0U)) {
        return;
    }

    snprintf(out_path,
             out_size,
             "%s/%s.gcht",
             PS_APP_CHEAT_SD_DIR,
             ctx->feature->rom_id);
}

static void PsAppRuntimeFeature_BuildRtcPath(const PsAppRuntimeContext *ctx,
                                             char *out_path,
                                             u32 out_size) {
    if ((ctx == NULL) || (ctx->feature == NULL) || (out_path == NULL) || (out_size == 0U)) {
        return;
    }

    snprintf(out_path,
             out_size,
             "%s/%s.rtc",
             PS_APP_RTC_SD_DIR,
             ctx->feature->rom_id);
}

static XStatus PsAppRuntimeFeature_EnsureDirectories(const PsAppRuntimeContext *ctx) {
    char dir_path[PS_APP_ROM_PATH_MAX_CHARS];

    if ((ctx == NULL) || (ctx->feature == NULL)) {
        return XST_FAILURE;
    }

    if (PsFatFsStorage_EnsureDirectory(PS_APP_SAVESTATE_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (PsFatFsStorage_EnsureDirectory(PS_APP_CHEAT_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (PsFatFsStorage_EnsureDirectory(PS_APP_RTC_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    PsAppRuntimeFeature_BuildSavestateDir(ctx, dir_path, sizeof(dir_path));
    if (PsFatFsStorage_EnsureDirectory(dir_path) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

static UINTPTR PsAppRuntimeFeature_SlotAddr(u32 slot) {
    return (UINTPTR)(PS_APP_GBA_SAVESTATE_REGION_BASE_ADDR +
                     ((slot & 0x7U) * PS_APP_GBA_SAVESTATE_SLOT_BYTES));
}

static XStatus PsAppRuntimeFeature_WaitLoadDone(PsAppRuntimeContext *ctx, u32 timeout_ms) {
    u32 start_ms;
    u32 status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    start_ms = PsAppRuntimeFeature_NowMs();
    while ((PsAppRuntimeFeature_NowMs() - start_ms) < timeout_ms) {
        status = PsGbaRegs_ReadFeatureStatus(ctx->regs);
        if ((status & GBA_FEATURE_STATUS_LOAD_DONE_LATCHED) != 0U) {
            PsGbaRegs_ClearFeatureStatus(ctx->regs, 1U);
            return XST_SUCCESS;
        }
        PsAppRuntimeFeature_DelayMs(1U);
    }

    return XST_FAILURE;
}

static XStatus PsAppRuntimeFeature_SaveSlotToPath(PsAppRuntimeContext *ctx,
                                                  u32 slot,
                                                  const char *path) {
    PsFatFsStorageWriteResult write_result;
    u32 start_ms;
    u32 status;
    u8 seen_busy;

    if ((ctx == NULL) || (path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    PsGbaRegs_SetSavestateSlot(ctx->regs, slot);
    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_SAVE_TRIG);

    /* 等 core 完成“busy 拉高再拉低”完整握手后再读 DDR。
     * 仅等待 busy=0 不够安全，可能误命中“还没开始”的空窗。 */
    start_ms = PsAppRuntimeFeature_NowMs();
    seen_busy = 0U;
    while ((PsAppRuntimeFeature_NowMs() - start_ms) < 4000U) {
        status = PsGbaRegs_ReadFeatureStatus(ctx->regs);
        if ((status & GBA_FEATURE_STATUS_SAVESTATE_BUSY) != 0U) {
            seen_busy = 1U;
        } else if (seen_busy != 0U) {
            break;
        }
        PsAppRuntimeFeature_DelayMs(1U);
    }

    Xil_DCacheFlushRange((INTPTR)PsAppRuntimeFeature_SlotAddr(slot),
                         PS_APP_GBA_SAVESTATE_SLOT_BYTES);
    memset(&write_result, 0, sizeof(write_result));
    return PsFatFsStorage_WriteMemoryToFile(path,
                                            PsAppRuntimeFeature_SlotAddr(slot),
                                            PS_APP_GBA_SAVESTATE_SLOT_BYTES,
                                            &write_result);
}

static XStatus PsAppRuntimeFeature_LoadSlotFromPath(PsAppRuntimeContext *ctx,
                                                    u32 slot,
                                                    const char *path) {
    PsFatFsStorageReadResult read_result;
    XStatus status;

    if ((ctx == NULL) || (path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    memset(&read_result, 0, sizeof(read_result));
    status = PsFatFsStorage_ReadFileToMemory(path,
                                             PsAppRuntimeFeature_SlotAddr(slot),
                                             PS_APP_GBA_SAVESTATE_SLOT_BYTES,
                                             &read_result);
    if (status != XST_SUCCESS) {
        return status;
    }

    Xil_DCacheFlushRange((INTPTR)PsAppRuntimeFeature_SlotAddr(slot),
                         PS_APP_GBA_SAVESTATE_SLOT_BYTES);
    PsGbaRegs_SetSavestateSlot(ctx->regs, slot);
    /* 先清旧 load_done，再触发新 load，避免读到上一次残留锁存位。 */
    PsGbaRegs_ClearFeatureStatus(ctx->regs, 1U);
    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_LOAD_TRIG);
    return PsAppRuntimeFeature_WaitLoadDone(ctx, 5000U);
}

static XStatus PsAppRuntimeFeature_SaveCheckpointMeta(const PsAppRuntimeContext *ctx) {
    PsAppCheckpointMeta meta;
    PsFatFsStorageWriteResult write_result;
    char meta_path[PS_APP_ROM_PATH_MAX_CHARS];

    if ((ctx == NULL) || (ctx->feature == NULL)) {
        return XST_FAILURE;
    }

    meta.magic = PS_APP_FEATURE_CKPT_MAGIC;
    meta.head = ctx->feature->checkpoint_head;
    PsAppRuntimeFeature_BuildCheckpointMetaPath(ctx, meta_path, sizeof(meta_path));
    memset(&write_result, 0, sizeof(write_result));
    return PsFatFsStorage_WriteMemoryToFile(meta_path,
                                            (UINTPTR)&meta,
                                            (u32)sizeof(meta),
                                            &write_result);
}

static XStatus PsAppRuntimeFeature_LoadCheckpointMeta(PsAppRuntimeContext *ctx) {
    PsAppCheckpointMeta meta;
    PsFatFsStorageReadResult read_result;
    char meta_path[PS_APP_ROM_PATH_MAX_CHARS];

    if ((ctx == NULL) || (ctx->feature == NULL)) {
        return XST_FAILURE;
    }

    PsAppRuntimeFeature_BuildCheckpointMetaPath(ctx, meta_path, sizeof(meta_path));
    memset(&read_result, 0, sizeof(read_result));
    if (PsFatFsStorage_ReadFileToMemory(meta_path,
                                        (UINTPTR)&meta,
                                        (u32)sizeof(meta),
                                        &read_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if ((meta.magic != PS_APP_FEATURE_CKPT_MAGIC) ||
        (meta.head >= PS_APP_REWIND_CKPT_COUNT)) {
        return XST_FAILURE;
    }

    ctx->feature->checkpoint_head = meta.head;
    return XST_SUCCESS;
}

static XStatus PsAppRuntimeFeature_SaveOneCheckpoint(PsAppRuntimeContext *ctx) {
    char ckpt_path[PS_APP_ROM_PATH_MAX_CHARS];
    XStatus status;

    if ((ctx == NULL) || (ctx->feature == NULL)) {
        return XST_FAILURE;
    }

    PsAppRuntimeFeature_BuildCheckpointPath(ctx,
                                            ctx->feature->checkpoint_head,
                                            ckpt_path,
                                            sizeof(ckpt_path));
    status = PsAppRuntimeFeature_SaveSlotToPath(ctx, PS_APP_FEATURE_REWIND_SLOT, ckpt_path);
    if (status == XST_SUCCESS) {
        ctx->feature->checkpoint_head = (ctx->feature->checkpoint_head + 1U) % PS_APP_REWIND_CKPT_COUNT;
        (void)PsAppRuntimeFeature_SaveCheckpointMeta(ctx);
    }
    return status;
}

static XStatus PsAppRuntimeFeature_RestoreLatestCheckpoint(PsAppRuntimeContext *ctx) {
    char ckpt_path[PS_APP_ROM_PATH_MAX_CHARS];
    u32 latest_index;
    u32 probe_count;

    if ((ctx == NULL) || (ctx->feature == NULL)) {
        return XST_FAILURE;
    }

    if (PsAppRuntimeFeature_LoadCheckpointMeta(ctx) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    latest_index = (ctx->feature->checkpoint_head + PS_APP_REWIND_CKPT_COUNT - 1U) %
                   PS_APP_REWIND_CKPT_COUNT;

    for (probe_count = 0U; probe_count < PS_APP_REWIND_CKPT_COUNT; ++probe_count) {
        PsAppRuntimeFeature_BuildCheckpointPath(ctx, latest_index, ckpt_path, sizeof(ckpt_path));
        if (PsAppRuntimeFeature_LoadSlotFromPath(ctx, PS_APP_FEATURE_REWIND_SLOT, ckpt_path) == XST_SUCCESS) {
            xil_printf("[REWIND] auto-restore checkpoint idx=%u\r\n", (unsigned int)latest_index);
            return XST_SUCCESS;
        }
        if (latest_index == 0U) {
            latest_index = PS_APP_REWIND_CKPT_COUNT - 1U;
        } else {
            latest_index--;
        }
    }

    return XST_FAILURE;
}

static int PsAppRuntimeFeature_HexNibble(char ch) {
    if ((ch >= '0') && (ch <= '9')) {
        return (int)(ch - '0');
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        return 10 + (int)(ch - 'a');
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        return 10 + (int)(ch - 'A');
    }
    return -1;
}

static int PsAppRuntimeFeature_ParseHex32(const char *text, u32 *out_value) {
    u32 value;
    u32 idx;
    int nibble;

    if ((text == NULL) || (out_value == NULL)) {
        return -1;
    }

    value = 0U;
    for (idx = 0U; idx < 8U; ++idx) {
        nibble = PsAppRuntimeFeature_HexNibble(text[idx]);
        if (nibble < 0) {
            return -1;
        }
        value = (value << 4) | (u32)nibble;
    }

    *out_value = value;
    return 0;
}

static XStatus PsAppRuntimeFeature_SaveCheatsToFile(PsAppRuntimeContext *ctx) {
    char cheat_path[PS_APP_ROM_PATH_MAX_CHARS];
    PsFatFsStorageWriteResult write_result;
    u32 idx;
    u32 off;

    if ((ctx == NULL) || (ctx->feature == NULL)) {
        return XST_FAILURE;
    }

    /* BuildCheatPath may no-op if context is incomplete; keep a defensive guard
     * so we never pass an uninitialized path into storage APIs. */
    cheat_path[0] = '\0';
    PsAppRuntimeFeature_BuildCheatPath(ctx, cheat_path, sizeof(cheat_path));
    if (cheat_path[0] == '\0') {
        return XST_FAILURE;
    }
    if (s_ps_app_feature_cheat_count == 0U) {
        /* Delete through storage wrapper (with global FatFs mutex) instead of
         * direct f_unlink/f_stat in runtime code. */
        (void)PsFatFsStorage_DeleteFileIfExists(cheat_path, NULL);
        return XST_SUCCESS;
    }

    off = 0U;
    memset(s_ps_app_feature_cheat_text_buf, 0, sizeof(s_ps_app_feature_cheat_text_buf));
    for (idx = 0U; idx < s_ps_app_feature_cheat_count; ++idx) {
        int wrote;
        size_t remaining;

        remaining = sizeof(s_ps_app_feature_cheat_text_buf) - off;

        /*
         * 持久化格式（每行一条 128bit 原始码）：
         *   flags(8hex) + addr(8hex) + compare(8hex) + replace(8hex) + '\n'
         * 即 33 字节/行。
         */
        wrote = snprintf(&s_ps_app_feature_cheat_text_buf[off],
                         remaining,
                         "%08X%08X%08X%08X\n",
                         (unsigned int)s_ps_app_feature_cheat_entries[idx].flags,
                         (unsigned int)s_ps_app_feature_cheat_entries[idx].addr,
                         (unsigned int)s_ps_app_feature_cheat_entries[idx].compare,
                         (unsigned int)s_ps_app_feature_cheat_entries[idx].replace);
        if ((wrote <= 0) || ((size_t)wrote >= remaining)) {
            return XST_FAILURE;
        }
        off += (u32)wrote;
    }

    memset(&write_result, 0, sizeof(write_result));
    return PsFatFsStorage_WriteMemoryToFile(cheat_path,
                                            (UINTPTR)s_ps_app_feature_cheat_text_buf,
                                            off,
                                            &write_result);
}

static XStatus PsAppRuntimeFeature_LoadCheatsFromFile(PsAppRuntimeContext *ctx) {
    char cheat_path[PS_APP_ROM_PATH_MAX_CHARS];
    PsFatFsStorageReadResult read_result;
    u32 cursor;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    PsAppRuntimeFeature_BuildCheatPath(ctx, cheat_path, sizeof(cheat_path));
    memset(&read_result, 0, sizeof(read_result));
    if (PsFatFsStorage_ReadFileToMemory(cheat_path,
                                        (UINTPTR)s_ps_app_feature_cheat_text_buf,
                                        (u32)sizeof(s_ps_app_feature_cheat_text_buf),
                                        &read_result) != XST_SUCCESS) {
        s_ps_app_feature_cheat_count = 0U;
        if (ctx->feature != NULL) {
            ctx->feature->cheat_entry_count = 0U;
        }
        return XST_FAILURE;
    }

    s_ps_app_feature_cheat_text_buf[
        read_result.bytes_loaded < sizeof(s_ps_app_feature_cheat_text_buf)
            ? read_result.bytes_loaded
            : (sizeof(s_ps_app_feature_cheat_text_buf) - 1U)
    ] = '\0';
    s_ps_app_feature_cheat_count = 0U;

    cursor = 0U;
    while ((s_ps_app_feature_cheat_text_buf[cursor] != '\0') &&
           (s_ps_app_feature_cheat_count < PS_APP_FEATURE_CHEAT_MAX_ENTRIES)) {
        char line[40];
        u32 line_len;
        u32 w0;
        u32 w1;
        u32 w2;
        u32 w3;

        line_len = 0U;
        while ((s_ps_app_feature_cheat_text_buf[cursor] != '\0') &&
               (s_ps_app_feature_cheat_text_buf[cursor] != '\n') &&
               (line_len < (sizeof(line) - 1U))) {
            line[line_len++] = s_ps_app_feature_cheat_text_buf[cursor++];
        }
        line[line_len] = '\0';
        if (s_ps_app_feature_cheat_text_buf[cursor] == '\n') {
            cursor++;
        }

        /*
         * 仅接受 32 个 hex 字符（不含换行）。异常行直接跳过，
         * 这样即使 .gcht 局部损坏也不会阻塞 ROM 启动流程。
         */
        if (line_len != 32U) {
            continue;
        }
        if ((PsAppRuntimeFeature_ParseHex32(&line[0], &w0) != 0) ||
            (PsAppRuntimeFeature_ParseHex32(&line[8], &w1) != 0) ||
            (PsAppRuntimeFeature_ParseHex32(&line[16], &w2) != 0) ||
            (PsAppRuntimeFeature_ParseHex32(&line[24], &w3) != 0)) {
            continue;
        }

        s_ps_app_feature_cheat_entries[s_ps_app_feature_cheat_count].flags = w0;
        s_ps_app_feature_cheat_entries[s_ps_app_feature_cheat_count].addr = w1;
        s_ps_app_feature_cheat_entries[s_ps_app_feature_cheat_count].compare = w2;
        s_ps_app_feature_cheat_entries[s_ps_app_feature_cheat_count].replace = w3;
        s_ps_app_feature_cheat_count++;
    }

    if (ctx->feature != NULL) {
        ctx->feature->cheat_entry_count = s_ps_app_feature_cheat_count;
    }

    return XST_SUCCESS;
}

static void PsAppRuntimeFeature_ReplayCheats(PsAppRuntimeContext *ctx) {
    u32 idx;

    if (ctx == NULL) {
        return;
    }

    /*
     * 回放策略：先清 core 端 cheat RAM，再按缓存顺序逐条 push。
     * 这能保证“cheat off/on”与重启自动回放后得到同一组生效条目。
     */
    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_CHEAT_CLEAR_TRIG);
    for (idx = 0U; idx < s_ps_app_feature_cheat_count; ++idx) {
        PsGbaRegs_SetCheatCodeWords(ctx->regs, &s_ps_app_feature_cheat_entries[idx]);
        PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_CHEAT_PUSH_TRIG);
        PsAppRuntimeFeature_DelayMs(1U);
    }
}

static const char *PsAppRuntimeFeature_SkipSpaces(const char *cursor) {
    while ((cursor != NULL) &&
           ((*cursor == ' ') || (*cursor == '\t') || (*cursor == '\r') || (*cursor == '\n'))) {
        cursor++;
    }
    return cursor;
}

static u8 PsAppRuntimeFeature_ParseNextU64(const char **cursor_io, u64 *value_out) {
    const char *cursor;
    char *end_ptr;
    unsigned long long value;

    if ((cursor_io == NULL) || (*cursor_io == NULL) || (value_out == NULL)) {
        return 0U;
    }

    cursor = PsAppRuntimeFeature_SkipSpaces(*cursor_io);
    if ((cursor == NULL) || (*cursor == '\0') || (*cursor == '#')) {
        return 0U;
    }

    value = strtoull(cursor, &end_ptr, 0);
    if (end_ptr == cursor) {
        return 0U;
    }

    *value_out = (u64)value;
    *cursor_io = end_ptr;
    return 1U;
}

static XStatus PsAppRuntimeFeature_ReadSeedFile(u32 *seed_unix_out,
                                                u32 *seed_uncert_s_out,
                                                u8 *seed_valid_out) {
    PsFatFsStorageReadResult read_result;
    char seed_text[96];
    const char *cursor;
    u64 parsed_seed;
    u64 parsed_uncert;
    u32 seed_uncert_s;

    if (seed_unix_out != NULL) {
        *seed_unix_out = 0U;
    }
    if (seed_uncert_s_out != NULL) {
        *seed_uncert_s_out = PS_APP_FEATURE_RTC_DEFAULT_SEED_UNCERT_S;
    }
    if (seed_valid_out != NULL) {
        *seed_valid_out = 0U;
    }
    if ((seed_unix_out == NULL) || (seed_uncert_s_out == NULL) || (seed_valid_out == NULL)) {
        return XST_INVALID_PARAM;
    }

    memset(&read_result, 0, sizeof(read_result));
    memset(seed_text, 0, sizeof(seed_text));
    if (PsFatFsStorage_ReadFileToMemory(PS_APP_FEATURE_RTC_SEED_PATH,
                                        (UINTPTR)seed_text,
                                        (u32)(sizeof(seed_text) - 1U),
                                        &read_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    seed_text[(read_result.bytes_loaded < (sizeof(seed_text) - 1U))
                  ? read_result.bytes_loaded
                  : (sizeof(seed_text) - 1U)] = '\0';

    cursor = seed_text;
    if (PsAppRuntimeFeature_ParseNextU64(&cursor, &parsed_seed) == 0U) {
        return XST_FAILURE;
    }
    if (parsed_seed > 0xFFFFFFFFULL) {
        parsed_seed = 0xFFFFFFFFULL;
    }

    seed_uncert_s = PS_APP_FEATURE_RTC_DEFAULT_SEED_UNCERT_S;
    cursor = PsAppRuntimeFeature_SkipSpaces(cursor);
    if ((cursor != NULL) && (*cursor != '\0') && (*cursor != '#')) {
        if (PsAppRuntimeFeature_ParseNextU64(&cursor, &parsed_uncert) != 0U) {
            if (parsed_uncert > 0xFFFFFFFFULL) {
                parsed_uncert = 0xFFFFFFFFULL;
            }
            seed_uncert_s = PsAppRuntimeFeature_ClampUncertS((u32)parsed_uncert);
        }
    }

    *seed_unix_out = (u32)parsed_seed;
    *seed_uncert_s_out = seed_uncert_s;
    *seed_valid_out = 1U;
    return XST_SUCCESS;
}

static XStatus PsAppRuntimeFeature_PersistRtcOut(PsAppRuntimeContext *ctx) {
    PsAppRtcPersistDataV2 rtc_file;
    PsGbaRtcOut rtc_out;
    PsFatFsStorageWriteResult write_result;
    char rtc_path[PS_APP_ROM_PATH_MAX_CHARS];
    u32 crc_bytes;
    u32 flags;
    u32 base_uncert_s;

    if ((ctx == NULL) || (ctx->rom == NULL) || (ctx->feature == NULL) ||
        (ctx->rtc == NULL) || (ctx->rom->loaded == 0U)) {
        return XST_FAILURE;
    }

    PsGbaRegs_ReadRtcOut(ctx->regs, &rtc_out);

    memset(&rtc_file, 0, sizeof(rtc_file));
    flags = 0U;
    if (rtc_out.save_loaded != 0U) {
        flags |= PS_APP_FEATURE_RTC2_FLAG_HAS_HISTORY;
    }
    if (ctx->rtc->has_last_cal != 0U) {
        flags |= PS_APP_FEATURE_RTC2_FLAG_HAS_CAL;
    }

    if (ctx->rtc->uncert_infinite != 0U) {
        base_uncert_s = 0xFFFFFFFFU;
    } else {
        base_uncert_s = (u32)((ctx->rtc->base_uncert_us + 500000ULL) / 1000000ULL);
        base_uncert_s = PsAppRuntimeFeature_ClampUncertS(base_uncert_s);
    }

    rtc_file.magic = PS_APP_FEATURE_RTC2_MAGIC;
    rtc_file.version = (u16)PS_APP_FEATURE_RTC2_VERSION;
    rtc_file.flags = (u16)flags;
    rtc_file.saved_unix = rtc_out.timestamp;
    rtc_file.savedtime_lo = (u32)(rtc_out.savedtime & 0xFFFFFFFFULL);
    rtc_file.savedtime_hi = (u16)((rtc_out.savedtime >> 32) & 0x3FFULL);
    rtc_file.ppm_cal_ppb = PsAppRuntimeFeature_ClampPpmPpb(ctx->rtc->ppm_cal_ppb);
    rtc_file.ppm_sigma_ppb = PsAppRuntimeFeature_ClampSigmaPpb(ctx->rtc->ppm_sigma_ppb);
    rtc_file.last_cal_unix = PsAppRuntimeFeature_UsToUnixSeconds(ctx->rtc->last_cal_utc_us);
    rtc_file.base_uncert_s = base_uncert_s;
    crc_bytes = (u32)(sizeof(rtc_file) - sizeof(rtc_file.crc32));
    rtc_file.crc32 = PsAppRuntimeFeature_Crc32((const u8 *)&rtc_file, crc_bytes);

    PsAppRuntimeFeature_BuildRtcPath(ctx, rtc_path, sizeof(rtc_path));
    if (rtc_path[0] == '\0') {
        return XST_FAILURE;
    }

    memset(&write_result, 0, sizeof(write_result));
    if (PsFatFsStorage_WriteMemoryToFile(rtc_path,
                                         (UINTPTR)&rtc_file,
                                         (u32)sizeof(rtc_file),
                                         &write_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ctx->rtc->last_saved_unix = rtc_out.timestamp;
    ctx->rtc->last_saved_time = rtc_out.savedtime;
    return XST_SUCCESS;
}

static XStatus PsAppRuntimeFeature_PersistRtcGlobal(PsAppRuntimeContext *ctx) {
    PsAppRtcPersistDataV2 rtc_file;
    PsFatFsStorageWriteResult write_result;
    u32 crc_bytes;
    u32 flags;
    u32 base_uncert_s;

    if ((ctx == NULL) || (ctx->rtc == NULL) || (ctx->config == NULL)) {
        return XST_FAILURE;
    }
    if (ctx->rtc->model_ready == 0U) {
        return XST_FAILURE;
    }

    if (PsFatFsStorage_EnsureDirectory(PS_APP_RTC_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    memset(&rtc_file, 0, sizeof(rtc_file));
    flags = 0U;
    if (ctx->rtc->has_last_cal != 0U) {
        flags |= PS_APP_FEATURE_RTC2_FLAG_HAS_CAL;
    }

    if (ctx->rtc->uncert_infinite != 0U) {
        base_uncert_s = 0xFFFFFFFFU;
    } else {
        base_uncert_s = (u32)((ctx->rtc->base_uncert_us + 500000ULL) / 1000000ULL);
        base_uncert_s = PsAppRuntimeFeature_ClampUncertS(base_uncert_s);
    }

    rtc_file.magic = PS_APP_FEATURE_RTC2_MAGIC;
    rtc_file.version = (u16)PS_APP_FEATURE_RTC2_VERSION;
    rtc_file.flags = (u16)flags;
    rtc_file.saved_unix = ctx->config->rtc_timestamp;
    rtc_file.savedtime_lo = 0U;
    rtc_file.savedtime_hi = 0U;
    rtc_file.ppm_cal_ppb = PsAppRuntimeFeature_ClampPpmPpb(ctx->rtc->ppm_cal_ppb);
    rtc_file.ppm_sigma_ppb = PsAppRuntimeFeature_ClampSigmaPpb(ctx->rtc->ppm_sigma_ppb);
    rtc_file.last_cal_unix = PsAppRuntimeFeature_UsToUnixSeconds(ctx->rtc->last_cal_utc_us);
    rtc_file.base_uncert_s = base_uncert_s;
    crc_bytes = (u32)(sizeof(rtc_file) - sizeof(rtc_file.crc32));
    rtc_file.crc32 = PsAppRuntimeFeature_Crc32((const u8 *)&rtc_file, crc_bytes);

    memset(&write_result, 0, sizeof(write_result));
    if (PsFatFsStorage_WriteMemoryToFile(PS_APP_FEATURE_RTC_GLOBAL_PATH,
                                         (UINTPTR)&rtc_file,
                                         (u32)sizeof(rtc_file),
                                         &write_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

static void PsAppRuntimeFeature_LoadRtcBootstrap(PsAppRuntimeContext *ctx) {
    PsAppRtcPersistDataV2 rtc_file;
    PsFatFsStorageReadResult read_result;
    u32 now_ms;
    u64 now_met_us;
    u32 crc_bytes;
    u32 expect_crc;
    u32 seed_unix;
    u32 seed_uncert_s;
    u8 seed_valid;
    u32 global_unix;
    u32 current_unix;
    u8 global_valid;

    if ((ctx == NULL) || (ctx->rtc == NULL) || (ctx->config == NULL) || (ctx->feature == NULL)) {
        return;
    }

    now_ms = PsAppRuntimeFeature_NowMs();
    now_met_us = PsAppRuntimeFeature_NowMetUs();
    seed_unix = 0U;
    seed_uncert_s = PS_APP_FEATURE_RTC_DEFAULT_SEED_UNCERT_S;
    seed_valid = 0U;
    global_unix = 0U;
    current_unix = 0U;
    global_valid = 0U;

    ctx->rtc->model_ready = 0U;
    ctx->rtc->uncert_infinite = 1U;
    ctx->rtc->has_last_cal = 0U;
    ctx->rtc->ppm_cal_ppb = 0;
    ctx->rtc->ppm_sigma_ppb = PS_APP_FEATURE_RTC_DEFAULT_SIGMA_PPB;
    ctx->rtc->base_uncert_us = 0ULL;
    ctx->rtc->last_cal_met_us = 0ULL;
    ctx->rtc->last_cal_utc_us = 0ULL;
    ctx->rtc->last_saved_unix = 0U;
    ctx->rtc->last_saved_time = 0ULL;
    ctx->rtc->last_uncert_us = 0U;
    snprintf(ctx->rtc->path, sizeof(ctx->rtc->path), "%s", PS_APP_FEATURE_RTC_GLOBAL_PATH);

    memset(&read_result, 0, sizeof(read_result));
    memset(&rtc_file, 0, sizeof(rtc_file));
    if ((PsFatFsStorage_ReadFileToMemory(PS_APP_FEATURE_RTC_GLOBAL_PATH,
                                         (UINTPTR)&rtc_file,
                                         (u32)sizeof(rtc_file),
                                         &read_result) == XST_SUCCESS) &&
        (read_result.bytes_loaded == (u32)sizeof(rtc_file))) {
        crc_bytes = (u32)(sizeof(rtc_file) - sizeof(rtc_file.crc32));
        expect_crc = PsAppRuntimeFeature_Crc32((const u8 *)&rtc_file, crc_bytes);
        if ((rtc_file.magic == PS_APP_FEATURE_RTC2_MAGIC) &&
            ((u32)rtc_file.version == PS_APP_FEATURE_RTC2_VERSION) &&
            (rtc_file.crc32 == expect_crc)) {
            global_valid = 1U;
            global_unix = rtc_file.saved_unix;
            ctx->rtc->ppm_cal_ppb = PsAppRuntimeFeature_ClampPpmPpb(rtc_file.ppm_cal_ppb);
            ctx->rtc->ppm_sigma_ppb = PsAppRuntimeFeature_ClampSigmaPpb(
                (rtc_file.ppm_sigma_ppb != 0U) ? rtc_file.ppm_sigma_ppb
                                               : PS_APP_FEATURE_RTC_DEFAULT_SIGMA_PPB);
            ctx->rtc->last_saved_unix = global_unix;
        } else {
            xil_printf("[RTC] global rtc2 invalid: magic=0x%08x ver=%u crc=0x%08x/0x%08x\r\n",
                       (unsigned int)rtc_file.magic,
                       (unsigned int)rtc_file.version,
                       (unsigned int)rtc_file.crc32,
                       (unsigned int)expect_crc);
        }
    }

    if (PsAppRuntimeFeature_ReadSeedFile(&seed_unix, &seed_uncert_s, &seed_valid) == XST_SUCCESS) {
        xil_printf("[RTC] seed unix=%u uncert=%us\r\n",
                   (unsigned int)seed_unix,
                   (unsigned int)seed_uncert_s);
        PsAppRuntimeFeature_PrintRtcUnixDual("seed", seed_unix);
    } else {
        seed_valid = 0U;
    }

    current_unix = global_unix;
    if ((seed_valid != 0U) && (seed_unix > current_unix)) {
        current_unix = seed_unix;
    }

    ctx->config->rtc_timestamp = current_unix;
    ctx->config->rtc_timestamp_saved = global_unix;

    if ((seed_valid != 0U) || (global_valid != 0U)) {
        PsAppRuntimeFeature_RtcModelInit(ctx, now_met_us, PsAppRuntimeFeature_SecondsToUs(current_unix));
        if (seed_valid != 0U) {
            ctx->rtc->base_uncert_us = PsAppRuntimeFeature_SecondsToUs(
                PsAppRuntimeFeature_ClampUncertS(seed_uncert_s));
            ctx->rtc->uncert_infinite = 0U;
            ctx->rtc->has_last_cal = 1U;
            ctx->rtc->last_cal_met_us = now_met_us;
            ctx->rtc->last_cal_utc_us = PsAppRuntimeFeature_SecondsToUs(seed_unix);
        } else {
            /* 仅有上次关机前记录时，无法观测掉电窗口误差，按未知处理。 */
            ctx->rtc->uncert_infinite = 1U;
            ctx->rtc->base_uncert_us = 0ULL;
            ctx->rtc->has_last_cal = 0U;
            ctx->rtc->last_cal_met_us = 0ULL;
            ctx->rtc->last_cal_utc_us = 0ULL;
        }
        ctx->feature->rtc_last_tick_ts = now_ms;
        ctx->feature->rtc_last_persist_ts = now_ms;
        xil_printf("[RTC] boot global=%u seed=%u current=%u file=%u\r\n",
                   (unsigned int)global_unix,
                   (unsigned int)((seed_valid != 0U) ? seed_unix : 0U),
                   (unsigned int)current_unix,
                   (unsigned int)global_valid);
        PsAppRuntimeFeature_PrintRtcUnixDual("current", current_unix);
    } else {
        xil_printf("[RTC] boot rtc source missing (no global/seed)\r\n");
    }
}

static void PsAppRuntimeFeature_LoadRtcIn(PsAppRuntimeContext *ctx) {
    PsAppRtcPersistDataV2 rtc_file;
    PsFatFsStorageReadResult read_result;
    char rtc_path[PS_APP_ROM_PATH_MAX_CHARS];
    u32 now_ms;
    u64 now_met_us;
    u32 crc_bytes;
    u32 expect_crc;
    u32 seed_unix;
    u32 seed_uncert_s;
    u8 seed_valid;
    u32 saved_unix;
    u32 current_unix;
    u32 prev_current_unix;
    u64 savedtime;
    u8 save_loaded;
    u8 rtc_file_valid;
    u8 rtc_has_cal;

    if ((ctx == NULL) || (ctx->config == NULL) || (ctx->rtc == NULL)) {
        return;
    }

    now_ms = PsAppRuntimeFeature_NowMs();
    now_met_us = PsAppRuntimeFeature_NowMetUs();
    seed_unix = 0U;
    seed_uncert_s = PS_APP_FEATURE_RTC_DEFAULT_SEED_UNCERT_S;
    seed_valid = 0U;
    saved_unix = 0U;
    current_unix = 0U;
    prev_current_unix = ctx->config->rtc_timestamp;
    savedtime = 0ULL;
    save_loaded = 0U;
    rtc_file_valid = 0U;
    rtc_has_cal = 0U;

    ctx->rtc->model_ready = 0U;
    ctx->rtc->uncert_infinite = 1U;
    ctx->rtc->has_last_cal = 0U;
    ctx->rtc->ppm_cal_ppb = 0;
    ctx->rtc->ppm_sigma_ppb = PS_APP_FEATURE_RTC_DEFAULT_SIGMA_PPB;
    ctx->rtc->base_uncert_us = 0ULL;
    ctx->rtc->last_cal_met_us = 0ULL;
    ctx->rtc->last_cal_utc_us = 0ULL;
    ctx->rtc->last_saved_unix = 0U;
    ctx->rtc->last_saved_time = 0ULL;
    ctx->rtc->last_uncert_us = 0U;
    ctx->rtc->path[0] = '\0';

    PsAppRuntimeFeature_BuildRtcPath(ctx, rtc_path, sizeof(rtc_path));
    if (rtc_path[0] != '\0') {
        snprintf(ctx->rtc->path, sizeof(ctx->rtc->path), "%s", rtc_path);
    }
    memset(&read_result, 0, sizeof(read_result));
    memset(&rtc_file, 0, sizeof(rtc_file));

    if ((rtc_path[0] != '\0') &&
        (PsFatFsStorage_ReadFileToMemory(rtc_path,
                                         (UINTPTR)&rtc_file,
                                         (u32)sizeof(rtc_file),
                                         &read_result) == XST_SUCCESS) &&
        (read_result.bytes_loaded == (u32)sizeof(rtc_file))) {
        crc_bytes = (u32)(sizeof(rtc_file) - sizeof(rtc_file.crc32));
        expect_crc = PsAppRuntimeFeature_Crc32((const u8 *)&rtc_file, crc_bytes);
        if ((rtc_file.magic == PS_APP_FEATURE_RTC2_MAGIC) &&
            ((u32)rtc_file.version == PS_APP_FEATURE_RTC2_VERSION) &&
            (rtc_file.crc32 == expect_crc)) {
            rtc_file_valid = 1U;
            saved_unix = rtc_file.saved_unix;
            savedtime = (((u64)(rtc_file.savedtime_hi & 0x03FFU)) << 32) |
                        (u64)rtc_file.savedtime_lo;
            save_loaded = ((rtc_file.flags & PS_APP_FEATURE_RTC2_FLAG_HAS_HISTORY) != 0U) ? 1U : 0U;

            ctx->rtc->ppm_cal_ppb = PsAppRuntimeFeature_ClampPpmPpb(rtc_file.ppm_cal_ppb);
            ctx->rtc->ppm_sigma_ppb = PsAppRuntimeFeature_ClampSigmaPpb(
                (rtc_file.ppm_sigma_ppb != 0U) ? rtc_file.ppm_sigma_ppb
                                               : PS_APP_FEATURE_RTC_DEFAULT_SIGMA_PPB);
            if (rtc_file.base_uncert_s == 0xFFFFFFFFU) {
                ctx->rtc->uncert_infinite = 1U;
                ctx->rtc->base_uncert_us = 0ULL;
            } else {
                ctx->rtc->uncert_infinite = 0U;
                ctx->rtc->base_uncert_us = PsAppRuntimeFeature_SecondsToUs(
                    PsAppRuntimeFeature_ClampUncertS(rtc_file.base_uncert_s));
            }
            rtc_has_cal = ((rtc_file.flags & PS_APP_FEATURE_RTC2_FLAG_HAS_CAL) != 0U) ? 1U : 0U;
            if ((rtc_has_cal != 0U) && (rtc_file.last_cal_unix != 0U)) {
                ctx->rtc->last_cal_utc_us = PsAppRuntimeFeature_SecondsToUs(rtc_file.last_cal_unix);
            }
            ctx->rtc->last_saved_unix = saved_unix;
            ctx->rtc->last_saved_time = savedtime;
        } else {
            xil_printf("[RTC] rtc2 invalid: magic=0x%08x ver=%u crc=0x%08x/0x%08x\r\n",
                       (unsigned int)rtc_file.magic,
                       (unsigned int)rtc_file.version,
                       (unsigned int)rtc_file.crc32,
                       (unsigned int)expect_crc);
        }
    } else {
        xil_printf("[RTC] rtc2 missing/short (%s)\r\n", rtc_path[0] != '\0' ? rtc_path : "(no-path)");
    }

    if (PsAppRuntimeFeature_ReadSeedFile(&seed_unix, &seed_uncert_s, &seed_valid) == XST_SUCCESS) {
        xil_printf("[RTC] seed unix=%u uncert=%us\r\n",
                   (unsigned int)seed_unix,
                   (unsigned int)seed_uncert_s);
        PsAppRuntimeFeature_PrintRtcUnixDual("seed", seed_unix);
    } else {
        seed_valid = 0U;
    }

    current_unix = saved_unix;
    if ((seed_valid != 0U) && (seed_unix > current_unix)) {
        current_unix = seed_unix;
    }
    if (prev_current_unix > current_unix) {
        current_unix = prev_current_unix;
    }

    ctx->config->rtc_timestamp = current_unix;
    ctx->config->rtc_timestamp_saved = saved_unix;

    PsAppRuntimeFeature_RtcModelInit(ctx, now_met_us, PsAppRuntimeFeature_SecondsToUs(current_unix));
    if (seed_valid != 0U) {
        ctx->rtc->base_uncert_us = PsAppRuntimeFeature_SecondsToUs(
            PsAppRuntimeFeature_ClampUncertS(seed_uncert_s));
        ctx->rtc->uncert_infinite = 0U;
        ctx->rtc->has_last_cal = 1U;
        ctx->rtc->last_cal_met_us = now_met_us;
        ctx->rtc->last_cal_utc_us = PsAppRuntimeFeature_SecondsToUs(seed_unix);
    } else {
        /* 无可信 seed 时，墙钟误差在重启后不可观测，统一标记为未知。 */
        ctx->rtc->uncert_infinite = 1U;
        ctx->rtc->base_uncert_us = 0ULL;
        ctx->rtc->has_last_cal = 0U;
        ctx->rtc->last_cal_met_us = 0ULL;
        ctx->rtc->last_cal_utc_us = 0ULL;
    }

    PsAppRuntime_ApplyShadowConfig(ctx);
    PsGbaRegs_WriteRtcSavedTimeIn(ctx->regs, savedtime, 0U);
    PsAppRuntimeFeature_DelayMs(1U);
    if (save_loaded != 0U) {
        PsGbaRegs_WriteRtcSavedTimeIn(ctx->regs, savedtime, 1U);
        PsAppRuntimeFeature_DelayMs(1U);
    }
    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_RTC_NEW_TRIG);
    PsAppRuntimeFeature_DelayMs(1U);
    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_RTC_NEW_TRIG);

    ctx->feature->rtc_last_tick_ts = now_ms;
    ctx->feature->rtc_last_persist_ts = now_ms;
    xil_printf("[RTC] load saved=%u seed=%u current=%u history=%u file=%u\r\n",
               (unsigned int)saved_unix,
               (unsigned int)(seed_valid != 0U ? seed_unix : 0U),
               (unsigned int)current_unix,
               (unsigned int)save_loaded,
               (unsigned int)rtc_file_valid);
    PsAppRuntimeFeature_PrintRtcUnixDual("saved", saved_unix);
    PsAppRuntimeFeature_PrintRtcUnixDual("current", current_unix);
}

void PsAppRuntimeFeature_InitDefaults(PsAppRuntimeContext *ctx) {
    u32 now_ms;

    if ((ctx == NULL) || (ctx->feature == NULL) || (ctx->rtc == NULL)) {
        return;
    }

    now_ms = PsAppRuntimeFeature_NowMs();
    memset(ctx->feature, 0, sizeof(*ctx->feature));
    ctx->feature->savestate_slot = 0U;
    ctx->feature->rewind_enabled = 0U;
    ctx->feature->rewind_active = 0U;
    ctx->feature->solar_level = 3U;
    ctx->feature->tilt_x = 0;
    ctx->feature->tilt_y = 0;
    ctx->feature->rumble_state = 0U;
    ctx->feature->cheats_enabled = 0U;
    ctx->feature->checkpoint_head = 0U;
    ctx->feature->checkpoint_last_ts = now_ms;
    ctx->feature->rtc_last_tick_ts = now_ms;
    ctx->feature->rtc_last_persist_ts = now_ms;
    ctx->feature->rom_crc32 = 0U;
    ctx->feature->rom_id[0] = '\0';
    s_ps_app_feature_cheat_count = 0U;
    memset(ctx->rtc, 0, sizeof(*ctx->rtc));
    ctx->rtc->ppm_sigma_ppb = PS_APP_FEATURE_RTC_DEFAULT_SIGMA_PPB;
    ctx->rtc->uncert_infinite = 1U;
    PsAppRuntimeFeature_LoadRtcBootstrap(ctx);
    (void)PsAppRuntimeFeature_EnsureFeatureCtrl(ctx);
}

void PsAppRuntimeFeature_OnRomPreUnload(PsAppRuntimeContext *ctx) {
    if ((ctx == NULL) || (ctx->rom == NULL) || (ctx->rom->loaded == 0U)) {
        return;
    }

    if ((ctx->feature != NULL) && (ctx->feature->rumble_state != 0U)) {
        (void)PsAppUsbHost_SetRumble(ctx->usb_host_ctx, 0U, 0U);
        ctx->feature->rumble_state = 0U;
    }

    /* ROM 卸载前必须优先冲刷 save 脏页：
     * 后续加载新 ROM 会重建窗口与上下文，若这里漏刷，旧游戏存档会直接丢失。 */
    if ((ctx->save_ctx != NULL) &&
        (PsAppSave_FlushIfDirty(ctx->save_ctx) != XST_SUCCESS)) {
        xil_printf("[SAVE] pre-unload flush failed\r\n");
    }

    /* save 冲刷之后再持久化 RTC，避免与 save/rom 链路争用锁时打乱关键顺序。 */
    (void)PsAppRuntimeFeature_PersistRtcOut(ctx);
    (void)PsAppRuntimeFeature_PersistRtcGlobal(ctx);
}

void PsAppRuntimeFeature_OnRomLoaded(PsAppRuntimeContext *ctx) {
    if ((ctx == NULL) || (ctx->rom == NULL) || (ctx->feature == NULL) ||
        (ctx->rom->loaded == 0U)) {
        return;
    }

    PsAppRuntimeFeature_BuildRomId(ctx);
    if (PsAppRuntimeFeature_EnsureDirectories(ctx) != XST_SUCCESS) {
        xil_printf("[FEATURE] warning: directory ensure failed\r\n");
    }

    {
        u32 now_ms = PsAppRuntimeFeature_NowMs();
        ctx->feature->checkpoint_last_ts = now_ms;
        ctx->feature->rtc_last_tick_ts = now_ms;
        ctx->feature->rtc_last_persist_ts = now_ms;
    }
    ctx->feature->pending_save = 0U;
    ctx->feature->pending_load = 0U;

    /* ROM 装载后恢复顺序：
     * cheat -> rtc -> rewind checkpoint。
     * 先回放 cheat 可保证后续自动状态恢复在一致规则下运行。 */
    (void)PsAppRuntimeFeature_LoadCheatsFromFile(ctx);
    if (s_ps_app_feature_cheat_count != 0U) {
        PsAppRuntimeFeature_ReplayCheats(ctx);
    }

    PsAppRuntimeFeature_LoadRtcIn(ctx);
    (void)PsAppRuntimeFeature_RestoreLatestCheckpoint(ctx);
    (void)PsAppRuntimeFeature_EnsureFeatureCtrl(ctx);
    PsAppRuntimeFeature_EnsureSolarGpioRouting(ctx);
    PsAppRuntimeFeature_WriteSolarAndVerify(ctx, 0U);
}

void PsAppRuntimeFeature_ServiceFast(PsAppRuntimeContext *ctx) {
    u32 buttons;
    u32 prev_buttons;
    u8 back_now;
    u8 combo_now;
    s8 tilt_x;
    s8 tilt_y;
    u8 rewind_active;

    if ((ctx == NULL) || (ctx->feature == NULL) || (ctx->input == NULL) || (ctx->config == NULL)) {
        return;
    }

    PsAppRuntimeFeature_EnsureSolarGpioRouting(ctx);

    buttons = (u32)ctx->input->buttons;
    prev_buttons = ctx->feature->input_prev_buttons;
    back_now = (u8)(((buttons & PS_XINPUT_BUTTON_MASK_BACK) != 0U) ? 1U : 0U);

    if (back_now != 0U) {
        if (((buttons & PS_XINPUT_BUTTON_MASK_LEFT) != 0U) &&
            ((prev_buttons & PS_XINPUT_BUTTON_MASK_LEFT) == 0U)) {
            if (ctx->feature->savestate_slot == 0U) {
                ctx->feature->savestate_slot = 3U;
            } else {
                ctx->feature->savestate_slot--;
            }
            PsGbaRegs_SetSavestateSlot(ctx->regs, ctx->feature->savestate_slot);
            xil_printf("[SS] slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
        }

        if (((buttons & PS_XINPUT_BUTTON_MASK_RIGHT) != 0U) &&
            ((prev_buttons & PS_XINPUT_BUTTON_MASK_RIGHT) == 0U)) {
            ctx->feature->savestate_slot = (ctx->feature->savestate_slot + 1U) & 0x3U;
            PsGbaRegs_SetSavestateSlot(ctx->regs, ctx->feature->savestate_slot);
            xil_printf("[SS] slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
        }

        if (((buttons & PS_XINPUT_BUTTON_MASK_DOWN) != 0U) &&
            ((prev_buttons & PS_XINPUT_BUTTON_MASK_DOWN) == 0U)) {
            ctx->feature->pending_save = 1U;
        }

        if (((buttons & PS_XINPUT_BUTTON_MASK_UP) != 0U) &&
            ((prev_buttons & PS_XINPUT_BUTTON_MASK_UP) == 0U)) {
            ctx->feature->pending_load = 1U;
        }
    }

    combo_now = (u8)((back_now != 0U) &&
                     ((buttons & (PS_XINPUT_BUTTON_MASK_LEFT |
                                  PS_XINPUT_BUTTON_MASK_RIGHT |
                                  PS_XINPUT_BUTTON_MASK_UP |
                                  PS_XINPUT_BUTTON_MASK_DOWN)) != 0U));
    if (combo_now != 0U) {
        u32 keys_masked;

        keys_masked = ctx->config->keys & ~(PS_APP_GBA_KEY_SELECT_BIT |
                                            PS_APP_GBA_KEY_LEFT_BIT |
                                            PS_APP_GBA_KEY_RIGHT_BIT |
                                            PS_APP_GBA_KEY_UP_BIT |
                                            PS_APP_GBA_KEY_DOWN_BIT);
        if (keys_masked != ctx->config->keys) {
            ctx->config->keys = keys_masked;
            PsAppRuntime_ApplyShadowConfig(ctx);
        }
    }

    rewind_active = (u8)(((buttons & PS_XINPUT_BUTTON_MASK_R3) != 0U) ? 1U : 0U);
    if (ctx->feature->rewind_enabled == 0U) {
        rewind_active = 0U;
    }
    if (ctx->feature->rewind_active != rewind_active) {
        ctx->feature->rewind_active = rewind_active;
        (void)PsAppRuntimeFeature_EnsureFeatureCtrl(ctx);
    }

    tilt_x = 0;
    tilt_y = 0;
    if ((ctx->rom != NULL) && (ctx->rom->quirk_tilt != 0U)) {
        tilt_x = PsAppRuntimeFeature_S16ToS8(ctx->input->rx);
        tilt_y = PsAppRuntimeFeature_S16ToS8((s16)(-ctx->input->ry));
    }
    if ((ctx->feature->tilt_x != tilt_x) || (ctx->feature->tilt_y != tilt_y)) {
        ctx->feature->tilt_x = tilt_x;
        ctx->feature->tilt_y = tilt_y;
        PsAppRuntimeFeature_WriteSensorInputRaw(ctx);
    }

    if ((ctx->rom != NULL) && (ctx->rom->quirk_solar != 0U)) {
        if ((ctx->input->lt >= PS_APP_FEATURE_TRIGGER_THRESH) &&
            (ctx->feature->input_prev_lt < PS_APP_FEATURE_TRIGGER_THRESH) &&
            (ctx->feature->solar_level < 7U)) {
            ctx->feature->solar_level++;
            PsAppRuntimeFeature_WriteSolarAndVerify(ctx, 1U);
        }
        if ((ctx->input->rt >= PS_APP_FEATURE_TRIGGER_THRESH) &&
            (ctx->feature->input_prev_rt < PS_APP_FEATURE_TRIGGER_THRESH) &&
            (ctx->feature->solar_level > 0U)) {
            ctx->feature->solar_level--;
            PsAppRuntimeFeature_WriteSolarAndVerify(ctx, 1U);
        }
    }

    ctx->feature->input_prev_buttons = buttons;
    ctx->feature->input_prev_lt = ctx->input->lt;
    ctx->feature->input_prev_rt = ctx->input->rt;
}

void PsAppRuntimeFeature_ServiceSlow(PsAppRuntimeContext *ctx) {
    u32 now_ms;
    u32 feature_status;

    if ((ctx == NULL) || (ctx->feature == NULL) || (ctx->rom == NULL)) {
        return;
    }

    now_ms = PsAppRuntimeFeature_NowMs();

    if (ctx->feature->pending_save != 0U) {
        char save_path[PS_APP_ROM_PATH_MAX_CHARS];

        PsAppRuntimeFeature_BuildSavestatePath(ctx,
                                               ctx->feature->savestate_slot,
                                               save_path,
                                               sizeof(save_path));
        if (PsAppRuntimeFeature_SaveSlotToPath(ctx,
                                               ctx->feature->savestate_slot,
                                               save_path) == XST_SUCCESS) {
            xil_printf("[SS] saved slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
        } else {
            xil_printf("[SS] save failed slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
        }
        ctx->feature->pending_save = 0U;
    }

    if (ctx->feature->pending_load != 0U) {
        char load_path[PS_APP_ROM_PATH_MAX_CHARS];

        PsAppRuntimeFeature_BuildSavestatePath(ctx,
                                               ctx->feature->savestate_slot,
                                               load_path,
                                               sizeof(load_path));
        if (PsAppRuntimeFeature_LoadSlotFromPath(ctx,
                                                 ctx->feature->savestate_slot,
                                                 load_path) == XST_SUCCESS) {
            xil_printf("[SS] loaded slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
        } else {
            xil_printf("[SS] load failed slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
        }
        ctx->feature->pending_load = 0U;
    }

    if ((ctx->rom->loaded != 0U) && (ctx->feature->rewind_enabled != 0U) &&
        ((now_ms - ctx->feature->checkpoint_last_ts) >= PS_APP_REWIND_CKPT_INTERVAL_MS)) {
        if (PsAppRuntimeFeature_SaveOneCheckpoint(ctx) == XST_SUCCESS) {
            ctx->feature->checkpoint_last_ts = now_ms;
        }
    }

    feature_status = PsGbaRegs_ReadFeatureStatus(ctx->regs);
    if ((ctx->feature->rumble_state == 0U) &&
        ((feature_status & GBA_FEATURE_STATUS_RUMBLE_OUT) != 0U)) {
        if (PsAppUsbHost_SetRumble(ctx->usb_host_ctx,
                                   PS_APP_FEATURE_RUMBLE_LARGE_ON,
                                   PS_APP_FEATURE_RUMBLE_SMALL_ON) == XST_SUCCESS) {
            ctx->feature->rumble_state = 1U;
        }
    } else if ((ctx->feature->rumble_state != 0U) &&
               ((feature_status & GBA_FEATURE_STATUS_RUMBLE_OUT) == 0U)) {
        (void)PsAppUsbHost_SetRumble(ctx->usb_host_ctx, 0U, 0U);
        ctx->feature->rumble_state = 0U;
    }

    if (ctx->rtc->model_ready != 0U) {
        u64 now_met_us = PsAppRuntimeFeature_NowMetUs();
        u64 utc_est_us = PsAppRuntimeFeature_RtcEstimateUtcUs(ctx->rtc, now_met_us);
        u64 uncert_us = PsAppRuntimeFeature_RtcEstimateUncertaintyUs(ctx->rtc, now_met_us);
        u32 current_unix = PsAppRuntimeFeature_UsToUnixSeconds(utc_est_us);
        u8 core_accepting_rtc = (u8)((ctx->rom->loaded != 0U) && (ctx->rom->is_loading == 0U));

        if (current_unix != ctx->config->rtc_timestamp) {
            ctx->config->rtc_timestamp = current_unix;
            PsAppRuntime_ApplyShadowConfig(ctx);
            if (core_accepting_rtc != 0U) {
                PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_RTC_NEW_TRIG);
            }
        }
        ctx->rtc->last_current_unix = current_unix;
        ctx->rtc->last_uncert_us = (uncert_us > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (u32)uncert_us;
        ctx->feature->rtc_last_tick_ts = now_ms;
    }

    /* ROM 切换窗口内暂停 RTC 文件周期写：
     * 切 ROM 本身就会触发高频 FatFs IO（save flush / rtc restore / rom read），
     * 若此时继续周期 persist，容易放大锁竞争并诱发“切换卡死”。 */
    if ((ctx->rom->is_loading == 0U) &&
        ((now_ms - ctx->feature->rtc_last_persist_ts) >= PS_APP_FEATURE_RTC_PERSIST_INTERVAL_MS)) {
        if (ctx->rom->loaded != 0U) {
            if (PsAppRuntimeFeature_PersistRtcOut(ctx) != XST_SUCCESS) {
                xil_printf("[RTC] persist failed\r\n");
            }
        } else {
            if ((ctx->rtc->model_ready != 0U) &&
                (PsAppRuntimeFeature_PersistRtcGlobal(ctx) != XST_SUCCESS)) {
                xil_printf("[RTC] global persist failed\r\n");
            }
        }
        ctx->feature->rtc_last_persist_ts = now_ms;
    }
}

static u32 PsAppRuntimeFeature_ParseU32(const char *text, u8 *ok_out) {
    char *end_ptr;
    unsigned long value;

    if (ok_out != NULL) {
        *ok_out = 0U;
    }
    if (text == NULL) {
        return 0U;
    }

    /* base=0: 同时支持十进制与 0x 十六进制，便于串口命令直接粘贴地址。 */
    value = strtoul(text, &end_ptr, 0);
    if ((end_ptr == text) || ((end_ptr != NULL) && (*end_ptr != '\0'))) {
        return 0U;
    }

    if (ok_out != NULL) {
        *ok_out = 1U;
    }
    return (u32)value;
}

static int PsAppRuntimeFeature_ParseCheatOp(const char *op_text) {
    if (op_text == NULL) {
        return -1;
    }
    /* 允许文本别名 + 符号别名，降低串口输入出错率。 */
    if ((strcmp(op_text, "eq") == 0) || (strcmp(op_text, "==") == 0)) return PS_APP_FEATURE_CHEAT_OP_EQ;
    if ((strcmp(op_text, "ne") == 0) || (strcmp(op_text, "!=") == 0)) return PS_APP_FEATURE_CHEAT_OP_NE;
    if ((strcmp(op_text, "lt") == 0) || (strcmp(op_text, "<") == 0)) return PS_APP_FEATURE_CHEAT_OP_LT;
    if ((strcmp(op_text, "le") == 0) || (strcmp(op_text, "<=") == 0)) return PS_APP_FEATURE_CHEAT_OP_LE;
    if ((strcmp(op_text, "gt") == 0) || (strcmp(op_text, ">") == 0)) return PS_APP_FEATURE_CHEAT_OP_GT;
    if ((strcmp(op_text, "ge") == 0) || (strcmp(op_text, ">=") == 0)) return PS_APP_FEATURE_CHEAT_OP_GE;
    return -1;
}

/*
 * gba_cheats.vhd 位段约定（cheat_in[127:96] = flags）：
 *   flags[3:0]  : operation type (0=always, 1=eq, 2=gt, 3=lt, 4=ge, 5=le, 6=ne)
 *   flags[7:4]  : byte enable mask (bit0..3 -> data byte0..3)
 *
 * 旧版本将 mask 放在 flags[31:16]，core 不读取该位段，会导致“命令成功但游戏无效果”。
 */
static u8 PsAppRuntimeFeature_CheatMaskToByteEnable(u32 mask) {
    u8 byte_en = 0U;

    /*
     * 控制台 mask 是 32bit 位掩码语义；core 侧需要按字节使能：
     * - mask[7:0]   非零 -> 使能 byte0
     * - mask[15:8]  非零 -> 使能 byte1
     * - mask[23:16] 非零 -> 使能 byte2
     * - mask[31:24] 非零 -> 使能 byte3
     */
    if ((mask & 0x000000FFU) != 0U) byte_en |= (1U << 0);
    if ((mask & 0x0000FF00U) != 0U) byte_en |= (1U << 1);
    if ((mask & 0x00FF0000U) != 0U) byte_en |= (1U << 2);
    if ((mask & 0xFF000000U) != 0U) byte_en |= (1U << 3);

    return byte_en;
}

static int PsAppRuntimeFeature_MapCondOpToCoreOp(int op) {
    /*
     * 命令层 op 枚举与 core OPTYPE 常量值不同，必须显式映射。
     * 这里若改错，会出现“命令通过但条件方向反了”的隐蔽故障。
     */
    switch (op) {
        case PS_APP_FEATURE_CHEAT_OP_EQ: return 1; /* OPTYPE_EQUALS */
        case PS_APP_FEATURE_CHEAT_OP_NE: return 6; /* OPTYPE_NOT_EQ */
        case PS_APP_FEATURE_CHEAT_OP_LT: return 3; /* OPTYPE_LESS */
        case PS_APP_FEATURE_CHEAT_OP_LE: return 5; /* OPTYPE_LESS_EQ */
        case PS_APP_FEATURE_CHEAT_OP_GT: return 2; /* OPTYPE_GREATER */
        case PS_APP_FEATURE_CHEAT_OP_GE: return 4; /* OPTYPE_GREATER_EQ */
        default: return -1;
    }
}

static u32 PsAppRuntimeFeature_BuildCheatFlags(u8 core_op, u8 byte_en) {
    /* flags[3:0]=op, flags[7:4]=byte enable，其余位当前保留。 */
    return (((u32)(byte_en & 0x0FU)) << 4) | ((u32)(core_op & 0x0FU));
}

static void PsAppRuntimeFeature_AddCheatEntry(const PsGbaCheatCodeWords *entry) {
    if ((entry == NULL) || (s_ps_app_feature_cheat_count >= PS_APP_FEATURE_CHEAT_MAX_ENTRIES)) {
        return;
    }
    s_ps_app_feature_cheat_entries[s_ps_app_feature_cheat_count] = *entry;
    s_ps_app_feature_cheat_count++;
}

static void PsAppRuntimeFeature_PrintRtcStatus(PsAppRuntimeContext *ctx) {
    u64 now_met_us;
    u64 utc_est_us;
    u64 uncert_us;

    if ((ctx == NULL) || (ctx->rtc == NULL) || (ctx->config == NULL)) {
        return;
    }

    now_met_us = PsAppRuntimeFeature_NowMetUs();
    utc_est_us = PsAppRuntimeFeature_RtcEstimateUtcUs(ctx->rtc, now_met_us);
    uncert_us = PsAppRuntimeFeature_RtcEstimateUncertaintyUs(ctx->rtc, now_met_us);
    xil_printf("[RTC] status model=%u current=%u saved=%u est=%u ppm=%d sigma=%u cal=%u\r\n",
               (unsigned int)ctx->rtc->model_ready,
               (unsigned int)ctx->config->rtc_timestamp,
               (unsigned int)ctx->config->rtc_timestamp_saved,
               (unsigned int)PsAppRuntimeFeature_UsToUnixSeconds(utc_est_us),
               (int)ctx->rtc->ppm_cal_ppb,
               (unsigned int)ctx->rtc->ppm_sigma_ppb,
               (unsigned int)ctx->rtc->has_last_cal);
    PsAppRuntimeFeature_PrintRtcUnixDual("current", ctx->config->rtc_timestamp);
    PsAppRuntimeFeature_PrintRtcUnixDual("saved", ctx->config->rtc_timestamp_saved);
    PsAppRuntimeFeature_PrintRtcUnixDual("est", PsAppRuntimeFeature_UsToUnixSeconds(utc_est_us));
    if ((ctx->rtc->uncert_infinite != 0U) || (uncert_us == 0xFFFFFFFFFFFFFFFFULL)) {
        xil_printf("[RTC] uncertainty=INF last_cal_unix=%u path=%s\r\n",
                   (unsigned int)PsAppRuntimeFeature_UsToUnixSeconds(ctx->rtc->last_cal_utc_us),
                   ctx->rtc->path[0] != '\0' ? ctx->rtc->path : "(none)");
    } else {
        xil_printf("[RTC] uncertainty=+-%us (%u us) last_cal_unix=%u path=%s\r\n",
                   (unsigned int)((uncert_us + 500000ULL) / 1000000ULL),
                   (unsigned int)((uncert_us > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (u32)uncert_us),
                   (unsigned int)PsAppRuntimeFeature_UsToUnixSeconds(ctx->rtc->last_cal_utc_us),
                   ctx->rtc->path[0] != '\0' ? ctx->rtc->path : "(none)");
    }
    PsAppRuntimeFeature_PrintRtcUnixDual("last_cal",
                                         PsAppRuntimeFeature_UsToUnixSeconds(ctx->rtc->last_cal_utc_us));
}

static void PsAppRuntimeFeature_RtcApplySync(PsAppRuntimeContext *ctx, u32 sync_unix, u32 uncert_s) {
    u64 now_met_us;
    u64 sync_utc_us;
    u64 delta_met_us;
    s64 delta_utc_us;
    s64 diff_us;
    s32 raw_ppb;
    s32 old_ppm;
    s32 new_ppm;
    u32 resid_ppb;
    u32 sigma_target;
    u32 new_sigma;
    u32 now_ms;
    u32 prev_current_unix;
    s64 denom;

    if ((ctx == NULL) || (ctx->rtc == NULL) || (ctx->config == NULL) ||
        (ctx->feature == NULL) || (ctx->regs == NULL)) {
        return;
    }

    uncert_s = PsAppRuntimeFeature_ClampUncertS(uncert_s);
    prev_current_unix = ctx->config->rtc_timestamp;
    now_met_us = PsAppRuntimeFeature_NowMetUs();
    sync_utc_us = PsAppRuntimeFeature_SecondsToUs(sync_unix);

    if ((ctx->rtc->has_last_cal != 0U) &&
        (now_met_us > ctx->rtc->last_cal_met_us) &&
        ((now_met_us - ctx->rtc->last_cal_met_us) >= PS_APP_FEATURE_RTC_CAL_MIN_INTERVAL_US)) {
        delta_met_us = now_met_us - ctx->rtc->last_cal_met_us;
        delta_utc_us = (s64)sync_utc_us - (s64)ctx->rtc->last_cal_utc_us;
        diff_us = delta_utc_us - (s64)delta_met_us;
        if (diff_us > 9000000000000LL) {
            diff_us = 9000000000000LL;
        } else if (diff_us < -9000000000000LL) {
            diff_us = -9000000000000LL;
        }
        denom = (s64)(delta_met_us / 1000ULL);
        if (denom <= 0LL) {
            denom = 1LL;
        }
        raw_ppb = (s32)((diff_us * 1000000LL) / denom);
        raw_ppb = PsAppRuntimeFeature_ClampPpmPpb(raw_ppb);

        old_ppm = ctx->rtc->ppm_cal_ppb;
        new_ppm = PsAppRuntimeFeature_ClampPpmPpb((old_ppm * 3 + raw_ppb) / 4);
        ctx->rtc->ppm_cal_ppb = new_ppm;

        resid_ppb = (u32)((raw_ppb >= old_ppm) ? (raw_ppb - old_ppm) : (old_ppm - raw_ppb));
        sigma_target = resid_ppb * 2U;
        if (sigma_target < PS_APP_FEATURE_RTC_SIGMA_MIN_PPB) {
            sigma_target = PS_APP_FEATURE_RTC_SIGMA_MIN_PPB;
        }
        new_sigma = ((ctx->rtc->ppm_sigma_ppb * 3U) + sigma_target) / 4U;
        ctx->rtc->ppm_sigma_ppb = PsAppRuntimeFeature_ClampSigmaPpb(new_sigma);

        xil_printf("[RTC] sync learn raw_ppb=%d old_ppb=%d new_ppb=%d sigma=%u\r\n",
                   (int)raw_ppb,
                   (int)old_ppm,
                   (int)ctx->rtc->ppm_cal_ppb,
                   (unsigned int)ctx->rtc->ppm_sigma_ppb);
    } else if ((ctx->rtc->has_last_cal != 0U) &&
               (now_met_us > ctx->rtc->last_cal_met_us)) {
        xil_printf("[RTC] sync learn skipped: delta=%us (<300s)\r\n",
                   (unsigned int)((now_met_us - ctx->rtc->last_cal_met_us) / 1000000ULL));
    }

    PsAppRuntimeFeature_RtcModelInit(ctx, now_met_us, sync_utc_us);
    ctx->rtc->base_uncert_us = PsAppRuntimeFeature_SecondsToUs(uncert_s);
    ctx->rtc->uncert_infinite = 0U;
    ctx->rtc->has_last_cal = 1U;
    ctx->rtc->last_cal_met_us = now_met_us;
    ctx->rtc->last_cal_utc_us = sync_utc_us;
    ctx->rtc->last_uncert_us = (ctx->rtc->base_uncert_us > 0xFFFFFFFFULL)
                                   ? 0xFFFFFFFFU
                                   : (u32)ctx->rtc->base_uncert_us;

    ctx->config->rtc_timestamp = sync_unix;
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_RTC_NEW_TRIG);
    if (PsAppRuntimeFeature_PersistRtcGlobal(ctx) != XST_SUCCESS) {
        xil_printf("[RTC] global persist failed after sync\r\n");
    }

    now_ms = PsAppRuntimeFeature_NowMs();
    ctx->feature->rtc_last_tick_ts = now_ms;
    ctx->feature->rtc_last_persist_ts = now_ms;
    xil_printf("[RTC] sync applied unix=%u uncert=%us%s\r\n",
               (unsigned int)sync_unix,
               (unsigned int)uncert_s,
               ((sync_unix < prev_current_unix) ? " (backstep)" : ""));
    PsAppRuntimeFeature_PrintRtcUnixDual("sync", sync_unix);
}

u8 PsAppRuntimeFeature_HandleConsole(PsAppRuntimeContext *ctx, const char *cmd) {
    char *arg1;

    if ((ctx == NULL) || (ctx->feature == NULL) || (cmd == NULL)) {
        return 0U;
    }

    if (strcmp(cmd, "rtc") == 0) {
        char *sub = strtok(NULL, " \t");
        u8 ok;
        u32 sync_unix;
        u32 uncert_s;

        if ((sub == NULL) || (strcmp(sub, "status") == 0)) {
            PsAppRuntimeFeature_PrintRtcStatus(ctx);
            return 1U;
        }

        if (strcmp(sub, "sync") == 0) {
            char *unix_text = strtok(NULL, " \t");
            char *uncert_text = strtok(NULL, " \t");

            sync_unix = PsAppRuntimeFeature_ParseU32(unix_text, &ok);
            if (ok == 0U) {
                xil_printf("[CMD] usage: rtc status | rtc sync <unix> [uncert_s] | rtc <unix>\r\n");
                return 1U;
            }
            uncert_s = PS_APP_FEATURE_RTC_DEFAULT_SYNC_UNCERT_S;
            if (uncert_text != NULL) {
                uncert_s = PsAppRuntimeFeature_ParseU32(uncert_text, &ok);
                if (ok == 0U) {
                    xil_printf("[CMD] usage: rtc sync <unix> [uncert_s]\r\n");
                    return 1U;
                }
            }
            PsAppRuntimeFeature_RtcApplySync(ctx, sync_unix, uncert_s);
            return 1U;
        }

        sync_unix = PsAppRuntimeFeature_ParseU32(sub, &ok);
        if (ok != 0U) {
            PsAppRuntimeFeature_RtcApplySync(ctx,
                                             sync_unix,
                                             PS_APP_FEATURE_RTC_DEFAULT_SYNC_UNCERT_S);
            return 1U;
        }

        xil_printf("[CMD] usage: rtc status | rtc sync <unix> [uncert_s] | rtc <unix>\r\n");
        return 1U;
    }

    if (strcmp(cmd, "rewind") == 0) {
        u8 ok;
        u32 value;

        arg1 = strtok(NULL, " \t");
        if (arg1 == NULL) {
            xil_printf("[CMD] rewind is %s\r\n", (ctx->feature->rewind_enabled != 0U) ? "on" : "off");
            return 1U;
        }

        value = PsAppRuntimeFeature_ParseU32(arg1, &ok);
        if ((!ok) && (strcmp(arg1, "on") != 0) && (strcmp(arg1, "off") != 0)) {
            xil_printf("[CMD] usage: rewind on|off\r\n");
            return 1U;
        }
        if ((strcmp(arg1, "on") == 0) || ((ok != 0U) && (value != 0U))) {
            ctx->feature->rewind_enabled = 1U;
        } else {
            ctx->feature->rewind_enabled = 0U;
            ctx->feature->rewind_active = 0U;
        }
        (void)PsAppRuntimeFeature_EnsureFeatureCtrl(ctx);
        xil_printf("[CMD] rewind=%u\r\n", (unsigned int)ctx->feature->rewind_enabled);
        return 1U;
    }

    if (strcmp(cmd, "savestate") == 0) {
        char *sub = strtok(NULL, " \t");
        if (sub == NULL) {
            xil_printf("[CMD] savestate slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
            return 1U;
        }
        if (strcmp(sub, "slot") == 0) {
            u8 ok;
            u32 slot = PsAppRuntimeFeature_ParseU32(strtok(NULL, " \t"), &ok);
            if ((ok != 0U) && (slot < 4U)) {
                ctx->feature->savestate_slot = (u8)slot;
                PsGbaRegs_SetSavestateSlot(ctx->regs, ctx->feature->savestate_slot);
                xil_printf("[CMD] savestate slot=%u\r\n", (unsigned int)ctx->feature->savestate_slot);
            } else {
                xil_printf("[CMD] usage: savestate slot <0..3>\r\n");
            }
            return 1U;
        }
        if (strcmp(sub, "save") == 0) {
            ctx->feature->pending_save = 1U;
            xil_printf("[CMD] savestate save queued\r\n");
            return 1U;
        }
        if (strcmp(sub, "load") == 0) {
            ctx->feature->pending_load = 1U;
            xil_printf("[CMD] savestate load queued\r\n");
            return 1U;
        }
        xil_printf("[CMD] usage: savestate slot <0..3>|save|load\r\n");
        return 1U;
    }

    if (strcmp(cmd, "cheat") == 0) {
        char *sub = strtok(NULL, " \t");

        if (sub == NULL) {
            xil_printf("[CMD] cheat %s count=%u\r\n",
                       (ctx->feature->cheats_enabled != 0U) ? "on" : "off",
                       (unsigned int)s_ps_app_feature_cheat_count);
            return 1U;
        }

        if ((strcmp(sub, "on") == 0) || (strcmp(sub, "off") == 0)) {
            ctx->feature->cheats_enabled = (u8)((strcmp(sub, "on") == 0) ? 1U : 0U);
            (void)PsAppRuntimeFeature_EnsureFeatureCtrl(ctx);
            if (ctx->feature->cheats_enabled != 0U) {
                /*
                 * 关键语义：cheat on 不只是打开开关，还要把当前缓存回放到 core。
                 * 否则在“先 add（off 状态）再 on”的场景中，core 端不会拿到条目。
                 */
                PsAppRuntimeFeature_ReplayCheats(ctx);
            }
            xil_printf("[CMD] cheat=%u count=%u\r\n",
                       (unsigned int)ctx->feature->cheats_enabled,
                       (unsigned int)s_ps_app_feature_cheat_count);
            return 1U;
        }

        if (strcmp(sub, "clear") == 0) {
            s_ps_app_feature_cheat_count = 0U;
            ctx->feature->cheat_entry_count = 0U;
            PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_CHEAT_CLEAR_TRIG);
            (void)PsAppRuntimeFeature_SaveCheatsToFile(ctx);
            xil_printf("[CMD] cheat cleared\r\n");
            return 1U;
        }

        if (strcmp(sub, "list") == 0) {
            u32 idx;
            for (idx = 0U; idx < s_ps_app_feature_cheat_count; ++idx) {
                xil_printf("[CHEAT] %u: %08x %08x %08x %08x\r\n",
                           (unsigned int)idx,
                           (unsigned int)s_ps_app_feature_cheat_entries[idx].flags,
                           (unsigned int)s_ps_app_feature_cheat_entries[idx].addr,
                           (unsigned int)s_ps_app_feature_cheat_entries[idx].compare,
                           (unsigned int)s_ps_app_feature_cheat_entries[idx].replace);
            }
            xil_printf("[CHEAT] total=%u\r\n", (unsigned int)s_ps_app_feature_cheat_count);
            return 1U;
        }

        if (strcmp(sub, "add") == 0) {
            char *mode = strtok(NULL, " \t");
            if ((mode != NULL) && (strcmp(mode, "always") == 0)) {
                u8 ok_addr;
                u8 ok_value;
                u8 ok_mask = 1U;
                u8 byte_en;
                u32 addr;
                u32 value;
                u32 mask = 0xFFFFU;
                PsGbaCheatCodeWords entry;

                addr = PsAppRuntimeFeature_ParseU32(strtok(NULL, " \t"), &ok_addr);
                value = PsAppRuntimeFeature_ParseU32(strtok(NULL, " \t"), &ok_value);
                {
                    char *mask_text = strtok(NULL, " \t");
                    if (mask_text != NULL) {
                        mask = PsAppRuntimeFeature_ParseU32(mask_text, &ok_mask);
                    }
                }
                if ((ok_addr == 0U) || (ok_value == 0U) || (ok_mask == 0U)) {
                    xil_printf("[CMD] usage: cheat add always <addr> <value> [mask]\r\n");
                    return 1U;
                }
                byte_en = PsAppRuntimeFeature_CheatMaskToByteEnable(mask);
                if (byte_en == 0U) {
                    xil_printf("[CMD] usage: cheat add always <addr> <value> [mask]\r\n");
                    return 1U;
                }

                /*
                 * always 写码：每帧都会按 byte_en 覆写目标地址。
                 * 例如写 0x04000000 bit7=1 会出现“白屏但声音继续”，属预期。
                 */
                entry.flags = PsAppRuntimeFeature_BuildCheatFlags(0U, byte_en);
                entry.addr = addr;
                entry.compare = 0U;
                entry.replace = value;
                PsAppRuntimeFeature_AddCheatEntry(&entry);
                if (ctx->feature->cheats_enabled != 0U) {
                    PsGbaRegs_SetCheatCodeWords(ctx->regs, &entry);
                    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_CHEAT_PUSH_TRIG);
                }
                ctx->feature->cheat_entry_count = s_ps_app_feature_cheat_count;
                (void)PsAppRuntimeFeature_SaveCheatsToFile(ctx);
                xil_printf("[CMD] cheat add always ok total=%u\r\n", (unsigned int)s_ps_app_feature_cheat_count);
                return 1U;
            }

            if ((mode != NULL) && (strcmp(mode, "if") == 0)) {
                char *op_text = strtok(NULL, " \t");
                u8 ok_addr;
                u8 ok_cmp;
                u8 ok_then;
                u8 ok_val;
                u8 ok_mask = 1U;
                u8 byte_en;
                u32 addr;
                u32 compare;
                u32 value;
                u32 mask = 0xFFFFU;
                int op;
                int core_op;
                PsGbaCheatCodeWords cond_entry;
                PsGbaCheatCodeWords write_entry;

                op = PsAppRuntimeFeature_ParseCheatOp(op_text);
                core_op = PsAppRuntimeFeature_MapCondOpToCoreOp(op);
                addr = PsAppRuntimeFeature_ParseU32(strtok(NULL, " \t"), &ok_addr);
                compare = PsAppRuntimeFeature_ParseU32(strtok(NULL, " \t"), &ok_cmp);
                {
                    char *then_word = strtok(NULL, " \t");
                    ok_then = (u8)((then_word != NULL) && (strcmp(then_word, "then") == 0));
                }
                value = PsAppRuntimeFeature_ParseU32(strtok(NULL, " \t"), &ok_val);
                {
                    char *mask_text = strtok(NULL, " \t");
                    if (mask_text != NULL) {
                        mask = PsAppRuntimeFeature_ParseU32(mask_text, &ok_mask);
                    }
                }

                byte_en = PsAppRuntimeFeature_CheatMaskToByteEnable(mask);
                if ((core_op < 0) || (ok_addr == 0U) || (ok_cmp == 0U) ||
                    (ok_then == 0U) || (ok_val == 0U) || (ok_mask == 0U) ||
                    (byte_en == 0U) ||
                    (s_ps_app_feature_cheat_count > (PS_APP_FEATURE_CHEAT_MAX_ENTRIES - 2U))) {
                    xil_printf("[CMD] usage: cheat add if <op> <addr> <compare> then <value> [mask]\r\n");
                    return 1U;
                }

                /*
                 * if-op-then 在底层会展开为两条码，顺序不可交换：
                 * 1) 条件码 cond_entry
                 * 2) 写入码 write_entry（仅在条件满足时执行）
                 */
                cond_entry.flags = PsAppRuntimeFeature_BuildCheatFlags((u8)core_op, byte_en);
                cond_entry.addr = addr;
                cond_entry.compare = compare;
                cond_entry.replace = 0U;
                write_entry.flags = PsAppRuntimeFeature_BuildCheatFlags(0U, byte_en);
                write_entry.addr = addr;
                write_entry.compare = 0U;
                write_entry.replace = value;

                PsAppRuntimeFeature_AddCheatEntry(&cond_entry);
                PsAppRuntimeFeature_AddCheatEntry(&write_entry);

                if (ctx->feature->cheats_enabled != 0U) {
                    PsGbaRegs_SetCheatCodeWords(ctx->regs, &cond_entry);
                    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_CHEAT_PUSH_TRIG);
                    PsGbaRegs_SetCheatCodeWords(ctx->regs, &write_entry);
                    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_CHEAT_PUSH_TRIG);
                }
                ctx->feature->cheat_entry_count = s_ps_app_feature_cheat_count;
                (void)PsAppRuntimeFeature_SaveCheatsToFile(ctx);
                xil_printf("[CMD] cheat add if ok total=%u\r\n", (unsigned int)s_ps_app_feature_cheat_count);
                return 1U;
            }

            xil_printf("[CMD] usage: cheat add always <addr> <value> [mask]\r\n");
            xil_printf("[CMD]    or: cheat add if <op> <addr> <compare> then <value> [mask]\r\n");
            return 1U;
        }

        xil_printf("[CMD] usage: cheat on|off|clear|list|add ...\r\n");
        return 1U;
    }

    return 0U;
}
