#include "App/Inc/ps_app_runtime.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "sleep.h"
#include "xaxivdma_hw.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "xuartps.h"
#include "xil_printf.h"

#include "Diagnostics/Inc/ps_app_diag.h"
#include "Input/Inc/ps_app_input.h"
#include "Save/Inc/ps_app_save.h"
#include "Video/Inc/ps_app_video.h"
#include "Hdmi/Inc/ps_app_hdmi_link.h"
#include "Rom/Inc/ps_rom_loader.h"
#include "Storage/Inc/ps_fatfs_storage.h"
#include "UsbHost/Inc/ps_app_usbhost.h"

/*
 * 这里显式保留 MIO50/MIO51 的 SLCR 地址，是为了在应用启动阶段兜底覆盖
 * 旧 platform/ps7_init 残留的错误配置。实测错误配置会把 BTN4/BTN5 钉死，
 * 导致 raw50/raw51 始终不变，UART 侧也看不到按键边沿。
 */
#define PS_APP_SLCR_LOCK_ADDR          0xF8000004U
#define PS_APP_SLCR_UNLOCK_ADDR        0xF8000008U
#define PS_APP_SLCR_LOCK_CODE          0x0000767BU
#define PS_APP_SLCR_UNLOCK_CODE        0x0000DF0DU
#define PS_APP_MIO50_CFG_ADDR          0xF80007C8U
#define PS_APP_MIO51_CFG_ADDR          0xF80007CCU
#define PS_APP_MIO_GPIO_INPUT_CFG      0x00000200U
/* UI 音量参数（百分比域）：0..100，按键步进 5，默认开机 45。 */
#define PS_APP_AUDIO_VOLUME_MAX        100U
#define PS_APP_AUDIO_VOLUME_STEP       5U
#define PS_APP_AUDIO_DEFAULT_VOLUME    45U
/* SSM2603 音量寄存器边界：0x30=-73 dB，0x79=0 dB(unity gain)。 */
#define PS_APP_AUDIO_CODEC_MIN_DB_VOL  0x30U
#define PS_APP_AUDIO_CODEC_UNITY_VOL   0x79U
#define PS_APP_RTC_FILE_MAGIC          0x43545247U /* "GRTC" */
#define PS_APP_STATE_FILE_MAGIC        0x54534247U /* "GBST" */
#define PS_APP_STATE_FILE_VERSION      0x00010000U
#define PS_APP_CORE_STATE_WORDS        0x00018346U
#define PS_APP_SENSOR_SOLAR_MAX        7U
#define PS_APP_SENSOR_TILT_DIVISOR     256

typedef struct {
    u32 magic;
    u32 timestamp_saved;
    u32 savedtime_lo;
    u32 savedtime_hi_loaded;
} PsAppRtcFileRecord;

typedef struct {
    u32 magic;
    u32 version;
    u32 slot;
    u32 payload_bytes;
    u32 core_state_words;
    u32 reserved0;
    u32 reserved1;
    u32 reserved2;
} PsAppStateFileHeader;

static u8 s_state_file_buffer[sizeof(PsAppStateFileHeader) + PS_APP_STATE_SLOT_BYTES]
    __attribute__((aligned(64)));

static u32 PsAppRuntime_ClampStateSlot(u32 slot);

