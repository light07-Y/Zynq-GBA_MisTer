#include "Save/Inc/ps_app_save.h"

#include <string.h>

#include "Gba/Inc/ps_gba_regs.h"
#include "Storage/Inc/ps_fatfs_storage.h"
#include "xil_cache.h"
#include "xil_printf.h"

#define PS_APP_SAVE_ENABLE_POSTWRITE_CHECKSUM 0U

/* 关键防坑：
 * 保存数据先快照到 PS DDR 常驻缓冲，再交给 FatFs 写盘，避免写盘过程中直接反复访问
 * 0x10000000 保存窗口带来的时序/总线耦合风险。
 * 64B 对齐用于配合 DCache 维护，降低缓存行边界副作用。 */
static u8 s_ps_app_save_snapshot[PS_APP_GBA_SAVE_REGION_MAX_BYTES] __attribute__((aligned(64)));

static int PsAppSave_IsPtrInDdr(const void *ptr) {
    UINTPTR addr;
    addr = (UINTPTR)ptr;
    return (addr >= 0x00100000U) && (addr < 0x40000000U);
}

static XStatus PsAppSave_SnapshotWindow(PsAppSaveContext *ctx,
                                        UINTPTR base_addr,
                                        u32 bytes,
                                        UINTPTR *snapshot_addr_out) {
    xil_printf("[SAVE] snapshot enter base=0x%08x bytes=%u\r\n",
               (unsigned int)base_addr,
               (unsigned int)bytes);
    xil_printf("[SAVE] snapshot ctx=0x%08x regs_ptr=0x%08x\r\n",
               (unsigned int)(UINTPTR)ctx,
               (unsigned int)(UINTPTR)((ctx != NULL) ? ctx->regs : NULL));
    if ((ctx == NULL) || (ctx->regs == NULL) || (snapshot_addr_out == NULL)) {
        xil_printf("[SAVE] snapshot invalid ctx\r\n");
        return XST_FAILURE;
    }
    if ((!PsAppSave_IsPtrInDdr(ctx)) ||
        (!PsAppSave_IsPtrInDdr(ctx->state)) ||
        (!PsAppSave_IsPtrInDdr(ctx->rom)) ||
        (!PsAppSave_IsPtrInDdr(ctx->regs))) {
        /* 关键防坑：
         * 一旦上下文指针跑飞，继续执行通常会直接硬挂。这里先 fail-fast 打印，便于现场定位。 */
        xil_printf("[SAVE] snapshot ptr invalid ctx=0x%08x state=0x%08x rom=0x%08x regs=0x%08x\r\n",
                   (unsigned int)(UINTPTR)ctx,
                   (unsigned int)(UINTPTR)ctx->state,
                   (unsigned int)(UINTPTR)ctx->rom,
                   (unsigned int)(UINTPTR)ctx->regs);
        return XST_FAILURE;
    }
    if ((base_addr == 0U) || (bytes == 0U) || (bytes > sizeof(s_ps_app_save_snapshot))) {
        xil_printf("[SAVE] snapshot invalid range\r\n");
        return XST_INVALID_PARAM;
    }

    /* 关键防坑：
     * 保存热路径不再读/改 SW_RESET。历史上该路径出现过读寄存器后系统停滞，
     * 因此保持“只做窗口快照，不碰控制寄存器”更稳妥。 */
    xil_printf("[SAVE] snapshot step skip reset toggle\r\n");

    xil_printf("[SAVE] snapshot step dcache inv\r\n");
    Xil_DCacheInvalidateRange((INTPTR)base_addr, bytes);
    xil_printf("[SAVE] snapshot step memcpy begin\r\n");
    memcpy(s_ps_app_save_snapshot, (const void *)base_addr, bytes);
    xil_printf("[SAVE] snapshot step memcpy done\r\n");
    Xil_DCacheFlushRange((INTPTR)s_ps_app_save_snapshot, bytes);
    xil_printf("[SAVE] snapshot step dcache flush done\r\n");

    *snapshot_addr_out = (UINTPTR)s_ps_app_save_snapshot;
    xil_printf("[SAVE] snapshot exit ok\r\n");
    return XST_SUCCESS;
}

