#include "Storage/Inc/ps_fatfs_storage.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "xil_cache.h"
#include "xil_printf.h"

#define PS_FATFS_STORAGE_DRIVE_PATH       "0:/"
#define PS_FATFS_STORAGE_READ_CHUNK_BYTES  (128U * 1024U)
#define PS_FATFS_STORAGE_WRITE_CHUNK_BYTES (8U * 1024U)
#define PS_FATFS_STORAGE_SAVEWR_VERBOSE    0U

#if PS_FATFS_STORAGE_SAVEWR_VERBOSE
#define PS_SAVEWR_LOG(...) xil_printf(__VA_ARGS__)
#else
#define PS_SAVEWR_LOG(...) do { } while (0)
#endif

static FATFS g_ps_fatfs;
static SemaphoreHandle_t g_ps_fatfs_lock;

static SemaphoreHandle_t PsFatFsStorage_GetLock(void) {
    SemaphoreHandle_t lock;

    lock = g_ps_fatfs_lock;
    if (lock != NULL) {
        return lock;
    }

    lock = xSemaphoreCreateMutex();
    if (lock == NULL) {
        return NULL;
    }

    if (g_ps_fatfs_lock == NULL) {
        g_ps_fatfs_lock = lock;
        return g_ps_fatfs_lock;
    }

    (void)vSemaphoreDelete(lock);
    return g_ps_fatfs_lock;
}

static void PsFatFsStorage_Lock(void) {
    SemaphoreHandle_t lock;

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return;
    }

    lock = PsFatFsStorage_GetLock();
    if (lock != NULL) {
        xil_printf("[SAVEWR] lock wait\r\n");
        (void)xSemaphoreTake(lock, portMAX_DELAY);
        xil_printf("[SAVEWR] lock ok\r\n");
    }
}

static void PsFatFsStorage_Unlock(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return;
    }

    if (g_ps_fatfs_lock != NULL) {
        (void)xSemaphoreGive(g_ps_fatfs_lock);
    }
}

static void PsFatFsStorage_YieldAfterWriteChunk(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        taskYIELD();
    }
}

static TickType_t PsFatFsStorage_GetTickNow(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        return xTaskGetTickCount();
    }
    return 0U;
}