static void PsAppRuntime_CopyText(char *dst, size_t dst_size, const char *src) {
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

static void PsAppRuntime_RomStem(const char *rom_path, char *stem_out, size_t stem_size) {
    const char *name_ptr;
    const char *dot_ptr;
    size_t copy_len;

    if ((stem_out == NULL) || (stem_size == 0U)) {
        return;
    }

    stem_out[0] = '\0';
    if ((rom_path == NULL) || (*rom_path == '\0')) {
        return;
    }

    name_ptr = strrchr(rom_path, '/');
    if (name_ptr == NULL) {
        name_ptr = strrchr(rom_path, '\\');
    }
    name_ptr = (name_ptr == NULL) ? rom_path : (name_ptr + 1);
    dot_ptr = strrchr(name_ptr, '.');
    copy_len = (dot_ptr != NULL) ? (size_t)(dot_ptr - name_ptr) : strlen(name_ptr);
    if (copy_len >= stem_size) {
        copy_len = stem_size - 1U;
    }

    memcpy(stem_out, name_ptr, copy_len);
    stem_out[copy_len] = '\0';
}

static void PsAppRuntime_BuildRtcPath(const char *rom_path, char *path_out, size_t path_size) {
    char stem[PS_APP_ROM_PATH_MAX_CHARS];
    size_t dir_len;
    size_t stem_len;
    const char *ext;
    size_t ext_len;

    if ((path_out == NULL) || (path_size == 0U)) {
        return;
    }

    path_out[0] = '\0';
    PsAppRuntime_RomStem(rom_path, stem, sizeof(stem));
    if (stem[0] == '\0') {
        return;
    }

    ext = ".rtc";
    dir_len = strlen(PS_APP_RTC_SD_DIR);
    stem_len = strlen(stem);
    ext_len = strlen(ext);
    if ((dir_len + 1U + stem_len + ext_len + 1U) > path_size) {
        return;
    }

    memcpy(path_out, PS_APP_RTC_SD_DIR, dir_len);
    path_out[dir_len] = '/';
    memcpy(&path_out[dir_len + 1U], stem, stem_len);
    memcpy(&path_out[dir_len + 1U + stem_len], ext, ext_len);
    path_out[dir_len + 1U + stem_len + ext_len] = '\0';
}

static void PsAppRuntime_BuildStatePath(const char *rom_path,
                                        u32 slot,
                                        char *path_out,
                                        size_t path_size) {
    char stem[PS_APP_ROM_PATH_MAX_CHARS];
    char ext[10];
    size_t dir_len;
    size_t stem_len;
    size_t ext_len;

    if ((path_out == NULL) || (path_size == 0U)) {
        return;
    }

    path_out[0] = '\0';
    PsAppRuntime_RomStem(rom_path, stem, sizeof(stem));
    if (stem[0] == '\0') {
        return;
    }

    if (slot >= PS_APP_STATE_SLOT_COUNT) {
        slot = 0U;
    }
    memcpy(ext, ".s1.state", sizeof(ext));
    ext[2] = (char)('1' + (char)slot);
    ext_len = strlen(ext);
    dir_len = strlen(PS_APP_STATE_SD_DIR);
    stem_len = strlen(stem);
    if ((dir_len + 1U + stem_len + ext_len + 1U) > path_size) {
        return;
    }

    memcpy(path_out, PS_APP_STATE_SD_DIR, dir_len);
    path_out[dir_len] = '/';
    memcpy(&path_out[dir_len + 1U], stem, stem_len);
    memcpy(&path_out[dir_len + 1U + stem_len], ext, ext_len);
    path_out[dir_len + 1U + stem_len + ext_len] = '\0';
}

static UINTPTR PsAppRuntime_StateSlotBaseAddr(u32 slot) {
    u32 safe_slot;

    safe_slot = PsAppRuntime_ClampStateSlot(slot);
    return (UINTPTR)(PS_APP_STATE_REGION_BASE_ADDR + (safe_slot * PS_APP_STATE_SLOT_BYTES));
}

static const char *PsAppRuntime_GetActiveStatePath(PsAppRuntimeContext *ctx,
                                                   char *fallback_path,
                                                   size_t fallback_size,
                                                   u32 slot) {
    if ((ctx != NULL) &&
        (ctx->state_feature != NULL) &&
        (ctx->state_feature->last_state_path[0] != '\0')) {
        return ctx->state_feature->last_state_path;
    }

    if ((ctx != NULL) && (ctx->rom != NULL)) {
        PsAppRuntime_BuildStatePath(ctx->rom->path, slot, fallback_path, fallback_size);
        if (fallback_path[0] != '\0') {
            return fallback_path;
        }
    }

    return "";
}

static XStatus PsAppRuntime_SaveStateSlotToFile(PsAppRuntimeContext *ctx,
                                                u32 slot,
                                                const char *path) {
    PsFatFsStorageWriteResult write_result;
    PsAppStateFileHeader header;
    UINTPTR slot_base_addr;
    u8 *payload_ptr;
    u32 total_bytes;

    if ((ctx == NULL) || (ctx->state_feature == NULL) || (path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    if (PsFatFsStorage_EnsureDirectory(PS_APP_STATE_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    slot = PsAppRuntime_ClampStateSlot(slot);
    slot_base_addr = PsAppRuntime_StateSlotBaseAddr(slot);
    payload_ptr = &s_state_file_buffer[sizeof(PsAppStateFileHeader)];
    total_bytes = (u32)sizeof(PsAppStateFileHeader) + PS_APP_STATE_SLOT_BYTES;

    header.magic = PS_APP_STATE_FILE_MAGIC;
    header.version = PS_APP_STATE_FILE_VERSION;
    header.slot = slot;
    header.payload_bytes = PS_APP_STATE_SLOT_BYTES;
    header.core_state_words = PS_APP_CORE_STATE_WORDS;
    header.reserved0 = 0U;
    header.reserved1 = 0U;
    header.reserved2 = 0U;

    Xil_DCacheInvalidateRange((INTPTR)slot_base_addr, (INTPTR)PS_APP_STATE_SLOT_BYTES);
    memcpy(s_state_file_buffer, &header, sizeof(header));
    memcpy(payload_ptr, (const void *)slot_base_addr, PS_APP_STATE_SLOT_BYTES);
    Xil_DCacheFlushRange((INTPTR)s_state_file_buffer, (INTPTR)total_bytes);

    memset(&write_result, 0, sizeof(write_result));
    if (PsFatFsStorage_WriteMemoryToFile(path,
                                         (UINTPTR)s_state_file_buffer,
                                         total_bytes,
                                         &write_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ctx->state_feature->save_file_count++;
    PsAppRuntime_CopyText(ctx->state_feature->last_state_path,
                          sizeof(ctx->state_feature->last_state_path),
                          path);
    return XST_SUCCESS;
}

static XStatus PsAppRuntime_LoadStateSlotFromFile(PsAppRuntimeContext *ctx,
                                                  u32 slot,
                                                  const char *path) {
    PsFatFsStorageReadResult read_result;
    const u8 *payload_ptr;
    u32 payload_bytes;
    UINTPTR slot_base_addr;
    const PsAppStateFileHeader *header;

    if ((ctx == NULL) || (ctx->state_feature == NULL) || (path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    memset(&read_result, 0, sizeof(read_result));
    if (PsFatFsStorage_ReadFileToMemory(path,
                                        (UINTPTR)s_state_file_buffer,
                                        sizeof(s_state_file_buffer),
                                        &read_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    payload_ptr = NULL;
    payload_bytes = 0U;
    if (read_result.bytes_loaded == PS_APP_STATE_SLOT_BYTES) {
        payload_ptr = s_state_file_buffer;
        payload_bytes = PS_APP_STATE_SLOT_BYTES;
    } else if (read_result.bytes_loaded ==
               ((u32)sizeof(PsAppStateFileHeader) + PS_APP_STATE_SLOT_BYTES)) {
        header = (const PsAppStateFileHeader *)s_state_file_buffer;
        if ((header->magic != PS_APP_STATE_FILE_MAGIC) ||
            (header->version != PS_APP_STATE_FILE_VERSION) ||
            (header->payload_bytes != PS_APP_STATE_SLOT_BYTES) ||
            (header->core_state_words != PS_APP_CORE_STATE_WORDS)) {
            return XST_FAILURE;
        }
        payload_ptr = &s_state_file_buffer[sizeof(PsAppStateFileHeader)];
        payload_bytes = PS_APP_STATE_SLOT_BYTES;
    } else {
        return XST_FAILURE;
    }

    slot = PsAppRuntime_ClampStateSlot(slot);
    slot_base_addr = PsAppRuntime_StateSlotBaseAddr(slot);
    memcpy((void *)slot_base_addr, payload_ptr, payload_bytes);
    Xil_DCacheFlushRange((INTPTR)slot_base_addr, (INTPTR)payload_bytes);

    ctx->state_feature->load_file_count++;
    PsAppRuntime_CopyText(ctx->state_feature->last_state_path,
                          sizeof(ctx->state_feature->last_state_path),
                          path);
    return XST_SUCCESS;
}

static u32 PsAppRuntime_ClampStateSlot(u32 slot) {
    if (PS_APP_STATE_SLOT_COUNT == 0U) {
        return 0U;
    }
    return slot % PS_APP_STATE_SLOT_COUNT;
}

static s8 PsAppRuntime_ClampS16ToS8(s16 value) {
    if (value > 127) {
        return 127;
    }
    if (value < -128) {
        return -128;
    }
    return (s8)value;
}

static u32 PsAppRuntime_BuildSensorRegWord(const PsAppSensorState *sensor) {
    u32 solar;

    if (sensor == NULL) {
        return 0U;
    }

    solar = sensor->solar;
    if (solar > PS_APP_SENSOR_SOLAR_MAX) {
        solar = PS_APP_SENSOR_SOLAR_MAX;
    }

    return (solar & 0x7U) |
           ((((u32)(u8)sensor->tilt_x) & 0xFFU) << 8) |
           ((((u32)(u8)sensor->tilt_y) & 0xFFU) << 16);
}

static void PsAppRuntime_ApplyFeatureLevels(PsAppRuntimeContext *ctx) {
    if ((ctx == NULL) || (ctx->regs == NULL) || (ctx->state_feature == NULL)) {
        return;
    }

    ctx->state_feature->slot = (u8)PsAppRuntime_ClampStateSlot(ctx->state_feature->slot);
    PsGbaRegs_SetStateSlot(ctx->regs, ctx->state_feature->slot);
    PsGbaRegs_SetRewindControl(ctx->regs,
                               (u32)(ctx->state_feature->rewind_enable & 0x1U),
                               (u32)(ctx->state_feature->rewind_active & 0x1U));
    PsGbaRegs_SetCheatEnable(ctx->regs, (u32)(ctx->state_feature->cheats_enabled & 0x1U));
    PsGbaRegs_SetCheatWords(ctx->regs,
                            ctx->state_feature->cheat_words[0],
                            ctx->state_feature->cheat_words[1],
                            ctx->state_feature->cheat_words[2],
                            ctx->state_feature->cheat_words[3]);
}

static u32 PsAppRuntime_SanitizeCtrlShadow(PsAppRuntimeContext *ctx,
                                           const char *tag,
                                           u8 print_when_clean) {
    u32 raw_ctrl;
    u32 safe_ctrl;

    if ((ctx == NULL) || (ctx->config == NULL)) {
        return 0U;
    }

    raw_ctrl = ctx->config->ctrl;
    safe_ctrl = raw_ctrl & GBA_CTRL_VALID_MASK;
    if (safe_ctrl != raw_ctrl) {
        xil_printf("[CFG] warning: ctrl shadow masked tag=%s raw=0x%08x safe=0x%08x\r\n",
                   (tag != NULL) ? tag : "unknown",
                   (unsigned int)raw_ctrl,
                   (unsigned int)safe_ctrl);
    } else if (print_when_clean != 0U) {
        xil_printf("[CFG] ctrl shadow ok tag=%s val=0x%08x\r\n",
                   (tag != NULL) ? tag : "unknown",
                   (unsigned int)safe_ctrl);
    }
    ctx->config->ctrl = safe_ctrl;
    return safe_ctrl;
}

static void PsAppRuntime_ValidateContextStability(PsAppRuntimeContext *ctx, const char *tag) {
    u8 vdma_bad;
    XStatus status;

    if ((ctx == NULL) || (ctx->vdma == NULL) || (ctx->video_ctx == NULL) || (ctx->config == NULL)) {
        return;
    }

    /*
     * 第一层防线：先清掉 ctrl 的异常高位。
     * 历史上出现过 0xEAxxxxxx 混入 ctrl，继续下发会把核心带进不可预期状态。
     */
    (void)PsAppRuntime_SanitizeCtrlShadow(ctx, tag, 0U);

    /*
     * 第二层防线：修复 runtime_ctx 与 video_ctx 的关键指针关系。
     * 若栈/内存曾被踩坏，这几根指针最先决定后续是否继续扩散。
     */
    if (ctx->video_ctx->vdma != ctx->vdma) {
        xil_printf("[CTX] warning: video_ctx->vdma mismatch tag=%s old=0x%08x new=0x%08x\r\n",
                   (tag != NULL) ? tag : "unknown",
                   (unsigned int)(UINTPTR)ctx->video_ctx->vdma,
                   (unsigned int)(UINTPTR)ctx->vdma);
        ctx->video_ctx->vdma = ctx->vdma;
    }
    if (ctx->video_ctx->regs != ctx->regs) {
        xil_printf("[CTX] warning: video_ctx->regs mismatch tag=%s old=0x%08x new=0x%08x\r\n",
                   (tag != NULL) ? tag : "unknown",
                   (unsigned int)(UINTPTR)ctx->video_ctx->regs,
                   (unsigned int)(UINTPTR)ctx->regs);
        ctx->video_ctx->regs = ctx->regs;
    }
    if (ctx->video_ctx->state != ctx->video) {
        xil_printf("[CTX] warning: video_ctx->state mismatch tag=%s old=0x%08x new=0x%08x\r\n",
                   (tag != NULL) ? tag : "unknown",
                   (unsigned int)(UINTPTR)ctx->video_ctx->state,
                   (unsigned int)(UINTPTR)ctx->video);
        ctx->video_ctx->state = ctx->video;
    }

    vdma_bad = 0U;
    /*
     * 第三层防线：用“硬约束”校验 VDMA 上下文。
     * 一旦 width/height/stride/frame_addrs/baseaddr 任一异常，就判定上下文已污染。
     */
    if ((ctx->vdma->width != PS_APP_HDMI_WIDTH) ||
        (ctx->vdma->height != PS_APP_HDMI_HEIGHT) ||
        (ctx->vdma->bytes_per_pixel != PS_APP_HDMI_BPP) ||
        (ctx->vdma->line_stride_bytes != (PS_APP_HDMI_WIDTH * PS_APP_HDMI_BPP)) ||
        (ctx->vdma->frame_size_bytes != (PS_APP_HDMI_WIDTH * PS_APP_HDMI_HEIGHT * PS_APP_HDMI_BPP)) ||
        (ctx->vdma->frame_count != 3U) ||
        (ctx->vdma->frame_addrs[0] != (UINTPTR)PS_APP_FB_REGION_BASE_ADDR) ||
        (ctx->vdma->frame_addrs[1] != (UINTPTR)(PS_APP_FB_REGION_BASE_ADDR + PS_APP_FB_FRAME_STORE_BYTES)) ||
        (ctx->vdma->frame_addrs[2] != (UINTPTR)(PS_APP_FB_REGION_BASE_ADDR + (2U * PS_APP_FB_FRAME_STORE_BYTES))) ||
        (ctx->vdma->vdma.BaseAddr != (UINTPTR)XPAR_XAXIVDMA_0_BASEADDR)) {
        vdma_bad = 1U;
    }

    if (vdma_bad == 0U) {
        return;
    }

    xil_printf("[CTX] warning: vdma context corrupted tag=%s base=0x%08x w=%u h=%u stride=%u count=%u ready=%u\r\n",
               (tag != NULL) ? tag : "unknown",
               (unsigned int)ctx->vdma->vdma.BaseAddr,
               (unsigned int)ctx->vdma->width,
               (unsigned int)ctx->vdma->height,
               (unsigned int)ctx->vdma->line_stride_bytes,
               (unsigned int)ctx->vdma->frame_count,
               (unsigned int)ctx->vdma->is_ready);

    /*
     * 进入自愈流程：重新初始化 VDMA + 重新绑定 video IRQ。
     * 目标是“不中断启动流程并恢复到可显示状态”，而不是直接卡死。
     */
    status = PsHdmiVdma_Init(ctx->vdma,
                             XPAR_XAXIVDMA_0_BASEADDR,
                             (UINTPTR)PS_APP_FB_REGION_BASE_ADDR,
                             PS_APP_FB_FRAME_STORE_BYTES,
                             PS_APP_HDMI_WIDTH,
                             PS_APP_HDMI_HEIGHT,
                             PS_APP_HDMI_BPP);
    if (status != XST_SUCCESS) {
        xil_printf("[CTX] warning: vdma reinit failed tag=%s status=%d\r\n",
                   (tag != NULL) ? tag : "unknown",
                   (int)status);
        return;
    }

    status = PsAppVideo_InitInterrupts(ctx->video_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[CTX] warning: video irq rebind failed tag=%s status=%d\r\n",
                   (tag != NULL) ? tag : "unknown",
                   (int)status);
    }
    PsAppVideo_SetInterframeMode(ctx->video_ctx, ctx->video->fx.interframe_mode);
    PsAppVideo_SetShadeMode(ctx->video_ctx, ctx->video->fx.shade_mode);
}

static void PsAppRuntime_ApplyRtcSavedState(PsAppRuntimeContext *ctx) {
    if ((ctx == NULL) || (ctx->regs == NULL) || (ctx->rtc == NULL)) {
        return;
    }

    PsGbaRegs_SetRtcSavedState(ctx->regs,
                               ctx->rtc->timestamp_saved,
                               ctx->rtc->saved_time,
                               (u32)(ctx->rtc->loaded & 0x1U));
}

static void PsAppRuntime_ApplySensorState(PsAppRuntimeContext *ctx, u8 force) {
    u32 sensor_word;
    u32 old_word;

    if ((ctx == NULL) || (ctx->regs == NULL) || (ctx->sensor == NULL)) {
        return;
    }

    sensor_word = PsAppRuntime_BuildSensorRegWord(ctx->sensor);
    old_word = PsGbaRegs_Read(ctx->regs, GBA_REG_SENSOR);
    if ((force == 0U) && (old_word == sensor_word)) {
        return;
    }

    PsGbaRegs_Write(ctx->regs, GBA_REG_SENSOR, sensor_word);
    if (ctx->sensor->sensor_update_count < 0xFFFFFFFFU) {
        ctx->sensor->sensor_update_count++;
    }
}

static XStatus PsAppRuntime_FlushRtcToFile(PsAppRuntimeContext *ctx) {
    PsFatFsStorageWriteResult write_result;
    PsAppRtcFileRecord record;

    if ((ctx == NULL) || (ctx->rtc == NULL)) {
        return XST_FAILURE;
    }
    if (ctx->rtc->path[0] == '\0') {
        return XST_INVALID_PARAM;
    }

    if (PsFatFsStorage_EnsureDirectory(PS_APP_RTC_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    record.magic = PS_APP_RTC_FILE_MAGIC;
    record.timestamp_saved = ctx->rtc->timestamp_saved;
    record.savedtime_lo = (u32)(ctx->rtc->saved_time & 0xFFFFFFFFULL);
    record.savedtime_hi_loaded = ((u32)((ctx->rtc->saved_time >> 32U) & 0x3FFULL)) |
                                 (((u32)(ctx->rtc->loaded & 0x1U)) << 10);

    memset(&write_result, 0, sizeof(write_result));
    if (PsFatFsStorage_WriteMemoryToFile(ctx->rtc->path,
                                         (UINTPTR)&record,
                                         sizeof(record),
                                         &write_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ctx->rtc->dirty = 0U;
    ctx->rtc->save_countdown = ctx->rtc->save_interval_ticks;
    xil_printf("[RTC] flush %s ts=0x%08x st=0x%08x_%08x\r\n",
               ctx->rtc->path,
               (unsigned int)ctx->rtc->timestamp_saved,
               (unsigned int)((ctx->rtc->saved_time >> 32U) & 0x3FFULL),
               (unsigned int)(ctx->rtc->saved_time & 0xFFFFFFFFULL));
    return XST_SUCCESS;
}

static XStatus PsAppRuntime_PrepareRtcForRom(PsAppRuntimeContext *ctx, const char *rom_path) {
    PsFatFsStorageReadResult read_result;
    PsAppRtcFileRecord record;
    char rtc_path_snapshot[PS_APP_ROM_PATH_MAX_CHARS];
    const char *rtc_path_for_log;
    XStatus status;

    if ((ctx == NULL) || (ctx->rtc == NULL)) {
        return XST_FAILURE;
    }

    ctx->rtc->loaded = 0U;
    ctx->rtc->in_use = 0U;
    ctx->rtc->dirty = 0U;
    ctx->rtc->timestamp_saved = 0U;
    ctx->rtc->saved_time = 0ULL;
    ctx->rtc->last_timestamp_out = 0U;
    ctx->rtc->last_savedtime_out = 0ULL;
    ctx->rtc->path[0] = '\0';
    ctx->rtc->save_countdown = ctx->rtc->save_interval_ticks;

    PsAppRuntime_BuildRtcPath(rom_path, ctx->rtc->path, sizeof(ctx->rtc->path));
    PsAppRuntime_CopyText(rtc_path_snapshot, sizeof(rtc_path_snapshot), ctx->rtc->path);
    rtc_path_for_log = (rtc_path_snapshot[0] != '\0') ? rtc_path_snapshot : "(invalid)";
    if (ctx->rtc->path[0] == '\0') {
        PsAppRuntime_ApplyRtcSavedState(ctx);
        return XST_INVALID_PARAM;
    }

    memset(&record, 0, sizeof(record));
    memset(&read_result, 0, sizeof(read_result));
    /*
     * 只接受固定大小 RTC 记录。
     * 底层会把 empty/oversize 映射为 FR_INVALID_OBJECT，上层在此做细分日志与降级。
     */
    status = PsFatFsStorage_ReadFileToMemory(ctx->rtc->path,
                                             (UINTPTR)&record,
                                             sizeof(record),
                                             &read_result);
    if ((status == XST_SUCCESS) &&
        (read_result.bytes_loaded == sizeof(record)) &&
        (record.magic == PS_APP_RTC_FILE_MAGIC)) {
        ctx->rtc->timestamp_saved = record.timestamp_saved;
        ctx->rtc->saved_time =
            (((u64)(record.savedtime_hi_loaded & 0x3FFU)) << 32U) |
            ((u64)record.savedtime_lo);
        ctx->rtc->loaded = (u8)((record.savedtime_hi_loaded >> 10) & 0x1U);
        ctx->rtc->last_timestamp_out = ctx->rtc->timestamp_saved;
        ctx->rtc->last_savedtime_out = ctx->rtc->saved_time;
        PsAppRuntime_ApplyRtcSavedState(ctx);
        xil_printf("[RTC] preload %s loaded=%u ts=0x%08x st=0x%08x_%08x\r\n",
                   ctx->rtc->path,
                   (unsigned int)ctx->rtc->loaded,
                   (unsigned int)ctx->rtc->timestamp_saved,
                   (unsigned int)((ctx->rtc->saved_time >> 32U) & 0x3FFULL),
                   (unsigned int)(ctx->rtc->saved_time & 0xFFFFFFFFULL));
        return XST_SUCCESS;
    }

    if ((status != XST_SUCCESS) &&
        (read_result.fs_result != FR_NO_FILE) &&
        (read_result.fs_result != FR_NO_PATH)) {
        if (read_result.fs_result == FR_INVALID_OBJECT) {
            if (read_result.bytes_loaded == 0U) {
                xil_printf("[RTC] empty rtc ignored %s\r\n", rtc_path_for_log);
            } else {
                xil_printf("[RTC] invalid rtc record size %s (bytes=%u expect=%u)\r\n",
                           rtc_path_for_log,
                           (unsigned int)read_result.bytes_loaded,
                           (unsigned int)sizeof(record));
            }
        } else {
            xil_printf("[RTC] warning: read %s failed (%s)\r\n",
                       rtc_path_for_log,
                       PsFatFsStorage_StrError(read_result.fs_result));
        }
    } else if (status != XST_SUCCESS) {
        xil_printf("[RTC] no existing rtc for %s\r\n", rtc_path_for_log);
    } else {
        xil_printf("[RTC] invalid rtc record %s (magic=0x%08x bytes=%u)\r\n",
                   rtc_path_for_log,
                   (unsigned int)record.magic,
                   (unsigned int)read_result.bytes_loaded);
    }

    PsAppRuntime_ApplyRtcSavedState(ctx);
    return XST_SUCCESS;
}

static void PsAppRuntime_ServiceStateFeature(PsAppRuntimeContext *ctx) {
    u32 busy_bits;
    u32 slot;
    const char *state_path_ptr;
    char state_path[PS_APP_ROM_PATH_MAX_CHARS];
    XStatus status;

    if ((ctx == NULL) || (ctx->state_feature == NULL) || (ctx->regs == NULL) || (ctx->rom == NULL)) {
        return;
    }

    ctx->state_feature->feature_status = PsGbaRegs_ReadFeatureStatus(ctx->regs);
    busy_bits = (ctx->state_feature->feature_status & GBA_FEATURE_STATUS_BUSY_MASK) >>
                GBA_FEATURE_STATUS_BUSY_SHIFT;

    if (ctx->state_feature->save_pending != 0U) {
        if ((ctx->rom == NULL) || (ctx->rom->loaded == 0U) || (ctx->rom->is_loading != 0U)) {
            ctx->state_feature->save_pending = 0U;
            if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                ctx->state_feature->io_error_count++;
            }
            ctx->state_feature->io_last_result = 2U;
            xil_printf("[STATE] save ignored: rom not ready\r\n");
        } else if (ctx->state_feature->io_phase == PS_APP_STATE_IO_IDLE) {
            slot = PsAppRuntime_ClampStateSlot(ctx->state_feature->slot);
            ctx->state_feature->slot = (u8)slot;
            PsAppRuntime_BuildStatePath(ctx->rom->path, slot, state_path, sizeof(state_path));
            if (state_path[0] == '\0') {
                if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                    ctx->state_feature->io_error_count++;
                }
                ctx->state_feature->io_last_result = 3U;
                xil_printf("[STATE] save failed: invalid path\r\n");
            } else {
                PsGbaRegs_SetStateSlot(ctx->regs, slot);
                PsGbaRegs_TriggerSaveState(ctx->regs);
                ctx->state_feature->io_phase = PS_APP_STATE_IO_SAVE_WAIT_BUSY;
                ctx->state_feature->io_busy_seen = 0U;
                ctx->state_feature->io_wait_ticks = 0U;
                ctx->state_feature->io_timeout_ticks = PS_APP_STATE_IO_TIMEOUT_TICKS;
                ctx->state_feature->io_last_result = 0U;
                PsAppRuntime_CopyText(ctx->state_feature->last_state_path,
                                      sizeof(ctx->state_feature->last_state_path),
                                      state_path);
                xil_printf("[STATE] save begin slot=%u path=%s\r\n",
                           (unsigned int)slot,
                           state_path);
            }
            ctx->state_feature->save_pending = 0U;
        }
    }

    if (ctx->state_feature->load_pending != 0U) {
        if ((ctx->rom == NULL) || (ctx->rom->loaded == 0U) || (ctx->rom->is_loading != 0U)) {
            ctx->state_feature->load_pending = 0U;
            if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                ctx->state_feature->io_error_count++;
            }
            ctx->state_feature->io_last_result = 2U;
            xil_printf("[STATE] load ignored: rom not ready\r\n");
        } else if (ctx->state_feature->io_phase == PS_APP_STATE_IO_IDLE) {
            slot = PsAppRuntime_ClampStateSlot(ctx->state_feature->slot);
            ctx->state_feature->slot = (u8)slot;
            PsAppRuntime_BuildStatePath(ctx->rom->path, slot, state_path, sizeof(state_path));
            if (state_path[0] == '\0') {
                if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                    ctx->state_feature->io_error_count++;
                }
                ctx->state_feature->io_last_result = 3U;
                xil_printf("[STATE] load failed: invalid path\r\n");
            } else {
                status = PsAppRuntime_LoadStateSlotFromFile(ctx, slot, state_path);
                if (status == XST_SUCCESS) {
                    PsGbaRegs_SetStateSlot(ctx->regs, slot);
                    PsGbaRegs_TriggerLoadState(ctx->regs);
                    ctx->state_feature->io_phase = PS_APP_STATE_IO_LOAD_WAIT_BUSY;
                    ctx->state_feature->io_busy_seen = 0U;
                    ctx->state_feature->io_wait_ticks = 0U;
                    ctx->state_feature->io_timeout_ticks = PS_APP_STATE_IO_TIMEOUT_TICKS;
                    ctx->state_feature->io_last_result = 0U;
                    xil_printf("[STATE] load begin slot=%u path=%s\r\n",
                               (unsigned int)slot,
                               state_path);
                } else {
                    if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                        ctx->state_feature->io_error_count++;
                    }
                    ctx->state_feature->io_last_result = 3U;
                    xil_printf("[STATE] load failed: file read/format error (%s)\r\n", state_path);
                }
            }
            ctx->state_feature->load_pending = 0U;
        }
    }

    if (ctx->state_feature->cheat_clear_pending != 0U) {
        PsGbaRegs_TriggerCheatClear(ctx->regs);
        ctx->state_feature->cheat_clear_pending = 0U;
    }
    if (ctx->state_feature->cheat_push_pending != 0U) {
        PsGbaRegs_PushCheatWords(ctx->regs,
                                 ctx->state_feature->cheat_words[0],
                                 ctx->state_feature->cheat_words[1],
                                 ctx->state_feature->cheat_words[2],
                                 ctx->state_feature->cheat_words[3]);
        ctx->state_feature->cheat_push_pending = 0U;
    }

    if (ctx->state_feature->io_phase == PS_APP_STATE_IO_IDLE) {
        ctx->state_feature->io_wait_ticks = 0U;
        ctx->state_feature->io_timeout_ticks = 0U;
        ctx->state_feature->io_busy_seen = 0U;
        return;
    }

    if (ctx->state_feature->io_wait_ticks < 0xFFFFFFFFU) {
        ctx->state_feature->io_wait_ticks++;
    }
    if (ctx->state_feature->io_timeout_ticks > 0U) {
        ctx->state_feature->io_timeout_ticks--;
    }
    if (busy_bits != 0U) {
        ctx->state_feature->io_busy_seen = 1U;
    }

    switch ((PsAppStateIoPhase)ctx->state_feature->io_phase) {
        case PS_APP_STATE_IO_SAVE_WAIT_BUSY:
            if (ctx->state_feature->io_busy_seen != 0U) {
                ctx->state_feature->io_phase = PS_APP_STATE_IO_SAVE_WAIT_DONE;
                ctx->state_feature->io_wait_ticks = 0U;
                ctx->state_feature->io_timeout_ticks = PS_APP_STATE_IO_TIMEOUT_TICKS;
            } else if (ctx->state_feature->io_wait_ticks >= PS_APP_STATE_IO_FALLBACK_TICKS) {
                slot = PsAppRuntime_ClampStateSlot(ctx->state_feature->slot);
                state_path_ptr = PsAppRuntime_GetActiveStatePath(ctx,
                                                                 state_path,
                                                                 sizeof(state_path),
                                                                 slot);
                status = PsAppRuntime_SaveStateSlotToFile(ctx, slot, state_path_ptr);
                if (status == XST_SUCCESS) {
                    ctx->state_feature->io_last_result = 1U;
                    xil_printf("[STATE] save done (fallback) slot=%u path=%s\r\n",
                               (unsigned int)slot,
                               state_path_ptr);
                } else {
                    if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                        ctx->state_feature->io_error_count++;
                    }
                    ctx->state_feature->io_last_result = 4U;
                    xil_printf("[STATE] save failed (fallback) slot=%u\r\n", (unsigned int)slot);
                }
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
            } else if (ctx->state_feature->io_timeout_ticks == 0U) {
                if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                    ctx->state_feature->io_error_count++;
                }
                ctx->state_feature->io_last_result = 5U;
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
                xil_printf("[STATE] save timeout before busy\r\n");
            }
            break;

        case PS_APP_STATE_IO_SAVE_WAIT_DONE:
            if (busy_bits == 0U) {
                slot = PsAppRuntime_ClampStateSlot(ctx->state_feature->slot);
                state_path_ptr = PsAppRuntime_GetActiveStatePath(ctx,
                                                                 state_path,
                                                                 sizeof(state_path),
                                                                 slot);
                status = PsAppRuntime_SaveStateSlotToFile(ctx, slot, state_path_ptr);
                if (status == XST_SUCCESS) {
                    ctx->state_feature->io_last_result = 1U;
                    xil_printf("[STATE] save done slot=%u path=%s\r\n",
                               (unsigned int)slot,
                               state_path_ptr);
                } else {
                    if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                        ctx->state_feature->io_error_count++;
                    }
                    ctx->state_feature->io_last_result = 4U;
                    xil_printf("[STATE] save file write failed slot=%u\r\n", (unsigned int)slot);
                }
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
            } else if (ctx->state_feature->io_timeout_ticks == 0U) {
                if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                    ctx->state_feature->io_error_count++;
                }
                ctx->state_feature->io_last_result = 5U;
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
                xil_printf("[STATE] save timeout while busy\r\n");
            }
            break;

        case PS_APP_STATE_IO_LOAD_WAIT_BUSY:
            if (ctx->state_feature->io_busy_seen != 0U) {
                ctx->state_feature->io_phase = PS_APP_STATE_IO_LOAD_WAIT_DONE;
                ctx->state_feature->io_wait_ticks = 0U;
                ctx->state_feature->io_timeout_ticks = PS_APP_STATE_IO_TIMEOUT_TICKS;
            } else if (ctx->state_feature->io_wait_ticks >= PS_APP_STATE_IO_FALLBACK_TICKS) {
                ctx->state_feature->io_last_result = 1U;
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
                xil_printf("[STATE] load done (fallback)\r\n");
            } else if (ctx->state_feature->io_timeout_ticks == 0U) {
                if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                    ctx->state_feature->io_error_count++;
                }
                ctx->state_feature->io_last_result = 5U;
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
                xil_printf("[STATE] load timeout before busy\r\n");
            }
            break;

        case PS_APP_STATE_IO_LOAD_WAIT_DONE:
            if (busy_bits == 0U) {
                ctx->state_feature->io_last_result = 1U;
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
                xil_printf("[STATE] load done\r\n");
            } else if (ctx->state_feature->io_timeout_ticks == 0U) {
                if (ctx->state_feature->io_error_count < 0xFFFFFFFFU) {
                    ctx->state_feature->io_error_count++;
                }
                ctx->state_feature->io_last_result = 5U;
                ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
                xil_printf("[STATE] load timeout while busy\r\n");
            }
            break;

        default:
            ctx->state_feature->io_phase = PS_APP_STATE_IO_IDLE;
            break;
    }
}

static void PsAppRuntime_ServiceRtcPersist(PsAppRuntimeContext *ctx) {
    u32 timestamp_out;
    u64 savedtime_out;

    if ((ctx == NULL) || (ctx->rtc == NULL) || (ctx->rom == NULL) || (ctx->regs == NULL) ||
        (ctx->state_feature == NULL)) {
        return;
    }
    if ((ctx->rom->loaded == 0U) || (ctx->rom->is_loading != 0U)) {
        return;
    }

    ctx->rtc->in_use = (u8)(((ctx->state_feature->feature_status & GBA_FEATURE_STATUS_RTC_INUSE) != 0U) ? 1U : 0U);
    if (ctx->rtc->in_use == 0U) {
        return;
    }

    timestamp_out = PsGbaRegs_ReadRtcTimestampOut(ctx->regs);
    savedtime_out = PsGbaRegs_ReadRtcSavedTimeOut(ctx->regs);
    if ((timestamp_out != ctx->rtc->last_timestamp_out) ||
        (savedtime_out != ctx->rtc->last_savedtime_out)) {
        ctx->rtc->last_timestamp_out = timestamp_out;
        ctx->rtc->last_savedtime_out = savedtime_out;
        ctx->rtc->timestamp_saved = timestamp_out;
        ctx->rtc->saved_time = savedtime_out;
        ctx->rtc->loaded = 1U;
        ctx->rtc->dirty = 1U;
        ctx->rtc->save_countdown = ctx->rtc->save_interval_ticks;
    }

    if (ctx->rtc->dirty == 0U) {
        return;
    }
    if (ctx->rtc->save_countdown != 0U) {
        ctx->rtc->save_countdown--;
    }
    if (ctx->rtc->save_countdown == 0U) {
        (void)PsAppRuntime_FlushRtcToFile(ctx);
    }
}

static void PsAppRuntime_ServiceRumble(PsAppRuntimeContext *ctx) {
    u8 core_rumble_on;
    u8 target_rumble;
    u8 desired_strength;

    if ((ctx == NULL) || (ctx->sensor == NULL) || (ctx->state_feature == NULL)) {
        return;
    }

    core_rumble_on = (u8)(((ctx->state_feature->feature_status & GBA_FEATURE_STATUS_RUMBLE) != 0U) ? 1U : 0U);
    target_rumble = (u8)(((ctx->sensor->rumble_enabled != 0U) && (core_rumble_on != 0U)) ? 1U : 0U);

    if ((target_rumble == ctx->sensor->rumble_active) && (ctx->sensor->rumble_dirty == 0U)) {
        return;
    }

    desired_strength = (target_rumble != 0U) ? ctx->sensor->rumble_strength : (u8)PS_APP_RUMBLE_STRENGTH_OFF;
    if ((ctx->usb_host_ctx != NULL) &&
        (ctx->usb_host != NULL) &&
        (ctx->usb_host->xbox_interface_active != 0U)) {
        if (PsAppUsbHost_SetRumble(ctx->usb_host_ctx, desired_strength, 0U) == XST_SUCCESS) {
            if (ctx->sensor->rumble_change_count < 0xFFFFFFFFU) {
                ctx->sensor->rumble_change_count++;
            }
        }
    }

    ctx->sensor->rumble_active = target_rumble;
    ctx->sensor->rumble_dirty = 0U;
}

static void PsAppRuntime_ServiceSensorFast(PsAppRuntimeContext *ctx) {
    u8 next_solar;
    s8 next_tilt_x;
    s8 next_tilt_y;

    if ((ctx == NULL) || (ctx->sensor == NULL) || (ctx->rom == NULL) || (ctx->input == NULL)) {
        return;
    }

    next_solar = (ctx->sensor->solar > PS_APP_SENSOR_SOLAR_MAX) ?
                 (u8)PS_APP_SENSOR_SOLAR_MAX : ctx->sensor->solar;
    next_tilt_x = ctx->sensor->tilt_x;
    next_tilt_y = ctx->sensor->tilt_y;

    if ((ctx->input->active != 0U) && (ctx->input->report_valid != 0U)) {
        if (ctx->rom->quirk_solar != 0U) {
            next_solar = (u8)(ctx->input->rt >> 5);
            if (next_solar > PS_APP_SENSOR_SOLAR_MAX) {
                next_solar = (u8)PS_APP_SENSOR_SOLAR_MAX;
            }
        }
        if (ctx->rom->quirk_tilt != 0U) {
            next_tilt_x = PsAppRuntime_ClampS16ToS8((s16)(ctx->input->lx / PS_APP_SENSOR_TILT_DIVISOR));
            next_tilt_y = PsAppRuntime_ClampS16ToS8((s16)(ctx->input->ly / PS_APP_SENSOR_TILT_DIVISOR));
        }
    }

    if ((next_solar != ctx->sensor->solar) ||
        (next_tilt_x != ctx->sensor->tilt_x) ||
        (next_tilt_y != ctx->sensor->tilt_y)) {
        ctx->sensor->solar = next_solar;
        ctx->sensor->tilt_x = next_tilt_x;
        ctx->sensor->tilt_y = next_tilt_y;
    }

    PsAppRuntime_ApplySensorState(ctx, 0U);
}

static const char *PsAppRuntime_BiosModeName(u8 bios_mode) {
    switch ((PsAppBiosMode)bios_mode) {
        case PS_APP_BIOS_MODE_EXTERNAL:
            return "external";
        case PS_APP_BIOS_MODE_FALLBACK:
            return "fallback";
        case PS_APP_BIOS_MODE_INTERNAL:
        default:
            return "internal";
    }
}

static u32 PsAppRuntime_ReadLe32(const u8 *data) {
    if (data == NULL) {
        return 0U;
    }

    return ((u32)data[0]) |
           ((u32)data[1] << 8) |
           ((u32)data[2] << 16) |
           ((u32)data[3] << 24);
}

static int PsAppRuntime_LoadExternalBios(PsAppRuntimeContext *ctx,
                                         u8 *external_present_out,
                                         u32 *bytes_loaded_out,
                                         const char **reason_out) {
    static u8 s_bios_image[PS_APP_GBA_BIOS_BYTES];
    PsFatFsStorageReadResult read_result;
    XStatus status;
    u32 word_idx;
    u8 external_present;

    if (external_present_out != NULL) {
        *external_present_out = 0U;
    }
    if (bytes_loaded_out != NULL) {
        *bytes_loaded_out = 0U;
    }
    if (reason_out != NULL) {
        *reason_out = "none";
    }

    if ((ctx == NULL) || (ctx->regs == NULL)) {
        if (reason_out != NULL) {
            *reason_out = "invalid_ctx";
        }
        return -1;
    }

    memset(&read_result, 0, sizeof(read_result));
    status = PsFatFsStorage_ReadFileToMemory(PS_APP_BIOS_SD_PATH,
                                             (UINTPTR)s_bios_image,
                                             PS_APP_GBA_BIOS_BYTES,
                                             &read_result);
    if (status != XST_SUCCESS) {
        const char *read_reason;

        external_present = (u8)(((read_result.fs_result != FR_NO_FILE) &&
                                 (read_result.fs_result != FR_NO_PATH)) ? 1U : 0U);
        read_reason = PsFatFsStorage_StrError(read_result.fs_result);
        if ((external_present != 0U) && (read_result.fs_result == FR_OK)) {
            read_reason = "size_invalid";
        }
        if (external_present_out != NULL) {
            *external_present_out = external_present;
        }
        if (reason_out != NULL) {
            *reason_out = read_reason;
        }
        return 0;
    }

    if (external_present_out != NULL) {
        *external_present_out = 1U;
    }
    if (bytes_loaded_out != NULL) {
        *bytes_loaded_out = read_result.bytes_loaded;
    }
    if (read_result.bytes_loaded != PS_APP_GBA_BIOS_BYTES) {
        if (reason_out != NULL) {
            *reason_out = "size_invalid";
        }
        return 0;
    }

    for (word_idx = 0U; word_idx < PS_APP_GBA_BIOS_WORDS; ++word_idx) {
        const u8 *word_ptr;
        u32 word_data;

        word_ptr = &s_bios_image[word_idx * 4U];
        word_data = PsAppRuntime_ReadLe32(word_ptr);
        status = PsGbaRegs_WriteBiosWord(ctx->regs,
                                         word_idx,
                                         word_data,
                                         PS_APP_GBA_BIOS_WRITE_TIMEOUT_LOOPS);
        if (status != XST_SUCCESS) {
            if (reason_out != NULL) {
                *reason_out = "pl_ack_timeout";
            }
            return -1;
        }
    }

    if (reason_out != NULL) {
        *reason_out = "ok";
    }
    return 1;
}

static XStatus PsAppRuntime_PrepareBiosForRom(PsAppRuntimeContext *ctx) {
    int load_result;
    u8 external_present;
    u32 bytes_loaded;
    const char *reason;

    if ((ctx == NULL) || (ctx->rom == NULL)) {
        return XST_FAILURE;
    }

    ctx->rom->bios_mode = (u8)PS_APP_BIOS_MODE_INTERNAL;
    ctx->rom->bios_load_ok = 0U;
    ctx->rom->bios_external_present = 0U;
    ctx->rom->bios_bytes_loaded = 0U;

    external_present = 0U;
    bytes_loaded = 0U;
    reason = "none";
    load_result = PsAppRuntime_LoadExternalBios(ctx,
                                                &external_present,
                                                &bytes_loaded,
                                                &reason);

    ctx->rom->bios_external_present = external_present;
    ctx->rom->bios_bytes_loaded = bytes_loaded;

    if (load_result > 0) {
        ctx->rom->bios_mode = (u8)PS_APP_BIOS_MODE_EXTERNAL;
        ctx->rom->bios_load_ok = 1U;
        xil_printf("[BIOS] mode=%s path=%s bytes=%u\r\n",
                   PsAppRuntime_BiosModeName(ctx->rom->bios_mode),
                   PS_APP_BIOS_SD_PATH,
                   (unsigned int)ctx->rom->bios_bytes_loaded);
        return XST_SUCCESS;
    }

    ctx->rom->bios_mode = (u8)PS_APP_BIOS_MODE_FALLBACK;
    ctx->rom->bios_load_ok = 0U;
    if (load_result == 0) {
        xil_printf("[BIOS] mode=%s path=%s reason=%s (use internal BIOS)\r\n",
                   PsAppRuntime_BiosModeName(ctx->rom->bios_mode),
                   PS_APP_BIOS_SD_PATH,
                   (reason != NULL) ? reason : "unknown");
        return XST_SUCCESS;
    }

    xil_printf("[BIOS] mode=%s fatal reason=%s\r\n",
               PsAppRuntime_BiosModeName(ctx->rom->bios_mode),
               (reason != NULL) ? reason : "unknown");
    return XST_FAILURE;
}

static u8 PsAppRuntime_Code3Eq(const char *code, const char *prefix3) {
    if ((code == NULL) || (prefix3 == NULL)) {
        return 0U;
    }
    return (memcmp(code, prefix3, 3U) == 0) ? 1U : 0U;
}

static u8 PsAppRuntime_Code4Eq(const char *code, const char *exact4) {
    if ((code == NULL) || (exact4 == NULL)) {
        return 0U;
    }
    return (memcmp(code, exact4, 4U) == 0) ? 1U : 0U;
}

static u8 PsAppRuntime_Code4InList(const char *code,
                                   const char *const *list,
                                   size_t count) {
    size_t idx;

    if ((code == NULL) || (list == NULL)) {
        return 0U;
    }

    for (idx = 0U; idx < count; ++idx) {
        if (PsAppRuntime_Code4Eq(code, list[idx]) != 0U) {
            return 1U;
        }
    }

    return 0U;
}

static void PsAppRuntime_ApplyRomDetection(PsAppRuntimeContext *ctx,
                                           const PsRomLoadResult *load_result) {
    static const char *const s_remap_codes[] = {
        "FBME", "FADE", "FDKE", "FDME", "FEBE", "FICE", "FMRE", "FP7E",
        "FSME", "FZLE", "FXVE", "FLBE", "FSRJ", "FGZJ", "FSDJ", "FADJ",
        "FM2J", "FGGJ", "FTWJ", "FMKJ", "FTBJ", "FDDJ", "FDMJ", "FWCJ",
        "FVFJ", "FCLJ", "FMBJ", "FSOJ", "FBMJ", "FMPJ", "FXVJ", "FZLJ",
        "FEBJ", "FICJ", "FDKJ", "FSMJ"
    };
    const char *game_code;
    u8 quirk_remap;
    u8 quirk_sram_disable;
    u8 quirk_gpio;
    u8 quirk_tilt;
    u8 quirk_solar;

    if ((ctx == NULL) || (load_result == NULL)) {
        return;
    }

    game_code = load_result->game_code;

    ctx->rom->sig_flash1m = (load_result->flash1m_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->sig_flash = (load_result->flash_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->sig_sram = (load_result->sram_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->sig_eeprom = (load_result->eeprom_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->flash1m_offset = load_result->flash1m_offset;
    ctx->rom->flash_offset = load_result->flash_offset;
    ctx->rom->sram_offset = load_result->sram_offset;
    ctx->rom->eeprom_offset = load_result->eeprom_offset;
    PsAppRuntime_CopyText(ctx->rom->game_code, sizeof(ctx->rom->game_code), load_result->game_code);
    PsAppRuntime_CopyText(ctx->rom->maker_code, sizeof(ctx->rom->maker_code), load_result->maker_code);

    quirk_remap = PsAppRuntime_Code4InList(game_code,
                                           s_remap_codes,
                                           sizeof(s_remap_codes) / sizeof(s_remap_codes[0]));
    quirk_sram_disable =
        PsAppRuntime_Code3Eq(game_code, "AR8") || PsAppRuntime_Code3Eq(game_code, "ARO") ||
        PsAppRuntime_Code3Eq(game_code, "ALG") || PsAppRuntime_Code3Eq(game_code, "ALF") ||
        PsAppRuntime_Code3Eq(game_code, "BLF") || PsAppRuntime_Code3Eq(game_code, "BDB") ||
        PsAppRuntime_Code3Eq(game_code, "BG3") || PsAppRuntime_Code3Eq(game_code, "BDV") ||
        PsAppRuntime_Code3Eq(game_code, "A2Y") || PsAppRuntime_Code3Eq(game_code, "AI2") ||
        PsAppRuntime_Code3Eq(game_code, "BT4") || quirk_remap;
    quirk_gpio =
        PsAppRuntime_Code3Eq(game_code, "BPE") || PsAppRuntime_Code3Eq(game_code, "AXV") ||
        PsAppRuntime_Code3Eq(game_code, "AXP") || PsAppRuntime_Code3Eq(game_code, "RZW") ||
        PsAppRuntime_Code3Eq(game_code, "BKA") || PsAppRuntime_Code3Eq(game_code, "BR4") ||
        PsAppRuntime_Code3Eq(game_code, "V49") || PsAppRuntime_Code3Eq(game_code, "2GB") ||
        PsAppRuntime_Code3Eq(game_code, "U3I") || PsAppRuntime_Code3Eq(game_code, "U32") ||
        PsAppRuntime_Code3Eq(game_code, "U33");
    quirk_tilt =
        PsAppRuntime_Code3Eq(game_code, "KHP") || PsAppRuntime_Code3Eq(game_code, "KYG");
    quirk_solar =
        PsAppRuntime_Code3Eq(game_code, "U3I") ||
        PsAppRuntime_Code3Eq(game_code, "U32") ||
        PsAppRuntime_Code3Eq(game_code, "U33");

    ctx->rom->quirk_remap = quirk_remap;
    ctx->rom->quirk_sram_disable = quirk_sram_disable;
    ctx->rom->quirk_gpio = quirk_gpio;
    ctx->rom->quirk_tilt = quirk_tilt;
    ctx->rom->quirk_solar = quirk_solar;

    ctx->config->ctrl &= ~(GBA_CTRL_MEMORY_REMAP |
                           GBA_CTRL_FLASH_1M |
                           GBA_CTRL_SPECIAL_GPIO |
                           GBA_CTRL_TILT);
    ctx->config->ctrl |= GBA_CTRL_SRAM_FLASH_EN;

    if (quirk_remap != 0U) {
        ctx->config->ctrl |= GBA_CTRL_MEMORY_REMAP;
    }
    if (ctx->rom->sig_flash1m != 0U) {
        ctx->config->ctrl |= GBA_CTRL_FLASH_1M;
    }
    if (quirk_gpio != 0U) {
        ctx->config->ctrl |= GBA_CTRL_SPECIAL_GPIO;
    }
    if (quirk_tilt != 0U) {
        ctx->config->ctrl |= GBA_CTRL_TILT;
    }
    if (quirk_sram_disable != 0U) {
        ctx->config->ctrl &= ~GBA_CTRL_SRAM_FLASH_EN;
    }

    xil_printf("[ROMCFG] code=%s maker=%s sig flash1m=%u flash=%u sram=%u eeprom=%u\r\n",
               ctx->rom->game_code[0] != '\0' ? ctx->rom->game_code : "....",
               ctx->rom->maker_code[0] != '\0' ? ctx->rom->maker_code : "..",
               (unsigned int)ctx->rom->sig_flash1m,
               (unsigned int)ctx->rom->sig_flash,
               (unsigned int)ctx->rom->sig_sram,
               (unsigned int)ctx->rom->sig_eeprom);
    xil_printf("[ROMCFG] quirk remap=%u sram_dis=%u gpio=%u tilt=%u solar=%u ctrl=0x%08x\r\n",
               (unsigned int)ctx->rom->quirk_remap,
               (unsigned int)ctx->rom->quirk_sram_disable,
               (unsigned int)ctx->rom->quirk_gpio,
               (unsigned int)ctx->rom->quirk_tilt,
               (unsigned int)ctx->rom->quirk_solar,
               (unsigned int)ctx->config->ctrl);
}

static int PsAppRuntime_ResolveRomPath(const char *input,
                                       char *resolved_path,
                                       size_t resolved_path_size) {
    const char *effective_path;
    size_t input_len;

    if ((resolved_path == NULL) || (resolved_path_size == 0U)) {
        return -1;
    }

    effective_path = (input != NULL) ? input : PS_APP_DEFAULT_ROM_SD_PATH;
    input_len = strlen(effective_path);

    if (strstr(effective_path, ":/") != NULL) {
        if (input_len >= resolved_path_size) {
            return -1;
        }
        PsAppRuntime_CopyText(resolved_path, resolved_path_size, effective_path);
        return 0;
    }

    if ((input_len + 3U) >= resolved_path_size) {
        return -1;
    }

    resolved_path[0] = '0';
    resolved_path[1] = ':';
    resolved_path[2] = '/';
    memcpy(&resolved_path[3], effective_path, input_len);
    resolved_path[input_len + 3U] = '\0';
    return 0;
}

static void PsAppRuntime_PrintPhysicalKeys(u32 keys_mask) {
    static const char *kNames[10] = {
        "A", "B", "SELECT", "START", "RIGHT",
        "LEFT", "UP", "DOWN", "R", "L"
    };
    u32 idx;
    u32 any;

    xil_printf("[INPUT] phys=0x%03x", (unsigned int)(keys_mask & 0x3FFU));
    any = 0U;
    for (idx = 0U; idx < 10U; ++idx) {
        if ((keys_mask & (1U << idx)) != 0U) {
            xil_printf("%s%s", any != 0U ? "+" : " ", kNames[idx]);
            any = 1U;
        }
    }
    if (any == 0U) {
        xil_printf(" (idle)");
    }
    xil_printf("\r\n");
}

static const char *PsAppRuntime_GbaKeyNameByBit(u32 bit_index) {
    static const char *kNames[10] = {
        "A", "B", "SELECT", "START", "RIGHT",
        "LEFT", "UP", "DOWN", "R", "L"
    };

    if (bit_index < 10U) {
        return kNames[bit_index];
    }
    return NULL;
}

static const char *PsAppRuntime_PadKeyNameByBit(u32 bit_index) {
    switch (bit_index) {
        case 0U: return "DPAD_UP";
        case 1U: return "DPAD_DOWN";
        case 2U: return "DPAD_LEFT";
        case 3U: return "DPAD_RIGHT";
        case 4U: return "START";
        case 5U: return "BACK";
        case 6U: return "L3";
        case 7U: return "R3";
        case 8U: return "LB";
        case 9U: return "RB";
        case 10U: return "GUIDE";
        case 12U: return "A_BOTTOM";
        case 13U: return "B_RIGHT";
        case 14U: return "X_LEFT";
        case 15U: return "Y_TOP";
        case 16U: return "LT";
        case 17U: return "RT";
        case 18U: return "LS_RIGHT";
        case 19U: return "LS_LEFT";
        case 20U: return "LS_UP";
        case 21U: return "LS_DOWN";
        default: break;
    }
    return NULL;
}

static void PsAppRuntime_PrintMaskNamed(const char *tag,
                                        u32 mask,
                                        const char *(*name_of_bit)(u32),
                                        u32 max_bit) {
    u32 bit;
    u8 any;
    const char *name;

    xil_printf(" %s=", tag);
    any = 0U;
    for (bit = 0U; bit <= max_bit; ++bit) {
        if ((mask & (1UL << bit)) == 0U) {
            continue;
        }
        name = name_of_bit(bit);
        if (name == NULL) {
            continue;
        }
        xil_printf("%s%s", (any != 0U) ? "+" : "", name);
        any = 1U;
    }
    if (any == 0U) {
        xil_printf("-");
    }
}

static void PsAppRuntime_PrintInputDelta(PsAppRuntimeContext *ctx,
                                         u32 prev_pad_source_mask,
                                         u32 curr_pad_source_mask,
                                         u32 prev_gba_keys,
                                         u32 curr_gba_keys) {
    u32 pad_pressed;
    u32 pad_released;
    u32 gba_pressed;
    u32 gba_released;

    if ((ctx == NULL) || (ctx->input == NULL)) {
        return;
    }

    pad_pressed = curr_pad_source_mask & ~prev_pad_source_mask;
    pad_released = prev_pad_source_mask & ~curr_pad_source_mask;
    gba_pressed = curr_gba_keys & ~prev_gba_keys;
    gba_released = prev_gba_keys & ~curr_gba_keys;

    xil_printf("[INPUT] map ");
    PsAppRuntime_PrintMaskNamed("pad_press", pad_pressed, PsAppRuntime_PadKeyNameByBit, 21U);
    PsAppRuntime_PrintMaskNamed("pad_release", pad_released, PsAppRuntime_PadKeyNameByBit, 21U);
    PsAppRuntime_PrintMaskNamed("gba_press", gba_pressed, PsAppRuntime_GbaKeyNameByBit, 9U);
    PsAppRuntime_PrintMaskNamed("gba_release", gba_released, PsAppRuntime_GbaKeyNameByBit, 9U);
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    xil_printf(" ab_map=A<=B_RIGHT,B<=A_BOTTOM");
#else
    xil_printf(" ab_map=A<=A_BOTTOM,B<=B_RIGHT");
#endif
    xil_printf(" src=0x%08x gba=0x%03x\r\n",
               (unsigned int)curr_pad_source_mask,
               (unsigned int)(curr_gba_keys & 0x3FFU));
}

static void PsAppRuntime_PrintPsButtons(u32 ps_btn_mask) {
    xil_printf("[BTN] ps BTN4=%u BTN5=%u mask=0x%02x\r\n",
               (unsigned int)((ps_btn_mask & PS_APP_BTN4_MASK) != 0U),
               (unsigned int)((ps_btn_mask & PS_APP_BTN5_MASK) != 0U),
               (unsigned int)(ps_btn_mask & (PS_APP_BTN4_MASK | PS_APP_BTN5_MASK)));
}

static u8 PsAppRuntime_ClampAudioVolumePercent(u32 volume_percent) {
    /* 所有外部入口统一钳制到 0..100，避免异常值进入 codec 映射。 */
    if (volume_percent > PS_APP_AUDIO_VOLUME_MAX) {
        return (u8)PS_APP_AUDIO_VOLUME_MAX;
    }
    return (u8)volume_percent;
}

static u8 PsAppRuntime_EncodeCodecVolume(u8 volume_percent) {
    u32 codec_range;

    if (volume_percent == 0U) {
        return PS_APP_AUDIO_CODEC_MIN_DB_VOL;
    }

    /*
     * SSM2603 (Rev.D) R2/R3 音量控制定义：
     * - 0x30 = -73 dB
     * - 0x79 = 0 dB (默认)
     * - 0x7F = +6 dB
     *
     * 行业常见做法是把 UI 的 100% 定义为 0 dB（unity gain），
     * 只在“增益/Boost”场景才进入 >0 dB 区间，避免默认档位过驱和失真风险。
     * 因此这里将 1..100 映射到 [-73 dB, 0 dB] => [0x30, 0x79]。
     */
    codec_range = (u32)PS_APP_AUDIO_CODEC_UNITY_VOL - (u32)PS_APP_AUDIO_CODEC_MIN_DB_VOL;
    return (u8)((u32)PS_APP_AUDIO_CODEC_MIN_DB_VOL +
                (((u32)volume_percent * codec_range) + (PS_APP_AUDIO_VOLUME_MAX / 2U)) /
                    PS_APP_AUDIO_VOLUME_MAX);
}

/*
 * Zybo Z7-20 上这两个 PS 按键最终应工作在 0x00000200 配置下。
 * 如果启动后仍保留旧的 0x00001201，按钮输入会被内部上拉扰乱，
 * 所以这里在 XGpioPs 初始化完成后再强制写一次，确保应用层读到真实电平。
 */
static void PsAppRuntime_ForcePsButtonMioConfig(void) {
    Xil_Out32(PS_APP_SLCR_UNLOCK_ADDR, PS_APP_SLCR_UNLOCK_CODE);
    Xil_Out32(PS_APP_MIO50_CFG_ADDR, PS_APP_MIO_GPIO_INPUT_CFG);
    Xil_Out32(PS_APP_MIO51_CFG_ADDR, PS_APP_MIO_GPIO_INPUT_CFG);
    Xil_Out32(PS_APP_SLCR_LOCK_ADDR, PS_APP_SLCR_LOCK_CODE);
}

/*
 * 这个调试接口保留 raw50/raw51、bank1、dir1、outen1 和 mio50/mio51，
 * 是为了以后再次遇到“按键没反应”时，能够一步区分：
 * 1) 应用逻辑问题；
 * 2) GPIO 方向/输出使能问题；
 * 3) MIO/SLCR 配置残留问题。
 */
void PsAppRuntime_ReadPsButtonRawLevels(PsAppRuntimeContext *ctx,
                                        u32 *btn4_level,
                                        u32 *btn5_level,
                                        u32 *bank1_data,
                                        u32 *bank1_dir,
                                        u32 *bank1_outen,
                                        u32 *mio50_cfg,
                                        u32 *mio51_cfg) {
    if (btn4_level != NULL) {
        *btn4_level = 0U;
    }
    if (btn5_level != NULL) {
        *btn5_level = 0U;
    }
    if (bank1_data != NULL) {
        *bank1_data = 0U;
    }
    if (bank1_dir != NULL) {
        *bank1_dir = 0U;
    }
    if (bank1_outen != NULL) {
        *bank1_outen = 0U;
    }
    if (mio50_cfg != NULL) {
        *mio50_cfg = 0U;
    }
    if (mio51_cfg != NULL) {
        *mio51_cfg = 0U;
    }

    if ((ctx == NULL) || (ctx->ps_gpio == NULL) || (ctx->ps_gpio_ready == 0U)) {
        return;
    }

#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    if (btn4_level != NULL) {
        *btn4_level = XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN);
    }
    if (btn5_level != NULL) {
        *btn5_level = XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN);
    }
    if (bank1_data != NULL) {
        *bank1_data = XGpioPs_Read(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (bank1_dir != NULL) {
        *bank1_dir = XGpioPs_GetDirection(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (bank1_outen != NULL) {
        *bank1_outen = XGpioPs_GetOutputEnable(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (mio50_cfg != NULL) {
        *mio50_cfg = Xil_In32(PS_APP_MIO50_CFG_ADDR);
    }
    if (mio51_cfg != NULL) {
        *mio51_cfg = Xil_In32(PS_APP_MIO51_CFG_ADDR);
    }
#endif
}

u32 PsAppRuntime_ReadPsButtonMask(PsAppRuntimeContext *ctx) {
    u32 ps_btn_mask;

    if ((ctx == NULL) || (ctx->ps_gpio == NULL) || (ctx->ps_gpio_ready == 0U)) {
        return 0U;
    }

    ps_btn_mask = 0U;
#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    /*
     * 当前板上修复后的实际行为是：空闲为低电平，按下为高电平。
     * 因此这里按 active-high 解释 BTN4/BTN5，和 raw50/raw51 的诊断输出保持一致。
     */
    if (XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN) != 0U) {
        ps_btn_mask |= PS_APP_BTN4_MASK;
    }
    if (XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN) != 0U) {
        ps_btn_mask |= PS_APP_BTN5_MASK;
    }
#endif
    return ps_btn_mask;
}

XStatus PsAppRuntime_ApplyAudioOutputState(PsAppRuntimeContext *ctx) {
    XStatus status;
    u8 codec_volume;
    u8 effective_mute;

    if ((ctx == NULL) || (ctx->audio == NULL)) {
        return XST_FAILURE;
    }

    ctx->audio->volume = PsAppRuntime_ClampAudioVolumePercent(ctx->audio->volume);
    /* codec 尚未就绪时仅保留状态，不报错，后续 ProgramAudio 会重新下发。 */
    if ((ctx->codec == NULL) || (ctx->codec->is_ready == 0U)) {
        return XST_SUCCESS;
    }

    /* 先下发音量，再根据 mute/volume=0 统一决定静音位。 */
    codec_volume = PsAppRuntime_EncodeCodecVolume(ctx->audio->volume);
    status = PsAudioCodec_SetHeadphoneVolume(ctx->codec, codec_volume);
    if (status != XST_SUCCESS) {
        return status;
    }

    effective_mute = ((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)) ? 1U : 0U;
    return PsAudioCodec_SetMute(ctx->codec, effective_mute);
}

XStatus PsAppRuntime_SetAudioVolume(PsAppRuntimeContext *ctx, u32 volume_percent) {
    XStatus status;

    if ((ctx == NULL) || (ctx->audio == NULL)) {
        return XST_FAILURE;
    }

    ctx->audio->volume = PsAppRuntime_ClampAudioVolumePercent(volume_percent);
    status = PsAppRuntime_ApplyAudioOutputState(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XST_SUCCESS;
}

XStatus PsAppRuntime_InitPsGpio(PsAppRuntimeContext *ctx) {
    XStatus status;
#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    XGpioPs_Config *cfg;
#endif

    if ((ctx == NULL) || (ctx->ps_gpio == NULL)) {
        return XST_FAILURE;
    }

    ctx->ps_gpio_ready = 0U;
    ctx->ps_btn_last_mask = 0U;

#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
#if defined(SDT)
    cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_BASEADDR);
#else
    cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_DEVICE_ID);
#endif
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XGpioPs_CfgInitialize(ctx->ps_gpio, cfg, cfg->BaseAddr);
    if (status != XST_SUCCESS) {
        return status;
    }

    PsAppRuntime_ForcePsButtonMioConfig();

    XGpioPs_SetDirectionPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN, 0U);
    XGpioPs_SetOutputEnablePin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN, 0U);
    XGpioPs_SetDirectionPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN, 0U);
    XGpioPs_SetOutputEnablePin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN, 0U);

    ctx->ps_gpio_ready = 1U;
    ctx->ps_btn_last_mask = PsAppRuntime_ReadPsButtonMask(ctx);
    return XST_SUCCESS;
#else
    (void)status;
    xil_printf("[INIT] 8 warning: BSP has no XGPIOPS instance, regenerate platform from updated XSA\r\n");
    return XST_SUCCESS;
#endif
}

static void PsAppRuntime_InitDefaults(PsAppRuntimeContext *ctx) {
    ctx->config->ctrl =
        (PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL) | PS_APP_GBA_CTRL_BOOT_REQUIRED) &
        ~GBA_CTRL_CORE_ON;
    ctx->config->ctrl &= GBA_CTRL_VALID_MASK;
    ctx->config->keys = 0U;
    ctx->config->max_pak_addr = 0U;
    ctx->config->cycle_precalc = 100U;
    ctx->config->rtc_timestamp = 0U;
    ctx->config->irq_enable = PS_APP_IRQ_MASK_DEFAULT;

    ctx->audio->sample_rate_hz = 48000U;
    ctx->audio->bits_per_sample = 16U;
    ctx->audio->mute = 0U;
    /* 默认音量：开机即为用户可接受的中档位。 */
    ctx->audio->volume = (u8)PS_APP_AUDIO_DEFAULT_VOLUME;

    ctx->video->display_frame_idx = 0xFFU;
    ctx->video->pending_frame_idx = 0xFFU;
    ctx->video->fbcap_last_frame_seq = 0U;
    ctx->video->fbcap_last_buf_idx = 0xFFU;
    ctx->video->fbcap_last_frame_idx = 0xFFU;
    ctx->video->blit_count = 0U;
    ctx->video->blit_last_us = 0U;
    ctx->video->blit_max_us = 0U;
    ctx->video->blit_total_us = 0U;
    ctx->video->blit_seq_gap_max = 0U;
    ctx->video->blit_seq_glitch_drop = 0U;
    ctx->video->fx.interframe_mode = (u8)PS_APP_VIDEO_INTERFRAME_DEFAULT;
    if (ctx->video->fx.interframe_mode > PS_APP_VIDEO_INTERFRAME_30HZ) {
        ctx->video->fx.interframe_mode = (u8)PS_APP_VIDEO_INTERFRAME_OFF;
    }
    ctx->video->fx.shade_mode = (u8)PS_APP_VIDEO_SHADE_DEFAULT;
    if (ctx->video->fx.shade_mode > 4U) {
        ctx->video->fx.shade_mode = 0U;
    }
    ctx->video->fx.non_eq_hd2x_hint = 1U;
    ctx->video->fx.non_eq_maxpixels_hint = 1U;
    ctx->video->fx.frame30_phase = 0U;
    ctx->video->fx.prev_capture_valid = 0U;
    ctx->video->fx.reserved0 = 0U;
    ctx->video->fx.reserved1 = 0U;

    ctx->state_feature->slot = 0U;
    ctx->state_feature->rewind_enable = 0U;
    ctx->state_feature->rewind_active = 0U;
    ctx->state_feature->cheats_enabled = 0U;
    ctx->state_feature->save_pending = 0U;
    ctx->state_feature->load_pending = 0U;
    ctx->state_feature->cheat_push_pending = 0U;
    ctx->state_feature->cheat_clear_pending = 0U;
    ctx->state_feature->feature_status = 0U;
    ctx->state_feature->cheat_words[0] = 0U;
    ctx->state_feature->cheat_words[1] = 0U;
    ctx->state_feature->cheat_words[2] = 0U;
    ctx->state_feature->cheat_words[3] = 0U;
    ctx->state_feature->io_phase = (u8)PS_APP_STATE_IO_IDLE;
    ctx->state_feature->io_busy_seen = 0U;
    ctx->state_feature->io_last_result = 0U;
    ctx->state_feature->reserved0 = 0U;
    ctx->state_feature->io_wait_ticks = 0U;
    ctx->state_feature->io_timeout_ticks = 0U;
    ctx->state_feature->save_file_count = 0U;
    ctx->state_feature->load_file_count = 0U;
    ctx->state_feature->io_error_count = 0U;
    ctx->state_feature->last_state_path[0] = '\0';

    ctx->rtc->loaded = 0U;
    ctx->rtc->in_use = 0U;
    ctx->rtc->dirty = 0U;
    ctx->rtc->reserved0 = 0U;
    ctx->rtc->save_interval_ticks = PS_APP_RTC_FLUSH_TICKS;
    if (ctx->rtc->save_interval_ticks == 0U) {
        ctx->rtc->save_interval_ticks = 1U;
    }
    ctx->rtc->save_countdown = ctx->rtc->save_interval_ticks;
    ctx->rtc->timestamp_saved = 0U;
    ctx->rtc->saved_time = 0ULL;
    ctx->rtc->last_timestamp_out = 0U;
    ctx->rtc->last_savedtime_out = 0ULL;
    ctx->rtc->path[0] = '\0';

    ctx->sensor->solar = 0U;
    ctx->sensor->tilt_x = 0;
    ctx->sensor->tilt_y = 0;
    ctx->sensor->rumble_enabled = 1U;
    ctx->sensor->rumble_active = 0U;
    ctx->sensor->rumble_strength = (u8)PS_APP_RUMBLE_STRENGTH_ON;
    ctx->sensor->rumble_dirty = 1U;
    ctx->sensor->reserved0 = 0U;
    ctx->sensor->rumble_change_count = 0U;
    ctx->sensor->sensor_update_count = 0U;

    ctx->rom->loaded = 0U;
    ctx->rom->is_loading = 0U;
    ctx->rom->sig_flash1m = 0U;
    ctx->rom->sig_flash = 0U;
    ctx->rom->sig_sram = 0U;
    ctx->rom->sig_eeprom = 0U;
    ctx->rom->quirk_remap = 0U;
    ctx->rom->quirk_sram_disable = 0U;
    ctx->rom->quirk_gpio = 0U;
    ctx->rom->quirk_tilt = 0U;
    ctx->rom->quirk_solar = 0U;
    ctx->rom->bios_mode = (u8)PS_APP_BIOS_MODE_INTERNAL;
    ctx->rom->bios_load_ok = 0U;
    ctx->rom->bios_external_present = 0U;
    ctx->rom->bios_bytes_loaded = 0U;
    ctx->rom->size_bytes = 0U;
    ctx->rom->size_aligned = 0U;
    ctx->rom->flash1m_offset = 0xFFFFFFFFU;
    ctx->rom->flash_offset = 0xFFFFFFFFU;
    ctx->rom->sram_offset = 0xFFFFFFFFU;
    ctx->rom->eeprom_offset = 0xFFFFFFFFU;
    ctx->rom->game_code[0] = '\0';
    ctx->rom->maker_code[0] = '\0';
    ctx->rom->path[0] = '\0';

    ctx->save->event_counters = 0U;
    ctx->save->flush_count = 0U;
    ctx->save->quiet_ticks = 0U;
    ctx->save->last_bytes = 0U;
    ctx->save->last_checksum = 0U;
    ctx->save->active_kind = PS_APP_SAVE_KIND_NONE;
    ctx->save->dirty = 0U;
    ctx->save->loaded_from_sd = 0U;
    ctx->save->path[0] = '\0';

    ctx->diag->last_runtime_irq_sts = 0U;
    ctx->diag->last_physical_keys = 0U;
    ctx->diag->warn_print_count = 0U;
    ctx->diag->warn_suppress_ticks = 0U;
    ctx->diag->warn_last_err_latch = 0U;
    ctx->diag->warn_last_vdma_errs = 0U;
    ctx->diag->stall_last_pc = 0U;
    ctx->diag->stall_last_mem = 0U;
    ctx->diag->stall_last_dma = 0U;
    ctx->diag->stall_last_frame = 0U;
    ctx->diag->stall_same_sample_count = 0U;
    ctx->diag->auto_boot_audit_printed = 0U;
    ctx->diag->auto_stall_audit_printed = 0U;
    ctx->diag->log_input_delta_enable = 0U;
    ctx->diag->log_fbscan_auto_enable = 0U;
    ctx->diag->delayed_chain_tick = 0U;
    ctx->diag->delayed_chain_printed = 0U;
    ctx->diag->fbscan_tick = 0U;
    ctx->diag->fbscan_dump_count = 0U;
    ctx->diag->fbscan_last_anomaly_tick = 0U;

    ctx->usb_host->enabled = (u8)((PS_APP_USBHOST_ENABLE_DEFAULT != 0U) ? 1U : 0U);
    ctx->usb_host->initialized = 0U;
    ctx->usb_host->irq_connected = 0U;
    ctx->usb_host->device_present = 0U;
    ctx->usb_host->xbox_interface_active = 0U;
    ctx->usb_host->bus_id = 0U;
    ctx->usb_host->interface_number = 0U;
    ctx->usb_host->port_speed = 0U;
    ctx->usb_host->vendor_id = 0U;
    ctx->usb_host->product_id = 0U;
    ctx->usb_host->interface_class = 0U;
    ctx->usb_host->interface_subclass = 0U;
    ctx->usb_host->interface_protocol = 0U;
    ctx->usb_host->ep_in_addr = 0U;
    ctx->usb_host->ep_out_addr = 0U;
    ctx->usb_host->irq_count = 0U;
    ctx->usb_host->event_count = 0U;
    ctx->usb_host->attach_count = 0U;
    ctx->usb_host->detach_count = 0U;
    ctx->usb_host->in_report_count = 0U;
    ctx->usb_host->in_report_error_count = 0U;
    ctx->usb_host->out_report_count = 0U;
    ctx->usb_host->out_report_error_count = 0U;

    ctx->input->enabled = (u8)((PS_APP_INPUT_ENABLE_DEFAULT != 0U) ? 1U : 0U);
    ctx->input->active = 0U;
    ctx->input->release_pending = 0U;
    ctx->input->report_valid = 0U;
    ctx->input->protocol_is_xinput = 0U;
    ctx->input->output_capable = 0U;
    ctx->input->vendor_id = 0U;
    ctx->input->product_id = 0U;
    ctx->input->interface_number = 0U;
    ctx->input->interface_class = 0U;
    ctx->input->interface_subclass = 0U;
    ctx->input->interface_protocol = 0U;
    ctx->input->ep_in_addr = 0U;
    ctx->input->ep_out_addr = 0U;
    ctx->input->ep_in_interval_ms = 0U;
    ctx->input->ep_out_interval_ms = 0U;
    ctx->input->buttons = 0U;
    ctx->input->lt = 0U;
    ctx->input->rt = 0U;
    ctx->input->lx = 0;
    ctx->input->ly = 0;
    ctx->input->rx = 0;
    ctx->input->ry = 0;
    ctx->input->mapped_keys = 0U;
    ctx->input->last_committed_keys = 0U;
    ctx->input->report_count = 0U;
    ctx->input->parse_error_count = 0U;
    ctx->input->unsupported_report_count = 0U;
    ctx->input->last_report_len = 0U;

    ctx->hdmi->initialized = 0U;
    ctx->hdmi->irq_connected = 0U;
    ctx->hdmi->hpd_level = 0U;
    ctx->hdmi->present_enable = 0U;
    ctx->hdmi->hpd_pending = 0U;
    ctx->hdmi->edid_valid = 0U;
    ctx->hdmi->edid_refresh_pending = 0U;
    ctx->hdmi->blank_frame_pending = 0U;
    ctx->hdmi->preferred_is_640x480p60 = 0U;
    ctx->hdmi->preferred_timing_valid = 0U;
    ctx->hdmi->preferred_vic = 0U;
    ctx->hdmi->vendor_id = 0U;
    ctx->hdmi->product_code = 0U;
    ctx->hdmi->hpd_rise_count = 0U;
    ctx->hdmi->hpd_fall_count = 0U;
    ctx->hdmi->edid_read_ok_count = 0U;
    ctx->hdmi->edid_read_fail_count = 0U;
    ctx->hdmi->last_error = 0U;
    ctx->hdmi->last_service_tick = 0U;
    ctx->hdmi->last_hpd_change_tick = 0U;
    memset(ctx->hdmi->edid_block0, 0, sizeof(ctx->hdmi->edid_block0));

    ctx->ps_gpio_ready = 0U;
    ctx->ps_btn_last_mask = 0U;
}

static XStatus PsAppRuntime_AutoloadDefaultRom(PsAppRuntimeContext *ctx) {
    XStatus status;

    xil_printf("[INIT] 9: default rom path=%s\r\n", PS_APP_DEFAULT_ROM_SD_PATH);
    status = PsAppRuntime_LoadRomFromSd(ctx, PS_APP_DEFAULT_ROM_SD_PATH);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 9 info: place a known-good ROM at %s\r\n",
                   PS_APP_DEFAULT_ROM_SD_PATH);
        xil_printf("[INIT] 9 info: or use 'rom load <path>' from the UART console\r\n");
        xil_printf("[INIT] 9 info: avoid relying on the previous hardcoded homebrew ROM for bring-up\r\n");
    }

    return status;
}

void PsAppRuntime_ApplyShadowConfig(PsAppRuntimeContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    (void)PsAppRuntime_SanitizeCtrlShadow(ctx, "apply-shadow", 0U);

    PsGbaRegs_CommitConfig(ctx->regs,
                           ctx->config->ctrl,
                           ctx->config->keys,
                           ctx->config->max_pak_addr,
                           ctx->config->cycle_precalc,
                           ctx->config->rtc_timestamp);
}

XStatus PsAppRuntime_InitUart(PsAppRuntimeContext *ctx) {
    XUartPs_Config *cfg;
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    cfg = XUartPs_LookupConfig(XPAR_XUARTPS_0_BASEADDR);
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XUartPs_CfgInitialize(ctx->uart, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XUartPs_SetBaudRate(ctx->uart, PS_APP_UART_BAUDRATE);
}

XStatus PsAppRuntime_ProgramAudio(PsAppRuntimeContext *ctx) {
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    status = PsAudioCodec_ProgramPlayback(ctx->codec,
                                          ctx->audio->sample_rate_hz,
                                          ctx->audio->bits_per_sample);
    if (status != XST_SUCCESS) {
        return status;
    }

    return PsAppRuntime_ApplyAudioOutputState(ctx);
}

XStatus PsAppRuntime_LoadRomFromSd(PsAppRuntimeContext *ctx, const char *requested_path) {
    PsRomLoadResult load_result;
    char resolved_path[PS_APP_ROM_PATH_MAX_CHARS];
    XStatus status;

    if ((ctx == NULL) ||
        (PsAppRuntime_ResolveRomPath(requested_path, resolved_path, sizeof(resolved_path)) != 0)) {
        xil_printf("[ROM] invalid path\r\n");
        return XST_INVALID_PARAM;
    }
    if ((ctx->rtc != NULL) && (ctx->rtc->dirty != 0U)) {
        (void)PsAppRuntime_FlushRtcToFile(ctx);
    }

    ctx->rom->is_loading = 1U;
    ctx->rom->loaded = 0U;
    ctx->rom->size_bytes = 0U;
    ctx->rom->size_aligned = 0U;
    ctx->rom->sig_flash1m = 0U;
    ctx->rom->sig_flash = 0U;
    ctx->rom->sig_sram = 0U;
    ctx->rom->sig_eeprom = 0U;
    ctx->rom->quirk_remap = 0U;
    ctx->rom->quirk_sram_disable = 0U;
    ctx->rom->quirk_gpio = 0U;
    ctx->rom->quirk_tilt = 0U;
    ctx->rom->quirk_solar = 0U;
    ctx->rom->bios_mode = (u8)PS_APP_BIOS_MODE_INTERNAL;
    ctx->rom->bios_load_ok = 0U;
    ctx->rom->bios_external_present = 0U;
    ctx->rom->bios_bytes_loaded = 0U;
    ctx->rom->flash1m_offset = 0xFFFFFFFFU;
    ctx->rom->flash_offset = 0xFFFFFFFFU;
    ctx->rom->sram_offset = 0xFFFFFFFFU;
    ctx->rom->eeprom_offset = 0xFFFFFFFFU;
    ctx->rom->game_code[0] = '\0';
    ctx->rom->maker_code[0] = '\0';
    ctx->video->fbcap_last_buf_idx = 0xFFU;
    ctx->video->fbcap_last_frame_idx = 0xFFU;
    ctx->video->blit_count = 0U;
    ctx->video->blit_last_us = 0U;
    ctx->video->blit_max_us = 0U;
    ctx->video->blit_total_us = 0U;
    ctx->video->blit_seq_gap_max = 0U;
    ctx->video->blit_seq_glitch_drop = 0U;
    ctx->diag->stall_last_pc = 0U;
    ctx->diag->stall_last_mem = 0U;
    ctx->diag->stall_last_dma = 0U;
    ctx->diag->stall_last_frame = 0U;
    ctx->diag->stall_same_sample_count = 0U;
    ctx->diag->auto_stall_audit_printed = 0U;
    ctx->state_feature->save_pending = 0U;
    ctx->state_feature->load_pending = 0U;
    ctx->state_feature->cheat_push_pending = 0U;
    ctx->state_feature->cheat_clear_pending = 0U;
    ctx->state_feature->feature_status = 0U;
    PsAppRuntime_CopyText(ctx->rom->path, sizeof(ctx->rom->path), resolved_path);

    ctx->config->ctrl |= PS_APP_GBA_CTRL_BOOT_REQUIRED;
    ctx->config->ctrl &= ~GBA_CTRL_CORE_ON;
    ctx->config->ctrl |= GBA_CTRL_ROM_LOADING;
    ctx->config->max_pak_addr = 0U;
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "rom-preload");

    PsGbaRegs_SetSwReset(ctx->regs, 1U);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "reset-assert");

    xil_printf("[ROM] load %s\r\n", ctx->rom->path);
    xil_printf("[ROM] target addr=0x%08x limit=%u\r\n",
               (unsigned int)PS_APP_GBA_ROM_REGION_BASE_ADDR,
               (unsigned int)PS_APP_GBA_ROM_REGION_MAX_BYTES);
    status = PsRomLoader_LoadSdFile(ctx->rom->path,
                                    (UINTPTR)PS_APP_GBA_ROM_REGION_BASE_ADDR,
                                    PS_APP_GBA_ROM_REGION_MAX_BYTES,
                                    &load_result);
    if (status != XST_SUCCESS) {
        ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
        ctx->config->max_pak_addr = 0U;
        PsAppRuntime_ApplyShadowConfig(ctx);
        PsGbaRegs_SetSwReset(ctx->regs, 1U);
        ctx->rom->is_loading = 0U;
        xil_printf("[ROM] load failed: %s\r\n", PsRomLoader_StrError(load_result.fs_result));
        return status;
    }

    status = PsAppRuntime_PrepareBiosForRom(ctx);
    if (status != XST_SUCCESS) {
        ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
        ctx->config->max_pak_addr = 0U;
        PsAppRuntime_ApplyShadowConfig(ctx);
        PsGbaRegs_SetSwReset(ctx->regs, 1U);
        ctx->rom->is_loading = 0U;
        xil_printf("[ROM] aborted: BIOS prepare failed\r\n");
        return status;
    }

    if (PsAppSave_PrepareForRom(ctx->save_ctx, ctx->rom->path) != XST_SUCCESS) {
        xil_printf("[SAVE] preload skipped\r\n");
    }
    if (strcmp(ctx->rom->path, resolved_path) != 0) {
        xil_printf("[ROM] warning: path clobbered after save preload old=%s new=%s\r\n",
                   ctx->rom->path,
                   resolved_path);
        PsAppRuntime_CopyText(ctx->rom->path, sizeof(ctx->rom->path), resolved_path);
    }
    /*
     * RTC 预加载前后各做一次稳定性校验：
     * - pre: 确保进入 RTC 路径前上下文已干净；
     * - post: 一旦 RTC/存储链路触发异常，立即就地自愈，避免带毒进入后续 VDMA 请求。
     */
    PsAppRuntime_ValidateContextStability(ctx, "rom-pre-rtc");
    (void)PsAppRuntime_PrepareRtcForRom(ctx, ctx->rom->path);
    PsAppRuntime_ValidateContextStability(ctx, "rom-post-rtc");
    PsAppRuntime_ApplyFeatureLevels(ctx);
    PsAppRuntime_ApplySensorState(ctx, 1U);

    ctx->config->max_pak_addr = load_result.max_pak_addr & 0x1FFFFFFU;
    ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
    ctx->config->ctrl |= (GBA_CTRL_CORE_ON | PS_APP_GBA_CTRL_BOOT_REQUIRED);
    PsAppRuntime_ApplyRomDetection(ctx, &load_result);
    (void)PsAppRuntime_SanitizeCtrlShadow(ctx, "rom-detect", 0U);
    xil_printf("[ROMSTEP] apply-shadow begin ctrl=0x%08x\r\n",
               (unsigned int)ctx->config->ctrl);
    PsAppRuntime_ApplyShadowConfig(ctx);
    xil_printf("[ROMSTEP] apply-shadow done\r\n");
    xil_printf("[ROMSTEP] vdma fill begin w=%u h=%u stride=%u count=%u\r\n",
               (unsigned int)ctx->vdma->width,
               (unsigned int)ctx->vdma->height,
               (unsigned int)ctx->vdma->line_stride_bytes,
               (unsigned int)ctx->vdma->frame_count);
    if (PsHdmiVdma_FillAllFrames(ctx->vdma, 0x00000000U) != XST_SUCCESS) {
        xil_printf("[ROMSTEP] warning: vdma fill failed\r\n");
    } else {
        xil_printf("[ROMSTEP] vdma fill done\r\n");
    }
    xil_printf("[ROMSTEP] request frame begin\r\n");
    if (PsAppVideo_RequestFrame(ctx->video_ctx, 0U) != XST_SUCCESS) {
        xil_printf("[ROMSTEP] warning: request frame failed\r\n");
    } else {
        xil_printf("[ROMSTEP] request frame done\r\n");
    }
    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
    xil_printf("[ROMSTEP] sync frame done\r\n");
    ctx->video->fbcap_last_frame_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "rom-postload");

    usleep(2000U);
    PsGbaRegs_SetSwReset(ctx->regs, 0U);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "reset-release");
    PsAppRuntime_ApplyShadowConfig(ctx);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "post-release-commit");

    ctx->rom->loaded = 1U;
    ctx->rom->is_loading = 0U;
    ctx->rom->size_bytes = load_result.bytes_loaded;
    ctx->rom->size_aligned = load_result.bytes_aligned;

    xil_printf("[ROM] ready bytes=%u aligned=%u maxpak=0x%08x addr=0x%08x\r\n",
               (unsigned int)ctx->rom->size_bytes,
               (unsigned int)ctx->rom->size_aligned,
               (unsigned int)ctx->config->max_pak_addr,
               (unsigned int)PS_APP_GBA_ROM_REGION_BASE_ADDR);
    xil_printf("[ROM] bios mode=%s ext=%u load_ok=%u bytes=%u\r\n",
               PsAppRuntime_BiosModeName(ctx->rom->bios_mode),
               (unsigned int)ctx->rom->bios_external_present,
               (unsigned int)ctx->rom->bios_load_ok,
               (unsigned int)ctx->rom->bios_bytes_loaded);
    PsAppDiag_PrintRomProbe(ctx->diag_ctx);

    return XST_SUCCESS;
}