/* 逻辑 save 类型 -> DDR 窗口基址：
 * SRAM 与 FLASH 在 core 侧复用同一段窗口（起点一致，按不同逻辑长度解释）。
 * EEPROM 则放在后续独立窗口，避免覆盖 SRAM/FLASH 区域。 */
static UINTPTR PsAppSave_BaseAddr(PsAppSaveKind kind) {
    switch (kind) {
        case PS_APP_SAVE_KIND_SRAM:
        case PS_APP_SAVE_KIND_FLASH:
            return (UINTPTR)PS_APP_GBA_SAVE_REGION_BASE_ADDR;
        case PS_APP_SAVE_KIND_EEPROM:
            /* 这里使用 FLASH 逻辑长度作为偏移锚点，是为了和 RTL softmap 地址保持一致。
             * 该偏移不是“展开字节”概念，窗口展开由 WindowByteCount 单独处理。 */
            return (UINTPTR)(PS_APP_GBA_SAVE_REGION_BASE_ADDR + PS_APP_GBA_SAVE_FLASH_BYTES);
        default:
            return 0U;
    }
}

static u32 PsAppSave_ByteCount(PsAppSaveKind kind) {
    switch (kind) {
        case PS_APP_SAVE_KIND_SRAM:
            return PS_APP_GBA_SAVE_SRAM_BYTES;
        case PS_APP_SAVE_KIND_FLASH:
            return PS_APP_GBA_SAVE_FLASH_BYTES;
        case PS_APP_SAVE_KIND_EEPROM:
            return PS_APP_GBA_SAVE_EEPROM_BYTES;
        default:
            return 0U;
    }
}

static u32 PsAppSave_WindowByteCount(PsAppSaveKind kind) {
    /* window 字节数 = 逻辑字节数 * stride(4)。
     * 任何对 DDR 保存窗口的 memcpy/memset 都必须用这个值，不能直接用逻辑大小。 */
    switch (kind) {
        case PS_APP_SAVE_KIND_SRAM:
            return PS_APP_GBA_SAVE_SRAM_WINDOW_BYTES;
        case PS_APP_SAVE_KIND_FLASH:
            return PS_APP_GBA_SAVE_FLASH_WINDOW_BYTES;
        case PS_APP_SAVE_KIND_EEPROM:
            return PS_APP_GBA_SAVE_EEPROM_WINDOW_BYTES;
        default:
            return 0U;
    }
}

static const char *PsAppSave_Extension(PsAppSaveKind kind) {
    switch (kind) {
        case PS_APP_SAVE_KIND_SRAM:
            return ".sav";
        case PS_APP_SAVE_KIND_FLASH:
            return ".fla";
        case PS_APP_SAVE_KIND_EEPROM:
            return ".eep";
        default:
            return ".bin";
    }
}

static u32 PsAppSave_Checksum(UINTPTR base_addr, u32 bytes) {
    const u8 *ptr;
    u32 idx;
    u32 hash;

    if ((base_addr == 0U) || (bytes == 0U)) {
        return 0U;
    }

    ptr = (const u8 *)base_addr;
    hash = 2166136261U;
    for (idx = 0U; idx < bytes; ++idx) {
        hash ^= ptr[idx];
        hash *= 16777619U;
    }
    return hash;
}

