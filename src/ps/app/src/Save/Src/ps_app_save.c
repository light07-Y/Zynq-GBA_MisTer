#include "Save/Inc/ps_app_save.h"

#include <string.h>

#include "Storage/Inc/ps_fatfs_storage.h"
#include "xil_cache.h"
#include "xil_printf.h"

static UINTPTR PsAppSave_BaseAddr(PsAppSaveKind kind) {
    switch (kind) {
        case PS_APP_SAVE_KIND_SRAM:
        case PS_APP_SAVE_KIND_FLASH:
            return (UINTPTR)PS_APP_GBA_SAVE_REGION_BASE_ADDR;
        case PS_APP_SAVE_KIND_EEPROM:
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
    bytes = PsAppSave_ByteCount(kind);
    if ((base_addr == 0U) || (bytes == 0U)) {
        return;
    }

    memset((void *)base_addr, 0, bytes);
    Xil_DCacheFlushRange((INTPTR)base_addr, bytes);
}

static XStatus PsAppSave_TryLoadKind(PsAppSaveContext *ctx,
                                     const char *rom_path,
                                     PsAppSaveKind kind,
                                     u8 *loaded_out) {
    PsFatFsStorageReadResult read_result;
    char path[PS_APP_ROM_PATH_MAX_CHARS];
    XStatus status;

    if ((ctx == NULL) || (ctx->state == NULL)) {
        return XST_FAILURE;
    }

    PsAppSave_BuildPath(rom_path, kind, path, sizeof(path));
    if (path[0] == '\0') {
        return XST_INVALID_PARAM;
    }

    memset(&read_result, 0, sizeof(read_result));
    status = PsFatFsStorage_ReadFileToMemory(path,
                                             PsAppSave_BaseAddr(kind),
                                             PsAppSave_ByteCount(kind),
                                             &read_result);
    if (status != XST_SUCCESS) {
        return status;
    }

    ctx->state->loaded_from_sd = 1U;
    ctx->state->last_bytes = read_result.bytes_loaded;
    ctx->state->last_checksum = PsAppSave_Checksum(PsAppSave_BaseAddr(kind), read_result.bytes_loaded);
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
    u32 bytes;

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

    if (PsFatFsStorage_EnsureDirectory(PS_APP_SAVE_SD_DIR) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    base_addr = PsAppSave_BaseAddr(kind);
    bytes = PsAppSave_ByteCount(kind);
    memset(&write_result, 0, sizeof(write_result));
    if (PsFatFsStorage_WriteMemoryToFile(path, base_addr, bytes, &write_result) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ctx->state->flush_count++;
    ctx->state->dirty = 0U;
    ctx->state->quiet_ticks = 0U;
    ctx->state->active_kind = (u8)kind;
    ctx->state->last_bytes = write_result.bytes_written;
    ctx->state->last_checksum = PsAppSave_Checksum(base_addr, bytes);
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

    status = PsGbaRegs_Read(ctx->regs, GBA_REG_SAVE_STATUS);
    dirty_kind = PsAppSave_ResolveDirtyKind(ctx->state->event_counters, status);
    if (dirty_kind != PS_APP_SAVE_KIND_NONE) {
        ctx->state->event_counters = status;
        ctx->state->active_kind = (u8)dirty_kind;
        ctx->state->dirty = 1U;
        ctx->state->quiet_ticks = 0U;
    }

    if (ctx->state->dirty == 0U) {
        return;
    }

    ctx->state->quiet_ticks++;
    if (ctx->state->quiet_ticks < PS_APP_SAVE_FLUSH_QUIET_TICKS) {
        return;
    }

    (void)PsAppSave_FlushKind(ctx, (PsAppSaveKind)ctx->state->active_kind);
}