XStatus PsAppRuntime_InitSystem(PsAppRuntimeContext *ctx) {
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    xil_printf("[INIT] 0: context cleared\r\n");
    xil_printf("[INIT] 1: gba regs bootstrap\r\n");
    PsGbaRegs_Init(ctx->regs, XPAR_ZYNQ_GBA_TOP_0_BASEADDR);
    PsAppRuntime_InitDefaults(ctx);
    PsAppSave_Reset(ctx->save_ctx);
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsGbaRegs_SetIrqEnable(ctx->regs, ctx->config->irq_enable);
    PsGbaRegs_ClearIrqStatus(ctx->regs, PS_APP_IRQ_MASK_VSYNC | PS_APP_IRQ_MASK_ERROR);
    PsAppRuntime_ApplyFeatureLevels(ctx);
    PsAppRuntime_ApplyRtcSavedState(ctx);
    PsAppRuntime_ApplySensorState(ctx, 1U);
    xil_printf("[INIT] 1: gba regs done\r\n");

    xil_printf("[INIT] 2: vdma init\r\n");
    status = PsHdmiVdma_Init(ctx->vdma,
                             XPAR_XAXIVDMA_0_BASEADDR,
                             (UINTPTR)PS_APP_FB_REGION_BASE_ADDR,
                             PS_APP_FB_FRAME_STORE_BYTES,
                             PS_APP_HDMI_WIDTH,
                             PS_APP_HDMI_HEIGHT,
                             PS_APP_HDMI_BPP);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 2 failed: %d\r\n", status);
        return status;
    }

    status = PsAppVideo_InitInterrupts(ctx->video_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 2.1 failed: %d\r\n", status);
        return status;
    }

    PsAppDiag_PrintVdmaSnapshot(ctx->diag_ctx, "post-init");
    xil_printf("[INIT] 2: vdma done\r\n");

    xil_printf("[INIT] 3: audio codec i2c init\r\n");
    status = PsAudioCodec_Init(ctx->codec,
                               XPAR_XIICPS_0_BASEADDR,
                               SSM2603_I2C_ADDR,
                               100000U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 3 warning: %d (audio disabled)\r\n", status);
        ctx->codec->is_ready = 0U;
    } else {
        xil_printf("[INIT] 3: audio i2c ready (defer program)\r\n");
    }

    xil_printf("[INIT] 5: clear boot framebuffers\r\n");
    status = PsAppVideo_RenderBootFrames(ctx->video_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 failed: %d\r\n", status);
        return status;
    }
    PsAppVideo_SetInterframeMode(ctx->video_ctx, ctx->video->fx.interframe_mode);
    PsAppVideo_SetShadeMode(ctx->video_ctx, ctx->video->fx.shade_mode);
    xil_printf("[INIT] 5: framebuffers ready\r\n");
    xil_printf("[INIT] 5: trying park frame 0\r\n");
    status = PsAppVideo_RequestFrame(ctx->video_ctx, 0U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 park failed: %d\r\n", status);
    } else {
        xil_printf("[INIT] 5 park ok\r\n");
    }
    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);

    if (ctx->codec->is_ready != 0U) {
        xil_printf("[INIT] 7: audio codec program\r\n");
        status = PsAppRuntime_ProgramAudio(ctx);
        if (status != XST_SUCCESS) {
            xil_printf("[INIT] 7 warning: %d (audio disabled)\r\n", status);
            ctx->codec->is_ready = 0U;
        } else {
            xil_printf("[INIT] 7: audio done\r\n");
        }
    }

    xil_printf("[INIT] 8: ps gpio BTN4/BTN5 init\r\n");
    status = PsAppRuntime_InitPsGpio(ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 8 failed: %d\r\n", status);
        return status;
    }
    if (ctx->ps_gpio_ready != 0U) {
        PsAppRuntime_PrintPsButtons(ctx->ps_btn_last_mask);
    }

    xil_printf("[INIT] 8.1: hdmi hpd/ddc init (HPD=GPIO54, DDC=IIC1)\r\n");
    status = PsAppHdmiLink_Init(ctx->hdmi_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 8.1 warning: hdmi link init failed: %d (keep fixed 640x480@60)\r\n",
                   status);
    }

    xil_printf("[INIT] 8.2: input map BTN3/2/1/0=left/up/down/right SW3/2/1/0=select(start-mod)/start/b/a SW3+BTN3=L SW3+BTN0=R\r\n");
    xil_printf("[INIT] 8.3: xinput parser init\r\n");
    PsAppInput_Init(ctx->input_ctx);
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    xil_printf("[INIT] 8.3: gba map A<=B(right) B<=A(bottom) deadzone=%d trig=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#else
    xil_printf("[INIT] 8.3: gba map A<=A(bottom) B<=B(right) deadzone=%d trig=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#endif

    xil_printf("[INIT] 9: rom autoload\r\n");
    if (PsAppRuntime_AutoloadDefaultRom(ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 9 warning: ROM autoload skipped\r\n");
    }

    xil_printf("[INIT] 9.1: usb host init (post-rom)\r\n");
    if (PsAppUsbHost_Init(ctx->usb_host_ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 9.1 warning: usb host unavailable\r\n");
    } else {
        PsAppUsbHost_PrintStatus(ctx->usb_host_ctx);
    }

    return XST_SUCCESS;
}

static void PsAppRuntime_ServiceInputFast(PsAppRuntimeContext *ctx) {
    u32 prev_gba_keys;
    u32 mapped_keys;
    u32 prev_pad_source_mask;
    u32 curr_pad_source_mask;
    u32 frame_token;
    u8 log_needed;
    u8 input_override;

    if (ctx == NULL) {
        return;
    }

    PsAppUsbHost_Service(ctx->usb_host_ctx);
    PsAppInput_Service(ctx->input_ctx);
    frame_token = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    PsAppInput_PublishFrame(ctx->input_ctx, frame_token);
    PsAppRuntime_ServiceSensorFast(ctx);
    input_override = PsAppInput_ShouldOverrideKeys(ctx->input_ctx);
    if (input_override != 0U) {
        prev_gba_keys = ctx->input->last_committed_keys & 0x3FFU;
        mapped_keys = PsAppInput_GetMappedKeys(ctx->input_ctx) & 0x3FFU;
        prev_pad_source_mask = ctx->input->last_logged_source_mask;
        curr_pad_source_mask = ctx->input->source_snapshot_mask;
        log_needed = (u8)(((prev_gba_keys != mapped_keys) ||
                           (prev_pad_source_mask != curr_pad_source_mask)) ? 1U : 0U);
        if ((ctx->config->keys != mapped_keys) ||
            (PsAppInput_IsReleasePending(ctx->input_ctx) != 0U)) {
            ctx->config->keys = mapped_keys;
            PsAppRuntime_ApplyShadowConfig(ctx);
        }
        if ((log_needed != 0U) && (ctx->diag->log_input_delta_enable != 0U)) {
            PsAppRuntime_PrintInputDelta(ctx,
                                         prev_pad_source_mask,
                                         curr_pad_source_mask,
                                         prev_gba_keys,
                                         mapped_keys);
        }
        ctx->input->last_committed_keys = mapped_keys;
        ctx->input->last_logged_source_mask = curr_pad_source_mask;
        PsAppInput_ClearReleasePending(ctx->input_ctx);
    }
}

static void PsAppRuntime_ServiceSlow(PsAppRuntimeContext *ctx) {
    u32 status0;
    u32 status1;
    u32 err_latch;
    u32 irq_sts;
    u32 cycles_missing;
    u32 dbg_pc;
    u32 dbg_dma;
    u32 dbg_mem;
    u32 vdma_status;
    u32 vdma_errs;
    u32 physical_keys;
    u32 ps_btn_mask;
    u32 pressed_ps_btns;

    if (ctx == NULL) {
        return;
    }

    status0 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0);
    status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS) & 0x3U;
    dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);
    vdma_errs = vdma_status & XAXIVDMA_SR_ERR_ALL_MASK;
    cycles_missing = (status1 >> 2) & 0x3FFFU;
    physical_keys = status0 & 0x3FFU;

    if (ctx->diag->last_physical_keys != physical_keys) {
        PsAppRuntime_PrintPhysicalKeys(physical_keys);
        ctx->diag->last_physical_keys = physical_keys;
    }
    ps_btn_mask = PsAppRuntime_ReadPsButtonMask(ctx);
    if ((ctx->ps_gpio_ready != 0U) && (ctx->ps_btn_last_mask != ps_btn_mask)) {
        pressed_ps_btns = ps_btn_mask & ~ctx->ps_btn_last_mask;
        if ((pressed_ps_btns & PS_APP_BTN4_MASK) != 0U) {
            u32 next_volume = (u32)ctx->audio->volume + PS_APP_AUDIO_VOLUME_STEP;
            if (PsAppRuntime_SetAudioVolume(ctx, next_volume) == XST_SUCCESS) {
                xil_printf("[AUDIO] volume=%u mute=%u\r\n",
                           (unsigned int)ctx->audio->volume,
                           (unsigned int)((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)));
            }
        }
        if ((pressed_ps_btns & PS_APP_BTN5_MASK) != 0U) {
            u32 next_volume = (ctx->audio->volume > PS_APP_AUDIO_VOLUME_STEP) ?
                              ((u32)ctx->audio->volume - PS_APP_AUDIO_VOLUME_STEP) : 0U;
            if (PsAppRuntime_SetAudioVolume(ctx, next_volume) == XST_SUCCESS) {
                xil_printf("[AUDIO] volume=%u mute=%u\r\n",
                           (unsigned int)ctx->audio->volume,
                           (unsigned int)((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)));
            }
        }
        PsAppRuntime_PrintPsButtons(ps_btn_mask);
        ctx->ps_btn_last_mask = ps_btn_mask;
    }

    PsAppHdmiLink_Service(ctx->hdmi_ctx);
    if ((ctx->hdmi != NULL) && (ctx->hdmi->blank_frame_pending != 0U)) {
        (void)PsHdmiVdma_FillAllFrames(ctx->vdma, 0x00000000U);
        (void)PsAppVideo_RequestFrame(ctx->video_ctx, 0U);
        PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
        ctx->hdmi->blank_frame_pending = 0U;
    }
    PsAppRuntime_ServiceStateFeature(ctx);
    PsAppRuntime_ServiceRtcPersist(ctx);
    PsAppRuntime_ServiceRumble(ctx);

    if (ctx->rom->is_loading != 0U) {
        PsAppVideo_AttemptRecover(ctx->video_ctx, vdma_status, vdma_errs);
        return;
    }

    if ((cycles_missing != 0U) || (err_latch != 0U) || (vdma_errs != 0U)) {
        u32 new_error = 0U;

        if ((err_latch != ctx->diag->warn_last_err_latch) ||
            (vdma_errs != ctx->diag->warn_last_vdma_errs)) {
            new_error = 1U;
        }
        if ((ctx->diag->warn_print_count == 0U) && (cycles_missing != 0U)) {
            new_error = 1U;
        }

        if (new_error != 0U) {
            xil_printf("[WARN] miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x\r\n",
                       (unsigned int)cycles_missing,
                       (unsigned int)err_latch,
                       (unsigned int)vdma_status,
                       (unsigned int)vdma_errs);
            ctx->diag->warn_print_count++;
            ctx->diag->warn_suppress_ticks = 0U;
        } else {
            ctx->diag->warn_suppress_ticks++;
            {
                u32 threshold = 300U;
                u32 i;
                for (i = 1U; i < ctx->diag->warn_print_count && i < 4U; i++) {
                    threshold *= 10U;
                }
                if (ctx->diag->warn_suppress_ticks >= threshold) {
                    xil_printf("[WARN] summary miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x (suppressed %u ticks)\r\n",
                               (unsigned int)cycles_missing,
                               (unsigned int)err_latch,
                               (unsigned int)vdma_status,
                               (unsigned int)vdma_errs,
                               (unsigned int)ctx->diag->warn_suppress_ticks);
                    ctx->diag->warn_print_count++;
                    ctx->diag->warn_suppress_ticks = 0U;
                }
            }
        }

        ctx->diag->warn_last_err_latch = err_latch;
        ctx->diag->warn_last_vdma_errs = vdma_errs;
    } else {
        if (ctx->diag->warn_print_count != 0U) {
            xil_printf("[WARN] cleared (was miss=%u)\r\n",
                       (unsigned int)ctx->diag->warn_print_count);
        }
        ctx->diag->warn_print_count = 0U;
        ctx->diag->warn_suppress_ticks = 0U;
        ctx->diag->warn_last_err_latch = 0U;
        ctx->diag->warn_last_vdma_errs = 0U;
    }

    if (irq_sts != 0U) {
        if ((irq_sts & ~PS_APP_IRQ_MASK_VSYNC) != 0U) {
            if (ctx->diag->last_runtime_irq_sts != irq_sts) {
                xil_printf("[IRQ] sts=0x%x\r\n", (unsigned int)irq_sts);
                ctx->diag->last_runtime_irq_sts = irq_sts;
            }
        } else {
            ctx->diag->last_runtime_irq_sts = 0U;
        }

        PsGbaRegs_ClearIrqStatus(ctx->regs, irq_sts);
    } else {
        ctx->diag->last_runtime_irq_sts = 0U;
    }

    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
    PsAppDiag_MaybePrintStallAudit(ctx->diag_ctx, status1, dbg_pc, dbg_mem, dbg_dma);
    PsAppVideo_AttemptRecover(ctx->video_ctx, vdma_status, vdma_errs);

    /* 早期 BRAM 捕获诊断：ROM 加载后约 1 秒触发一次 */
    if (ctx->rom->loaded && ctx->diag->delayed_chain_printed == 0U &&
        ctx->diag->delayed_chain_tick >= 80U && ctx->diag->delayed_chain_tick <= 82U) {
        u32 cap_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
        u32 cap_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
        u32 bram_w0 = PsAppVideo_ReadCaptureWord(cap_sts & 0x1U, 0U);
        u32 bram_w1 = PsAppVideo_ReadCaptureWord(cap_sts & 0x1U, 1U);
        u32 ctrl_rb = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
        xil_printf("[CAPDIAG] seq=%u buf=%u w0=0x%08x w1=0x%08x ctrl=0x%08x last_seq=%u vdma_sr=0x%08x\r\n",
                   (unsigned int)cap_seq,
                   (unsigned int)(cap_sts & 0x1U),
                   (unsigned int)bram_w0,
                   (unsigned int)bram_w1,
                   (unsigned int)ctrl_rb,
                   (unsigned int)ctx->video->fbcap_last_frame_seq,
                   (unsigned int)vdma_status);
    }

    if (ctx->rom->loaded && ctx->diag->delayed_chain_printed < 2U) {
        ctx->diag->delayed_chain_tick++;
        if ((ctx->diag->delayed_chain_printed == 0U && ctx->diag->delayed_chain_tick >= 500U) ||
            (ctx->diag->delayed_chain_printed == 1U && ctx->diag->delayed_chain_tick >= 1500U)) {
            xil_printf("[AUTO] delayed chain snapshot @tick=%u\r\n",
                       (unsigned int)ctx->diag->delayed_chain_tick);
            PsAppDiag_PrintChainSnapshot(ctx->diag_ctx, "delayed");
            PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "delayed");
            ctx->diag->delayed_chain_printed++;
        }
    }

    if (ctx->rom->loaded &&
        (ctx->diag->log_fbscan_auto_enable != 0U) &&
        (ctx->diag->delayed_chain_tick > 200U)) {
        ctx->diag->fbscan_tick++;
        if ((ctx->diag->fbscan_tick % PS_APP_FBSCAN_INTERVAL_TICKS) == 0U) {
            if (ctx->diag->fbscan_dump_count < PS_APP_FBSCAN_MAX_DUMPS) {
                PsAppDiag_ScanFramebufferForAnomaly(ctx->diag_ctx, "auto", 0U);
            }
        }
    }
}