static u32 PsFatFsStorage_ElapsedMs(TickType_t start_tick) {
    TickType_t delta_ticks;
    u64 elapsed_ms;
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return 0U;
    }
    delta_ticks = xTaskGetTickCount() - start_tick;
    elapsed_ms = ((u64)delta_ticks * 1000ULL) / (u64)configTICK_RATE_HZ;
    if (elapsed_ms > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFU;
    }
    return (u32)elapsed_ms;
}

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
    XStatus status;

    if (result_out != NULL) {
        memset(result_out, 0, sizeof(*result_out));
        result_out->dst_addr = dst_addr;
    }

    if ((path == NULL) || (*path == '\0') || (capacity_bytes == 0U)) {
        return XST_INVALID_PARAM;
    }

    status = XST_FAILURE;
    dst_ptr = (u8 *)dst_addr;
    offset = 0U;
    memset(&file, 0, sizeof(file));
    PsFatFsStorage_Lock();

    fs_result = PsFatFsStorage_Mount();
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    fs_result = f_open(&file, path, FA_READ);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    file_size = f_size(&file);
    if ((file_size == 0U) || (file_size > (FSIZE_t)capacity_bytes)) {
        (void)f_close(&file);
        goto cleanup;
    }

    while (offset < (u32)file_size) {
        u32 chunk_bytes;

        chunk_bytes = (u32)file_size - offset;
        if (chunk_bytes > PS_FATFS_STORAGE_READ_CHUNK_BYTES) {
            chunk_bytes = PS_FATFS_STORAGE_READ_CHUNK_BYTES;
        }

        bytes_read = 0U;
        fs_result = f_read(&file, &dst_ptr[offset], chunk_bytes, &bytes_read);
        if (result_out != NULL) {
            result_out->fs_result = fs_result;
        }
        if ((fs_result != FR_OK) || (bytes_read != chunk_bytes)) {
            (void)f_close(&file);
            goto cleanup;
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
    status = XST_SUCCESS;

cleanup:
    PsFatFsStorage_Unlock();
    return status;
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
    FRESULT close_result;
    UINT bytes_written;
    const u8 *src_ptr;
    u32 offset;
    u32 chunk_index;
    u32 chunk_total;
    XStatus status;
    TickType_t total_start_tick;
    TickType_t step_start_tick;

    if (result_out != NULL) {
        memset(result_out, 0, sizeof(*result_out));
        result_out->src_addr = src_addr;
    }

    if ((path == NULL) || (*path == '\0') || (bytes_to_write == 0U)) {
        return XST_INVALID_PARAM;
    }

    status = XST_FAILURE;
    src_ptr = (const u8 *)src_addr;
    offset = 0U;
    chunk_index = 0U;
    chunk_total = (bytes_to_write + (PS_FATFS_STORAGE_WRITE_CHUNK_BYTES - 1U)) /
                  PS_FATFS_STORAGE_WRITE_CHUNK_BYTES;
    memset(&file, 0, sizeof(file));
    total_start_tick = PsFatFsStorage_GetTickNow();
    xil_printf("[SAVEWR] enter path=%s src=0x%08x bytes=%u\r\n",
               (path != NULL) ? path : "(null)",
               (unsigned int)src_addr,
               (unsigned int)bytes_to_write);
    PS_SAVEWR_LOG("[SAVEWR] begin path=%s bytes=%u chunk=%u total_chunks=%u\r\n",
                  path,
                  (unsigned int)bytes_to_write,
                  (unsigned int)PS_FATFS_STORAGE_WRITE_CHUNK_BYTES,
                  (unsigned int)chunk_total);
    PsFatFsStorage_Lock();

    step_start_tick = PsFatFsStorage_GetTickNow();
    xil_printf("[SAVEWR] mount begin\r\n");
    fs_result = PsFatFsStorage_Mount();
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    PS_SAVEWR_LOG("[SAVEWR] mount fs=%s dt=%u ms\r\n",
                  PsFatFsStorage_StrError(fs_result),
                  (unsigned int)PsFatFsStorage_ElapsedMs(step_start_tick));
    if (fs_result != FR_OK) {
        goto cleanup;
    }
    xil_printf("[SAVEWR] mount ok\r\n");

    step_start_tick = PsFatFsStorage_GetTickNow();
    xil_printf("[SAVEWR] open begin\r\n");
    fs_result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    PS_SAVEWR_LOG("[SAVEWR] open fs=%s dt=%u ms\r\n",
                  PsFatFsStorage_StrError(fs_result),
                  (unsigned int)PsFatFsStorage_ElapsedMs(step_start_tick));
    if (fs_result != FR_OK) {
        goto cleanup;
    }
    xil_printf("[SAVEWR] open ok\r\n");

    step_start_tick = PsFatFsStorage_GetTickNow();
    Xil_DCacheFlushRange((INTPTR)src_addr, bytes_to_write);
    xil_printf("[SAVEWR] dcache flush ok\r\n");
    PS_SAVEWR_LOG("[SAVEWR] dcache flush bytes=%u dt=%u ms\r\n",
                  (unsigned int)bytes_to_write,
                  (unsigned int)PsFatFsStorage_ElapsedMs(step_start_tick));

    while (offset < bytes_to_write) {
        u32 chunk_bytes;
        TickType_t chunk_start_tick;

        chunk_bytes = bytes_to_write - offset;
        if (chunk_bytes > PS_FATFS_STORAGE_WRITE_CHUNK_BYTES) {
            chunk_bytes = PS_FATFS_STORAGE_WRITE_CHUNK_BYTES;
        }

        chunk_start_tick = PsFatFsStorage_GetTickNow();
        bytes_written = 0U;
        if (chunk_index == 0U) {
            xil_printf("[SAVEWR] first chunk begin off=%u size=%u\r\n",
                       (unsigned int)offset,
                       (unsigned int)chunk_bytes);
        }
        fs_result = f_write(&file, &src_ptr[offset], chunk_bytes, &bytes_written);
        if (result_out != NULL) {
            result_out->fs_result = fs_result;
        }
        chunk_index++;
        PS_SAVEWR_LOG("[SAVEWR] chunk %u/%u off=%u req=%u wrote=%u fs=%s dt=%u ms\r\n",
                      (unsigned int)chunk_index,
                      (unsigned int)chunk_total,
                      (unsigned int)offset,
                      (unsigned int)chunk_bytes,
                      (unsigned int)bytes_written,
                      PsFatFsStorage_StrError(fs_result),
                      (unsigned int)PsFatFsStorage_ElapsedMs(chunk_start_tick));
        if ((fs_result != FR_OK) || (bytes_written != chunk_bytes)) {
            step_start_tick = PsFatFsStorage_GetTickNow();
            close_result = f_close(&file);
            PS_SAVEWR_LOG("[SAVEWR] close_after_write_fail fs=%s dt=%u ms\r\n",
                          PsFatFsStorage_StrError(close_result),
                          (unsigned int)PsFatFsStorage_ElapsedMs(step_start_tick));
            goto cleanup;
        }
        if (chunk_index == 1U) {
            xil_printf("[SAVEWR] first chunk ok wrote=%u\r\n", (unsigned int)bytes_written);
        }

        offset += bytes_written;
        PsFatFsStorage_YieldAfterWriteChunk();
    }

    step_start_tick = PsFatFsStorage_GetTickNow();
    xil_printf("[SAVEWR] sync begin\r\n");
    fs_result = f_sync(&file);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    PS_SAVEWR_LOG("[SAVEWR] sync fs=%s dt=%u ms\r\n",
                  PsFatFsStorage_StrError(fs_result),
                  (unsigned int)PsFatFsStorage_ElapsedMs(step_start_tick));
    if (fs_result != FR_OK) {
        step_start_tick = PsFatFsStorage_GetTickNow();
        close_result = f_close(&file);
        PS_SAVEWR_LOG("[SAVEWR] close_after_sync_fail fs=%s dt=%u ms\r\n",
                      PsFatFsStorage_StrError(close_result),
                      (unsigned int)PsFatFsStorage_ElapsedMs(step_start_tick));
        goto cleanup;
    }
    xil_printf("[SAVEWR] sync ok\r\n");

    step_start_tick = PsFatFsStorage_GetTickNow();
    xil_printf("[SAVEWR] close begin\r\n");
    close_result = f_close(&file);
    PS_SAVEWR_LOG("[SAVEWR] close fs=%s dt=%u ms\r\n",
                  PsFatFsStorage_StrError(close_result),
                  (unsigned int)PsFatFsStorage_ElapsedMs(step_start_tick));

    xil_printf("[SAVEWR] close done\r\n");

    if (result_out != NULL) {
        result_out->bytes_written = bytes_to_write;
        result_out->fs_result = FR_OK;
    }
    status = XST_SUCCESS;

cleanup:
    PS_SAVEWR_LOG("[SAVEWR] end status=%s fs=%s bytes=%u total=%u ms\r\n",
                  (status == XST_SUCCESS) ? "ok" : "fail",
                  (result_out != NULL) ? PsFatFsStorage_StrError(result_out->fs_result) : "na",
                  (unsigned int)offset,
                  (unsigned int)PsFatFsStorage_ElapsedMs(total_start_tick));
    PsFatFsStorage_Unlock();
    return status;
}

XStatus PsFatFsStorage_EnsureDirectory(const char *path) {
    FILINFO info;
    FRESULT fs_result;
    XStatus status;

    if ((path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    status = XST_FAILURE;
    PsFatFsStorage_Lock();

    fs_result = PsFatFsStorage_Mount();
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    memset(&info, 0, sizeof(info));
    fs_result = f_stat(path, &info);
    if (fs_result == FR_OK) {
        status = ((info.fattrib & AM_DIR) != 0U) ? XST_SUCCESS : XST_FAILURE;
        goto cleanup;
    }
    if ((fs_result != FR_NO_FILE) && (fs_result != FR_NO_PATH)) {
        goto cleanup;
    }

    fs_result = f_mkdir(path);
    if ((fs_result == FR_OK) || (fs_result == FR_EXIST)) {
        status = XST_SUCCESS;
        goto cleanup;
    }

cleanup:
    PsFatFsStorage_Unlock();
    return status;
}
