#include "Rom/Inc/ps_rom_loader.h"

#include <string.h>

#include "Storage/Inc/ps_fatfs_storage.h"
#include "xil_cache.h"

typedef struct {
    UINTPTR ddr_base_addr;
    u32 flash1m_offset;
    u32 flash_offset;
    u32 sram_offset;
    u32 eeprom_offset;
} PsRomLoaderScanContext;

static u32 PsRomLoader_FindAsciiSignature(const u8 *haystack,
                                          u32 haystack_len,
                                          const char *needle) {
    u32 idx;
    size_t needle_len;

    if ((haystack == NULL) || (needle == NULL)) {
        return 0xFFFFFFFFU;
    }

    needle_len = strlen(needle);
    if ((needle_len == 0U) || (haystack_len < needle_len)) {
        return 0xFFFFFFFFU;
    }

    for (idx = 0U; idx <= (haystack_len - (u32)needle_len); ++idx) {
        if (memcmp(&haystack[idx], needle, needle_len) == 0) {
            return idx;
        }
    }

    return 0xFFFFFFFFU;
}

static void PsRomLoader_ScanChunk(const u8 *chunk_ptr,
                                  u32 chunk_offset,
                                  u32 chunk_bytes,
                                  void *user_ctx) {
    PsRomLoaderScanContext *ctx;
    u32 overlap;
    const u8 *scan_ptr;
    u32 scan_base;
    u32 scan_len;
    u32 rel_off;

    ctx = (PsRomLoaderScanContext *)user_ctx;
    if ((ctx == NULL) || (chunk_ptr == NULL) || (chunk_bytes == 0U)) {
        return;
    }

    overlap = (chunk_offset > 8U) ? 8U : chunk_offset;
    scan_ptr = &chunk_ptr[0] - overlap;
    scan_base = chunk_offset - overlap;
    scan_len = chunk_bytes + overlap;

    if (ctx->flash1m_offset == 0xFFFFFFFFU) {
        rel_off = PsRomLoader_FindAsciiSignature(scan_ptr, scan_len, "FLASH1M_V");
        if (rel_off != 0xFFFFFFFFU) {
            ctx->flash1m_offset = scan_base + rel_off;
        }
    }
    if (ctx->flash_offset == 0xFFFFFFFFU) {
        rel_off = PsRomLoader_FindAsciiSignature(scan_ptr, scan_len, "FLASH_V");
        if (rel_off != 0xFFFFFFFFU) {
            ctx->flash_offset = scan_base + rel_off;
        }
    }
    if (ctx->sram_offset == 0xFFFFFFFFU) {
        rel_off = PsRomLoader_FindAsciiSignature(scan_ptr, scan_len, "SRAM_V");
        if (rel_off != 0xFFFFFFFFU) {
            ctx->sram_offset = scan_base + rel_off;
        }
    }
    if (ctx->eeprom_offset == 0xFFFFFFFFU) {
        rel_off = PsRomLoader_FindAsciiSignature(scan_ptr, scan_len, "EEPROM_V");
        if (rel_off != 0xFFFFFFFFU) {
            ctx->eeprom_offset = scan_base + rel_off;
        }
    }
}

const char *PsRomLoader_StrError(FRESULT result) {
    return PsFatFsStorage_StrError(result);
}

XStatus PsRomLoader_LoadSdFile(const char *path,
                               UINTPTR ddr_base_addr,
                               u32 ddr_capacity_bytes,
                               PsRomLoadResult *result_out) {
    PsFatFsStorageReadResult storage_result;
    PsRomLoaderScanContext scan_ctx;
    u32 bytes_aligned;

    if (result_out != NULL) {
        memset(result_out, 0, sizeof(*result_out));
        result_out->ddr_base_addr = ddr_base_addr;
        result_out->flash1m_offset = 0xFFFFFFFFU;
        result_out->flash_offset = 0xFFFFFFFFU;
        result_out->sram_offset = 0xFFFFFFFFU;
        result_out->eeprom_offset = 0xFFFFFFFFU;
    }

    if ((path == NULL) || (*path == '\0') || (ddr_capacity_bytes == 0U)) {
        return XST_INVALID_PARAM;
    }

    memset(&storage_result, 0, sizeof(storage_result));
    memset(&scan_ctx, 0, sizeof(scan_ctx));
    scan_ctx.ddr_base_addr = ddr_base_addr;
    scan_ctx.flash1m_offset = 0xFFFFFFFFU;
    scan_ctx.flash_offset = 0xFFFFFFFFU;
    scan_ctx.sram_offset = 0xFFFFFFFFU;
    scan_ctx.eeprom_offset = 0xFFFFFFFFU;
    if (PsFatFsStorage_ReadFileToMemoryEx(path,
                                          ddr_base_addr,
                                          ddr_capacity_bytes,
                                          &storage_result,
                                          PsRomLoader_ScanChunk,
                                          &scan_ctx) != XST_SUCCESS) {
        if (result_out != NULL) {
            result_out->fs_result = storage_result.fs_result;
        }
        return XST_FAILURE;
    }

    bytes_aligned = (storage_result.bytes_loaded + 3U) & ~0x3U;
    if (bytes_aligned > storage_result.bytes_loaded) {
        u8 *ddr_ptr = (u8 *)ddr_base_addr;
        memset(&ddr_ptr[storage_result.bytes_loaded], 0, bytes_aligned - storage_result.bytes_loaded);
        Xil_DCacheFlushRange((INTPTR)&ddr_ptr[storage_result.bytes_loaded],
                             bytes_aligned - storage_result.bytes_loaded);
    }

    if (result_out != NULL) {
        result_out->bytes_loaded = storage_result.bytes_loaded;
        result_out->bytes_aligned = bytes_aligned;
        result_out->max_pak_addr = bytes_aligned >> 2;
        result_out->flash1m_offset = scan_ctx.flash1m_offset;
        result_out->flash_offset = scan_ctx.flash_offset;
        result_out->sram_offset = scan_ctx.sram_offset;
        result_out->eeprom_offset = scan_ctx.eeprom_offset;
        if (storage_result.bytes_loaded >= 0xB2U) {
            const u8 *rom = (const u8 *)ddr_base_addr;
            memcpy(result_out->game_code, &rom[0xACU], 4U);
            result_out->game_code[4] = '\0';
            memcpy(result_out->maker_code, &rom[0xB0U], 2U);
            result_out->maker_code[2] = '\0';
        }
        result_out->fs_result = FR_OK;
    }

    return XST_SUCCESS;
}
