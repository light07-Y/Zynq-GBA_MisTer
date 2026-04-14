#include "Storage/Inc/ps_fatfs_storage.h"

#include <string.h>

#include "xil_cache.h"

#define PS_FATFS_STORAGE_DRIVE_PATH       "0:/"
#define PS_FATFS_STORAGE_CHUNK_BYTES      (128U * 1024U)

static FATFS g_ps_fatfs;

static FRESULT PsFatFsStorage_Mount(void) {
    return f_mount(&g_ps_fatfs, PS_FATFS_STORAGE_DRIVE_PATH, 0);
}

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

XStatus PsFatFsStorage_ReadFileToMemoryEx(const char *path,
                                         UINTPTR dst_addr,
                                         u32 capacity_bytes,
                                         PsFatFsStorageReadResult *result_out,
                                         PsFatFsStorageReadChunkCallback chunk_callback,
                                         void *user_ctx) {
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

    fs_result = PsFatFsStorage_Mount();
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
        if (chunk_callback != NULL) {
            chunk_callback(&dst_ptr[offset], offset, bytes_read, user_ctx);
        }
        offset += bytes_read;
    }

    (void)f_close(&file);

    if (result_out != NULL) {
        result_out->bytes_loaded = (u32)file_size;
        result_out->fs_result = FR_OK;
    }

    return XST_SUCCESS;
}

XStatus PsFatFsStorage_ReadFileToMemory(const char *path,
                                       UINTPTR dst_addr,
                                       u32 capacity_bytes,
                                       PsFatFsStorageReadResult *result_out) {
    return PsFatFsStorage_ReadFileToMemoryEx(path,
                                             dst_addr,
                                             capacity_bytes,
                                             result_out,
                                             NULL,
                                             NULL);
}

XStatus PsFatFsStorage_WriteMemoryToFile(const char *path,
                                        UINTPTR src_addr,
                                        u32 bytes_to_write,
                                        PsFatFsStorageWriteResult *result_out) {
    FIL file;
    FRESULT fs_result;
    UINT bytes_written;
    const u8 *src_ptr;
    u32 offset;

    if (result_out != NULL) {
        memset(result_out, 0, sizeof(*result_out));
        result_out->src_addr = src_addr;
    }

    if ((path == NULL) || (*path == '\0') || (bytes_to_write == 0U)) {
        return XST_INVALID_PARAM;
    }

    src_ptr = (const u8 *)src_addr;
    offset = 0U;
    memset(&file, 0, sizeof(file));

    fs_result = PsFatFsStorage_Mount();
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    if (fs_result != FR_OK) {
        return XST_FAILURE;
    }

    fs_result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    if (fs_result != FR_OK) {
        return XST_FAILURE;
    }

    Xil_DCacheFlushRange((INTPTR)src_addr, bytes_to_write);

    while (offset < bytes_to_write) {
        u32 chunk_bytes;

        chunk_bytes = bytes_to_write - offset;
        if (chunk_bytes > PS_FATFS_STORAGE_CHUNK_BYTES) {
            chunk_bytes = PS_FATFS_STORAGE_CHUNK_BYTES;
        }

        bytes_written = 0U;
        fs_result = f_write(&file, &src_ptr[offset], chunk_bytes, &bytes_written);
        if (result_out != NULL) {
            result_out->fs_result = fs_result;
        }
        if ((fs_result != FR_OK) || (bytes_written != chunk_bytes)) {
            (void)f_close(&file);
            return XST_FAILURE;
        }

        offset += bytes_written;
    }

    fs_result = f_sync(&file);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    if (fs_result != FR_OK) {
        (void)f_close(&file);
        return XST_FAILURE;
    }

    (void)f_close(&file);

    if (result_out != NULL) {
        result_out->bytes_written = bytes_to_write;
        result_out->fs_result = FR_OK;
    }

    return XST_SUCCESS;
}

XStatus PsFatFsStorage_EnsureDirectory(const char *path) {
    FILINFO info;
    FRESULT fs_result;

    if ((path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    fs_result = PsFatFsStorage_Mount();
    if (fs_result != FR_OK) {
        return XST_FAILURE;
    }

    memset(&info, 0, sizeof(info));
    fs_result = f_stat(path, &info);
    if (fs_result == FR_OK) {
        return ((info.fattrib & AM_DIR) != 0U) ? XST_SUCCESS : XST_FAILURE;
    }
    if ((fs_result != FR_NO_FILE) && (fs_result != FR_NO_PATH)) {
        return XST_FAILURE;
    }

    fs_result = f_mkdir(path);
    if ((fs_result == FR_OK) || (fs_result == FR_EXIST)) {
        return XST_SUCCESS;
    }

    return XST_FAILURE;
}
