#include "Storage/Inc/ps_fatfs_storage.h"

#include <string.h>

#include "xil_cache.h"

#define PS_FATFS_STORAGE_DRIVE_PATH       "0:/"
#define PS_FATFS_STORAGE_CHUNK_BYTES      (128U * 1024U)

static FATFS g_ps_fatfs;

const char *PsFatFsStorage_StrError(FRESULT result) {
    switch (result) {
        case FR_OK: return "ok";
        case FR_DISK_ERR: return "disk_err";
        case FR_INT_ERR: return "int_err";
        case FR_NOT_READY: return "not_ready";
        case FR_NO_FILE: return "no_file";
        case FR_NO_PATH: return "no_path";
        case FR_INVALID_NAME: return "invalid_name";
        case FR_DENIED: return "denied";
        case FR_EXIST: return "exist";
        case FR_INVALID_OBJECT: return "invalid_object";
        case FR_WRITE_PROTECTED: return "write_protected";
        case FR_INVALID_DRIVE: return "invalid_drive";
        case FR_NOT_ENABLED: return "not_enabled";
        case FR_NO_FILESYSTEM: return "no_filesystem";
        case FR_MKFS_ABORTED: return "mkfs_aborted";
        case FR_TIMEOUT: return "timeout";
        case FR_LOCKED: return "locked";
        case FR_NOT_ENOUGH_CORE: return "no_core";
        case FR_TOO_MANY_OPEN_FILES: return "too_many_open";
        case FR_INVALID_PARAMETER: return "invalid_param";
        default: return "unknown";
    }
}

XStatus PsFatFsStorage_ReadFileToMemory(const char *path,
                                       UINTPTR dst_addr,
                                       u32 capacity_bytes,
                                       PsFatFsStorageReadResult *result_out) {
    FIL file;
    FRESULT fs_result;
    FSIZE_t file_size;
    UINT bytes_read;
    u8 *dst_ptr;
    u32 offset;

    if (result_out != NULL) {
        memset(result_out, 0, sizeof(*result_out));
        result_out->dst_addr = dst_addr;
    }

    if ((path == NULL) || (*path == '\0') || (capacity_bytes == 0U)) {
        return XST_INVALID_PARAM;
    }

    dst_ptr = (u8 *)dst_addr;
    offset = 0U;
    memset(&file, 0, sizeof(file));

    fs_result = f_mount(&g_ps_fatfs, PS_FATFS_STORAGE_DRIVE_PATH, 0);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    if (fs_result != FR_OK) {
        return XST_FAILURE;
    }

    fs_result = f_open(&file, path, FA_READ);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    if (fs_result != FR_OK) {
        return XST_FAILURE;
    }

    file_size = f_size(&file);
    if ((file_size == 0U) || (file_size > (FSIZE_t)capacity_bytes)) {
        (void)f_close(&file);
        return XST_FAILURE;
    }

    while (offset < (u32)file_size) {
        u32 chunk_bytes;

        chunk_bytes = (u32)file_size - offset;
        if (chunk_bytes > PS_FATFS_STORAGE_CHUNK_BYTES) {
            chunk_bytes = PS_FATFS_STORAGE_CHUNK_BYTES;
        }

        bytes_read = 0U;
        fs_result = f_read(&file, &dst_ptr[offset], chunk_bytes, &bytes_read);
        if (result_out != NULL) {
            result_out->fs_result = fs_result;
        }
        if ((fs_result != FR_OK) || (bytes_read != chunk_bytes)) {
            (void)f_close(&file);
            return XST_FAILURE;
        }

        Xil_DCacheFlushRange((INTPTR)&dst_ptr[offset], bytes_read);
        offset += bytes_read;
    }

    (void)f_close(&file);

    if (result_out != NULL) {
        result_out->bytes_loaded = (u32)file_size;
        result_out->fs_result = FR_OK;
    }

    return XST_SUCCESS;
}