void PsAppRuntime_Service(PsAppRuntimeContext *ctx) {
    PsAppRuntime_ServiceInputFast(ctx);
    PsAppRuntime_ServiceSlow(ctx);
}

void PsAppMonitorTask(void *arg) {
    PsAppRuntimeContext *ctx = (PsAppRuntimeContext *)arg;
    TickType_t fast_delay_ticks;
    u32 slow_divider;
    u32 slow_tick_accum;

    fast_delay_ticks = pdMS_TO_TICKS(PS_APP_INPUT_SERVICE_INTERVAL_MS);
    if (fast_delay_ticks == 0U) {
        fast_delay_ticks = 1U;
    }
    slow_divider = PS_APP_MONITOR_INTERVAL_MS / PS_APP_INPUT_SERVICE_INTERVAL_MS;
    if (slow_divider == 0U) {
        slow_divider = 1U;
    }
    slow_tick_accum = slow_divider;

    for (;;) {
        /* 输入快路径每 1ms 跑一次，尽量贴近 USB IN 报告节拍；
         * 其余监控/诊断仍按原来的 10ms 慢节拍执行，避免打乱既有超时基准。 */
        PsAppRuntime_ServiceInputFast(ctx);
        if (slow_tick_accum >= slow_divider) {
            PsAppRuntime_ServiceSlow(ctx);
            slow_tick_accum = 0U;
        }
        slow_tick_accum++;
        vTaskDelay(fast_delay_ticks);
    }
}