static void PsAppSave_CopyText(char *dst, size_t dst_size, const char *src) {
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

static void PsAppSave_CopyTextBounded(char *dst,
                                      size_t dst_size,
                                      const char *src,
                                      size_t src_max_len) {
    size_t src_len;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    src_len = 0U;
    while ((src_len < src_max_len) && (src[src_len] != '\0')) {
        src_len++;
    }
    if (src_len >= dst_size) {
        src_len = dst_size - 1U;
    }

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

static void PsAppSave_SanitizeAsciiInPlace(char *text) {
    size_t idx;

    if (text == NULL) {
        return;
    }

    for (idx = 0U; text[idx] != '\0'; ++idx) {
        unsigned char ch = (unsigned char)text[idx];
        if ((ch < 0x20U) || (ch > 0x7EU)) {
            text[idx] = '?';
        }
    }
}

static void PsAppSave_RomStem(const char *rom_path, char *stem_out, size_t stem_size) {
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

static void PsAppSave_BuildPath(const char *rom_path,
                                PsAppSaveKind kind,
                                char *path_out,
                                size_t path_size) {
    char stem[PS_APP_ROM_PATH_MAX_CHARS];
    const char *ext;
    size_t dir_len;
    size_t stem_len;
    size_t ext_len;

    if ((path_out == NULL) || (path_size == 0U)) {
        return;
    }

    path_out[0] = '\0';
    PsAppSave_RomStem(rom_path, stem, sizeof(stem));
    if (stem[0] == '\0') {
        return;
    }

    ext = PsAppSave_Extension(kind);
    dir_len = strlen(PS_APP_SAVE_SD_DIR);
    stem_len = strlen(stem);
    ext_len = strlen(ext);
    if ((dir_len + 1U + stem_len + ext_len + 1U) > path_size) {
        return;
    }

    memcpy(path_out, PS_APP_SAVE_SD_DIR, dir_len);
    path_out[dir_len] = '/';
    memcpy(&path_out[dir_len + 1U], stem, stem_len);
    memcpy(&path_out[dir_len + 1U + stem_len], ext, ext_len);
    path_out[dir_len + 1U + stem_len + ext_len] = '\0';
}

static void PsAppSave_ClearWindow(PsAppSaveKind kind) {
    UINTPTR base_addr;
    u32 bytes;

    base_addr = PsAppSave_BaseAddr(kind);
    bytes = PsAppSave_WindowByteCount(kind);
    if ((base_addr == 0U) || (bytes == 0U)) {
        return;
    }

    /* 用 0xFF 清窗口，匹配大多数卡带保存介质“擦除态”语义。 */
    memset((void *)base_addr, 0xFF, bytes);
    Xil_DCacheFlushRange((INTPTR)base_addr, bytes);
}

static XStatus PsAppSave_TryLoadKind(PsAppSaveContext *ctx,
                                     const char *rom_path,
                                     PsAppSaveKind kind,
                                     u8 *loaded_out) {
    PsFatFsStorageReadResult read_result;
    char path[PS_APP_ROM_PATH_MAX_CHARS];
    UINTPTR base_addr;
    u8 *window_ptr;
    u32 logical_bytes;
    u32 window_bytes;
    u32 idx;
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_FAILURE;
    }

    PsAppSave_BuildPath(rom_path, kind, path, sizeof(path));
    if (path[0] == '\0') {
        return XST_INVALID_PARAM;
    }

    logical_bytes = PsAppSave_ByteCount(kind);
    window_bytes = PsAppSave_WindowByteCount(kind);
    base_addr = PsAppSave_BaseAddr(kind);
    if ((logical_bytes == 0U) || (window_bytes == 0U) || (base_addr == 0U) ||
        (logical_bytes > sizeof(s_ps_app_save_snapshot))) {
        return XST_FAILURE;
    }

    memset(&read_result, 0, sizeof(read_result));
    status = PsFatFsStorage_ReadFileToMemory(path,
                                             (UINTPTR)s_ps_app_save_snapshot,
                                             logical_bytes,
                                             &read_result);
    if (status != XST_SUCCESS) {
        return status;
    }

    /* gba_mister 的保存窗口是 4x 展开格式：每个逻辑字节占用一个 32-bit 槽位。
     * 从磁盘加载时需要把标准存档格式“解包”到 core 可直接读取的窗口布局。 */
    window_ptr = (u8 *)base_addr;
    memset(window_ptr, 0xFF, window_bytes);
    for (idx = 0U; idx < read_result.bytes_loaded; ++idx) {
        u8 v = s_ps_app_save_snapshot[idx];
        u32 off = idx * PS_APP_GBA_SAVE_EXPAND_STRIDE;
        window_ptr[off + 0U] = v;
        window_ptr[off + 1U] = v;
        window_ptr[off + 2U] = v;
        window_ptr[off + 3U] = v;
    }
    Xil_DCacheFlushRange((INTPTR)base_addr, window_bytes);

    ctx->state->loaded_from_sd = 1U;
    ctx->state->last_bytes = read_result.bytes_loaded;
    ctx->state->last_checksum = PsAppSave_Checksum((UINTPTR)s_ps_app_save_snapshot, read_result.bytes_loaded);
    ctx->state->active_kind = (u8)kind;
    PsAppSave_CopyText(ctx->state->path, sizeof(ctx->state->path), path);
    if (loaded_out != NULL) {
        *loaded_out = 1U;
    }
    return XST_SUCCESS;
}

static PsAppSaveKind PsAppSave_ResolveDirtyKind(u32 old_status, u32 new_status) {
    u32 old_sram;
    u32 old_flash;
    u32 old_eeprom;
    u32 new_sram;
    u32 new_flash;
    u32 new_eeprom;

    old_sram = old_status & 0xFFU;
    old_flash = (old_status >> 8) & 0xFFU;
    old_eeprom = (old_status >> 16) & 0xFFU;
    new_sram = new_status & 0xFFU;
    new_flash = (new_status >> 8) & 0xFFU;
    new_eeprom = (new_status >> 16) & 0xFFU;

    /* SAVE_STATUS 是事件计数器而非“脏位”。
     * 只要对应计数变化，就说明该介质出现了新写入事件。
     * 优先级 EEPROM > FLASH > SRAM：EEPROM 最敏感，优先保证及时落盘。 */
    if (new_eeprom != old_eeprom) {
        return PS_APP_SAVE_KIND_EEPROM;
    }
    if (new_flash != old_flash) {
        return PS_APP_SAVE_KIND_FLASH;
    }
    if (new_sram != old_sram) {
        return PS_APP_SAVE_KIND_SRAM;
    }
    return PS_APP_SAVE_KIND_NONE;
}

static XStatus PsAppSave_FlushKind(PsAppSaveContext *ctx, PsAppSaveKind kind) {
    PsFatFsStorageWriteResult write_result;
    char path[PS_APP_ROM_PATH_MAX_CHARS];
    UINTPTR base_addr;
    UINTPTR snapshot_addr;
    u32 logical_bytes;
    u32 window_bytes;
    u32 idx;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->rom == NULL)) {
        return XST_FAILURE;
    }
    if (ctx->rom->path[0] == '\0') {
        return XST_INVALID_PARAM;
    }

    PsAppSave_BuildPath(ctx->rom->path, kind, path, sizeof(path));
    if (path[0] == '\0') {
        return XST_INVALID_PARAM;
    }

    base_addr = PsAppSave_BaseAddr(kind);
    logical_bytes = PsAppSave_ByteCount(kind);
    window_bytes = PsAppSave_WindowByteCount(kind);
    xil_printf("[SAVE] flush begin %s logical=%u window=%u\r\n",
               path,
               (unsigned int)logical_bytes,
               (unsigned int)window_bytes);
    if ((logical_bytes == 0U) || (window_bytes == 0U)) {
        return XST_FAILURE;
    }
    if (PsAppSave_SnapshotWindow(ctx, base_addr, window_bytes, &snapshot_addr) != XST_SUCCESS) {
        xil_printf("[SAVE] snapshot failed base=0x%08x bytes=%u\r\n",
                   (unsigned int)base_addr,
                   (unsigned int)window_bytes);
        return XST_FAILURE;
    }

    /* 从 core 的 4x 展开窗口中“压缩”回标准存档文件格式。 */
    for (idx = 0U; idx < logical_bytes; ++idx) {
        s_ps_app_save_snapshot[idx] = s_ps_app_save_snapshot[idx * PS_APP_GBA_SAVE_EXPAND_STRIDE];
    }
    Xil_DCacheFlushRange((INTPTR)s_ps_app_save_snapshot, logical_bytes);
    xil_printf("[SAVE] snapshot done logical=%u\r\n", (unsigned int)logical_bytes);

    memset(&write_result, 0, sizeof(write_result));
    if (PsFatFsStorage_WriteMemoryToFile(path,
                                         (UINTPTR)s_ps_app_save_snapshot,
                                         logical_bytes,
                                         &write_result) != XST_SUCCESS) {
        xil_printf("[SAVE] flush failed %s fs=%s\r\n",
                   path,
                   PsFatFsStorage_StrError(write_result.fs_result));
        return XST_FAILURE;
    }

    if ((!PsAppSave_IsPtrInDdr(ctx)) ||
        (!PsAppSave_IsPtrInDdr(ctx->state)) ||
        (!PsAppSave_IsPtrInDdr(ctx->rom))) {
        /* 写盘返回后再次校验，防止后续 state 写回把系统带崩。 */
        xil_printf("[SAVE] postwrite ptr invalid ctx=0x%08x state=0x%08x rom=0x%08x\r\n",
                   (unsigned int)(UINTPTR)ctx,
                   (unsigned int)(UINTPTR)ctx->state,
                   (unsigned int)(UINTPTR)ctx->rom);
        return XST_FAILURE;
    }

    ctx->state->flush_count++;
    ctx->state->dirty = 0U;
    ctx->state->quiet_ticks = 0U;
    ctx->state->dirty_age_ticks = 0U;
    ctx->state->active_kind = (u8)kind;
    ctx->state->last_bytes = write_result.bytes_written;
#if PS_APP_SAVE_ENABLE_POSTWRITE_CHECKSUM
    ctx->state->last_checksum = PsAppSave_Checksum((UINTPTR)s_ps_app_save_snapshot, logical_bytes);
#else
    ctx->state->last_checksum = 0U;
#endif
    PsAppSave_CopyText(ctx->state->path, sizeof(ctx->state->path), path);
    xil_printf("[SAVE] flush %s bytes=%u checksum=0x%08x\r\n",
               path,
               (unsigned int)ctx->state->last_bytes,
               (unsigned int)ctx->state->last_checksum);
    return XST_SUCCESS;
}

