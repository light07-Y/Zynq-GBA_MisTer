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
#define PS_APP_FEATURE_RTC_MAGIC         0x52544331U
#define PS_APP_FEATURE_CKPT_MAGIC        0x434B5054U
#define PS_APP_FEATURE_RUMBLE_LARGE_ON   0xC0U
#define PS_APP_FEATURE_RUMBLE_SMALL_ON   0x60U
#define PS_APP_FEATURE_TRIGGER_THRESH    80U
#define PS_APP_FEATURE_SENSOR_DEADZONE   4096
#define PS_APP_FEATURE_SENSOR_ECHO_RETRY 6U
#define PS_APP_FEATURE_RTC_PERSIST_INTERVAL_MS 30000U

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
    u32 timestamp;
    u32 savedtime_lo;
    u32 savedtime_hi;
} PsAppRtcPersistData;

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

static XStatus PsAppRuntimeFeature_PersistRtcOut(PsAppRuntimeContext *ctx) {
    PsAppRtcPersistData rtc_file;
    PsGbaRtcOut rtc_out;
    PsFatFsStorageWriteResult write_result;
    char rtc_path[PS_APP_ROM_PATH_MAX_CHARS];

    if ((ctx == NULL) || (ctx->rom == NULL) || (ctx->feature == NULL) ||
        (ctx->rom->loaded == 0U)) {
        return XST_FAILURE;
    }

    /* 将 core 侧 RTC 输出压平为固定 16B 记录：
     * magic + timestamp + savedtime(低32/高10) + save_loaded 标志。 */
    PsGbaRegs_ReadRtcOut(ctx->regs, &rtc_out);
    rtc_file.magic = PS_APP_FEATURE_RTC_MAGIC;
    rtc_file.timestamp = rtc_out.timestamp;
    rtc_file.savedtime_lo = (u32)(rtc_out.savedtime & 0xFFFFFFFFULL);
    rtc_file.savedtime_hi = (u32)((rtc_out.savedtime >> 32) & 0x3FFULL);
    if (rtc_out.save_loaded != 0U) {
        rtc_file.savedtime_hi |= (1UL << 31);
    }

    PsAppRuntimeFeature_BuildRtcPath(ctx, rtc_path, sizeof(rtc_path));
    memset(&write_result, 0, sizeof(write_result));
    return PsFatFsStorage_WriteMemoryToFile(rtc_path,
                                            (UINTPTR)&rtc_file,
                                            (u32)sizeof(rtc_file),
                                            &write_result);
}

static void PsAppRuntimeFeature_LoadRtcIn(PsAppRuntimeContext *ctx) {
    PsAppRtcPersistData rtc_file;
    PsFatFsStorageReadResult read_result;
    char rtc_path[PS_APP_ROM_PATH_MAX_CHARS];
    u64 savedtime;
    u8 save_loaded;

    if ((ctx == NULL) || (ctx->config == NULL)) {
        return;
    }

    PsAppRuntimeFeature_BuildRtcPath(ctx, rtc_path, sizeof(rtc_path));
    memset(&read_result, 0, sizeof(read_result));
    memset(&rtc_file, 0, sizeof(rtc_file));

    if (PsFatFsStorage_ReadFileToMemory(rtc_path,
                                        (UINTPTR)&rtc_file,
                                        (u32)sizeof(rtc_file),
                                        &read_result) == XST_SUCCESS &&
        (rtc_file.magic == PS_APP_FEATURE_RTC_MAGIC)) {
        ctx->config->rtc_timestamp = rtc_file.timestamp;
        savedtime = (((u64)(rtc_file.savedtime_hi & 0x3FFU)) << 32) |
                    (u64)rtc_file.savedtime_lo;
        save_loaded = (u8)((rtc_file.savedtime_hi >> 31) & 0x1U);
    } else {
        ctx->config->rtc_timestamp = 0U;
        savedtime = 0ULL;
        save_loaded = 0U;
    }

    /* RTC 外部握手顺序必须固定：
     * 1) 先写 shadow rtc_timestamp；
     * 2) 再写 savedtime 输入寄存器；
     * 3) 最后脉冲 RTC_NEW_TRIG 通知 core 采样。 */
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsGbaRegs_WriteRtcSavedTimeIn(ctx->regs, savedtime, save_loaded);
    PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_RTC_NEW_TRIG);
}

void PsAppRuntimeFeature_InitDefaults(PsAppRuntimeContext *ctx) {
    u32 now_ms;

    if ((ctx == NULL) || (ctx->feature == NULL)) {
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

    /* ROM 切换窗口内暂停 RTC 周期写：
     * 切 ROM 本身就会触发高频 FatFs IO（save flush / rtc restore / rom read），
     * 若此时继续每秒 tick + 周期 persist，容易放大锁竞争并诱发“切换卡死”。 */
    if ((ctx->rom->loaded != 0U) && (ctx->rom->is_loading == 0U)) {
        while ((now_ms - ctx->feature->rtc_last_tick_ts) >= 1000U) {
            ctx->config->rtc_timestamp += 1U;
            PsAppRuntime_ApplyShadowConfig(ctx);
            PsGbaRegs_TriggerFeatureAction(ctx->regs, GBA_FEATURE_ACTION_RTC_NEW_TRIG);
            ctx->feature->rtc_last_tick_ts += 1000U;
        }

        if ((now_ms - ctx->feature->rtc_last_persist_ts) >=
            PS_APP_FEATURE_RTC_PERSIST_INTERVAL_MS) {
            if (PsAppRuntimeFeature_PersistRtcOut(ctx) != XST_SUCCESS) {
                xil_printf("[RTC] persist failed\r\n");
            }
            ctx->feature->rtc_last_persist_ts = now_ms;
        }
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

u8 PsAppRuntimeFeature_HandleConsole(PsAppRuntimeContext *ctx, const char *cmd) {
    char *arg1;

    if ((ctx == NULL) || (ctx->feature == NULL) || (cmd == NULL)) {
        return 0U;
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