void PsAppSaveTask(void *arg) {
    /* 关键防坑：
     * 这里使用 static 保存任务上下文，避免在异常栈压力下局部变量被破坏，
     * 进而把无效指针传入保存流程导致“写盘完成后整机卡死”。 */
    static PsAppSaveContext *s_save_task_ctx;
    TickType_t save_delay_ticks;

    s_save_task_ctx = (PsAppSaveContext *)arg;
    save_delay_ticks = pdMS_TO_TICKS(PS_APP_MONITOR_INTERVAL_MS);
    if (save_delay_ticks == 0U) {
        save_delay_ticks = 1U;
    }

    for (;;) {
        PsAppSave_Service(s_save_task_ctx);
        vTaskDelay(save_delay_ticks);
    }
}

void PsAppVideoPresentTask(void *arg) {
    PsAppRuntimeContext *ctx = (PsAppRuntimeContext *)arg;
    TickType_t present_delay_ticks;

    present_delay_ticks = pdMS_TO_TICKS(PS_APP_VIDEO_PRESENT_INTERVAL_MS);
    if (present_delay_ticks == 0U) {
        present_delay_ticks = 1U;
    }

    for (;;) {
        if ((ctx != NULL) &&
            (ctx->rom != NULL) &&
            (ctx->rom->loaded != 0U) &&
            ((ctx->hdmi == NULL) || (ctx->hdmi->present_enable != 0U))) {
            PsAppVideo_PresentCapturedFrameIfReady(ctx->video_ctx);
        }
        vTaskDelay(present_delay_ticks);
    }
}
