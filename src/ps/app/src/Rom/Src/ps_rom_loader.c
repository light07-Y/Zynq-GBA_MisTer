#include "Rom/Inc/ps_rom_loader.h"

#include <string.h>

#include "xil_cache.h"

const char *PsRomLoader_StrError(FRESULT result) {
    return PsFatFsStorage_StrError(result);
}

XStatus PsRomLoader_LoadSdFile(const char *path,
                               UINTPTR ddr_base_addr,
                               u32 ddr_capacity_bytes,
                               PsRomLoadResult *result_out) {
    PsFatFsStorageReadResult storage_result;
    u32 bytes_aligned;

    if (result_out != NULL) {
        memset(result_out, 0, sizeof(*result_out));
        result_out->ddr_base_addr = ddr_base_addr;
    }

    if ((path == NULL) || (*path == '\0') || (ddr_capacity_bytes == 0U)) {
        return XST_INVALID_PARAM;
    }

    memset(&storage_result, 0, sizeof(storage_result));
    if (PsFatFsStorage_ReadFileToMemory(path,
                                        ddr_base_addr,
                                        ddr_capacity_bytes,
                                        &storage_result) != XST_SUCCESS) {
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
        result_out->fs_result = FR_OK;
    }

    return XST_SUCCESS;
}