void PsAppSave_Reset(PsAppSaveContext *ctx) {
    if ((ctx == NULL) || (ctx->state == NULL)) {
        return;
    }

    memset(ctx->state, 0, sizeof(*ctx->state));
    /* FLASH 窗口覆盖 SRAM/FLASH 共享区域，因此清 FLASH 即可同时清 SRAM 视图。 */
    PsAppSave_ClearWindow(PS_APP_SAVE_KIND_FLASH);
    PsAppSave_ClearWindow(PS_APP_SAVE_KIND_EEPROM);
}

XStatus PsAppSave_PrepareForRom(PsAppSaveContext *ctx, const char *rom_path) {
    char log_path[PS_APP_ROM_PATH_MAX_CHARS];
    const char *print_path;
    u8 loaded;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_FAILURE;
    }

    PsAppSave_Reset(ctx);
    if ((rom_path == NULL) || (*rom_path == '\0')) {
        return XST_INVALID_PARAM;
    }

    if (PsFatFsStorage_EnsureDirectory(PS_APP_SAVE_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    if ((ctx->rom != NULL) && (ctx->rom->path[0] != '\0')) {
        PsAppSave_CopyTextBounded(log_path,
                                  sizeof(log_path),
                                  ctx->rom->path,
                                  sizeof(log_path) - 1U);
    } else {
        PsAppSave_CopyTextBounded(log_path,
                                  sizeof(log_path),
                                  rom_path,
                                  sizeof(log_path) - 1U);
    }
    PsAppSave_SanitizeAsciiInPlace(log_path);
    print_path = (log_path[0] != '\0') ? log_path : "(unknown)";

    loaded = 0U;
    /* 加载优先级说明：
     * 1) 先尝试 FLASH（覆盖 FLASH1M/FLASH 场景）；
     * 2) FLASH 不存在再尝试 SRAM；
     * 3) EEPROM 独立尝试（可与前两者并存于窗口布局中）。
     * 这样可兼容历史文件命名并最大化命中可恢复存档。 */
    if (PsAppSave_TryLoadKind(ctx, rom_path, PS_APP_SAVE_KIND_FLASH, &loaded) != XST_SUCCESS) {
        (void)PsAppSave_TryLoadKind(ctx, rom_path, PS_APP_SAVE_KIND_SRAM, &loaded);
    }
    if (PsAppSave_TryLoadKind(ctx, rom_path, PS_APP_SAVE_KIND_EEPROM, NULL) == XST_SUCCESS) {
        loaded = 1U;
    }
    if (loaded != 0U) {
        xil_printf("[SAVE] preload %s\r\n", ctx->state->path);
    } else {
        xil_printf("[SAVE] no existing save for %s\r\n", print_path);
    }

    return XST_SUCCESS;
}

void PsAppSave_Service(PsAppSaveContext *ctx) {
    u32 status;
    PsAppSaveKind dirty_kind;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->regs == NULL) || (ctx->rom == NULL)) {
        return;
    }
    if (ctx->rom->loaded == 0U) {
        return;
    }

    /* 每次 service 轮询硬件计数器，检测是否出现新的 save 写入事件。 */
    status = PsGbaRegs_Read(ctx->regs, GBA_REG_SAVE_STATUS);
    dirty_kind = PsAppSave_ResolveDirtyKind(ctx->state->event_counters, status);
    if (dirty_kind != PS_APP_SAVE_KIND_NONE) {
        ctx->state->event_counters = status;
        if ((ctx->state->dirty == 0U) ||
            (ctx->state->active_kind != (u8)dirty_kind)) {
            ctx->state->dirty_age_ticks = 0U;
        }
        ctx->state->active_kind = (u8)dirty_kind;
        ctx->state->dirty = 1U;
        ctx->state->quiet_ticks = 0U;
    }

    if (ctx->state->dirty == 0U) {
        return;
    }

    ctx->state->quiet_ticks++;
    if (ctx->state->dirty_age_ticks < 0xFFFFFFFFU) {
        ctx->state->dirty_age_ticks++;
    }
    /* 双阈值策略：
     * quiet: 写入事件安静一段时间后再落盘，减少碎片写；
     * hard : 即使一直有写入抖动，也在上限时间强制落盘。 */
    if ((ctx->state->quiet_ticks < PS_APP_SAVE_FLUSH_QUIET_TICKS) &&
        (ctx->state->dirty_age_ticks < PS_APP_SAVE_FLUSH_HARD_TICKS)) {
        return;
    }

    (void)PsAppSave_FlushKind(ctx, (PsAppSaveKind)ctx->state->active_kind);
}

XStatus PsAppSave_FlushIfDirty(PsAppSaveContext *ctx) {
    PsAppSaveKind kind;

    if ((ctx == NULL) || (ctx->state == NULL) || (ctx->rom == NULL)) {
        return XST_FAILURE;
    }
    if ((ctx->rom->loaded == 0U) || (ctx->state->dirty == 0U)) {
        return XST_SUCCESS;
    }

    /* 仅允许三种有效 save 介质，避免异常值导致写错窗口。 */
    kind = (PsAppSaveKind)ctx->state->active_kind;
    if ((kind != PS_APP_SAVE_KIND_SRAM) &&
        (kind != PS_APP_SAVE_KIND_FLASH) &&
        (kind != PS_APP_SAVE_KIND_EEPROM)) {
        return XST_FAILURE;
    }

    return PsAppSave_FlushKind(ctx, kind);
}
