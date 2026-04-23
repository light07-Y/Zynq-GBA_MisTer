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
/* 不允许无限期等待存储锁：
 * ROM 切换、RTC 持久化、save 周期落盘会并发触发 FatFs 访问。
 * 若某条路径异常卡住，有限超时可以避免整个 PS 运行时被“连坐”死锁。 */
#define PS_FATFS_STORAGE_LOCK_TIMEOUT_MS   3000U
#define PS_FATFS_STORAGE_SAVEWR_VERBOSE    0U

#if PS_FATFS_STORAGE_SAVEWR_VERBOSE
#define PS_SAVEWR_LOG(...) xil_printf(__VA_ARGS__)
#else
#define PS_SAVEWR_LOG(...) do { } while (0)
#endif

/* 模块内统一采用“单 FATFS 实例 + 全局互斥锁”模型：
 * 所有直接 FatFs 调用都必须放在这把锁内，避免跨任务重入导致不可预测行为。 */
static FATFS s_ps_fatfs_storage_fs;
static SemaphoreHandle_t s_ps_fatfs_storage_lock;
static u8 s_ps_fatfs_list_cfg_logged;

static SemaphoreHandle_t PsFatFsStorage_GetLock(void) {
    SemaphoreHandle_t lock;

    lock = s_ps_fatfs_storage_lock;
    if (lock != NULL) {
        return lock;
    }

    lock = xSemaphoreCreateMutex();
    if (lock == NULL) {
        return NULL;
    }

    if (s_ps_fatfs_storage_lock == NULL) {
        s_ps_fatfs_storage_lock = lock;
        return s_ps_fatfs_storage_lock;
    }

    (void)vSemaphoreDelete(lock);
    return s_ps_fatfs_storage_lock;
}

static XStatus PsFatFsStorage_Lock(void) {
    SemaphoreHandle_t lock;
    TickType_t wait_ticks;

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return XST_SUCCESS;
    }

    lock = PsFatFsStorage_GetLock();
    if (lock == NULL) {
        return XST_FAILURE;
    }

    /* 使用有限等待而非 portMAX_DELAY：
     * 超时按“可恢复失败”返回给上层，由上层决定重试/降级，而不是整机硬挂。 */
    wait_ticks = pdMS_TO_TICKS(PS_FATFS_STORAGE_LOCK_TIMEOUT_MS);
    if (wait_ticks == 0U) {
        wait_ticks = 1U;
    }

    PS_SAVEWR_LOG("[SAVEWR] lock wait\r\n");
    if (xSemaphoreTake(lock, wait_ticks) != pdTRUE) {
        xil_printf("[FATFS] lock timeout after %u ms\r\n",
                   (unsigned int)PS_FATFS_STORAGE_LOCK_TIMEOUT_MS);
        return XST_FAILURE;
    }
    PS_SAVEWR_LOG("[SAVEWR] lock ok\r\n");
    return XST_SUCCESS;
}

static void PsFatFsStorage_Unlock(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return;
    }

    if (s_ps_fatfs_storage_lock != NULL) {
        (void)xSemaphoreGive(s_ps_fatfs_storage_lock);
    }
}

static void PsFatFsStorage_YieldAfterWriteChunk(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        taskYIELD();
    }
}

static FRESULT PsFatFsStorage_Mount(void) {
    return f_mount(&s_ps_fatfs_storage_fs, PS_FATFS_STORAGE_DRIVE_PATH, 0);
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
    /* 如果全局存储锁超时，立即失败返回：
     * 让调用方决定重试策略，避免后台线程永久阻塞。 */
    if (PsFatFsStorage_Lock() != XST_SUCCESS) {
        if (result_out != NULL) {
            result_out->fs_result = FR_TIMEOUT;
        }
        return XST_FAILURE;
    }

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
    UINT bytes_written;
    const u8 *src_ptr;
    u32 offset;
    u32 chunk_index;
    XStatus status;

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
    memset(&file, 0, sizeof(file));
    PS_SAVEWR_LOG("[SAVEWR] begin path=%s bytes=%u chunk=%u\r\n",
                  path,
                  (unsigned int)bytes_to_write,
                  (unsigned int)PS_FATFS_STORAGE_WRITE_CHUNK_BYTES);
    /* 写路径与读路径采用同一套有界锁策略，保证行为一致。 */
    if (PsFatFsStorage_Lock() != XST_SUCCESS) {
        if (result_out != NULL) {
            result_out->fs_result = FR_TIMEOUT;
        }
        return XST_FAILURE;
    }

    PS_SAVEWR_LOG("[SAVEWR] mount begin\r\n");
    fs_result = PsFatFsStorage_Mount();
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    PS_SAVEWR_LOG("[SAVEWR] mount fs=%s\r\n",
                  PsFatFsStorage_StrError(fs_result));
    if (fs_result != FR_OK) {
        goto cleanup;
    }
    PS_SAVEWR_LOG("[SAVEWR] mount ok\r\n");

    PS_SAVEWR_LOG("[SAVEWR] open begin\r\n");
    fs_result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    PS_SAVEWR_LOG("[SAVEWR] open fs=%s\r\n",
                  PsFatFsStorage_StrError(fs_result));
    if (fs_result != FR_OK) {
        goto cleanup;
    }
    PS_SAVEWR_LOG("[SAVEWR] open ok\r\n");

    Xil_DCacheFlushRange((INTPTR)src_addr, bytes_to_write);
    PS_SAVEWR_LOG("[SAVEWR] dcache flush ok\r\n");
    PS_SAVEWR_LOG("[SAVEWR] dcache flush bytes=%u\r\n",
                  (unsigned int)bytes_to_write);

    while (offset < bytes_to_write) {
        u32 chunk_bytes;

        chunk_bytes = bytes_to_write - offset;
        if (chunk_bytes > PS_FATFS_STORAGE_WRITE_CHUNK_BYTES) {
            chunk_bytes = PS_FATFS_STORAGE_WRITE_CHUNK_BYTES;
        }

        bytes_written = 0U;
        if (chunk_index == 0U) {
            PS_SAVEWR_LOG("[SAVEWR] first chunk begin off=%u size=%u\r\n",
                          (unsigned int)offset,
                          (unsigned int)chunk_bytes);
        }
        fs_result = f_write(&file, &src_ptr[offset], chunk_bytes, &bytes_written);
        if (result_out != NULL) {
            result_out->fs_result = fs_result;
        }
        chunk_index++;
        PS_SAVEWR_LOG("[SAVEWR] chunk %u off=%u req=%u wrote=%u fs=%s\r\n",
                      (unsigned int)chunk_index,
                      (unsigned int)offset,
                      (unsigned int)chunk_bytes,
                      (unsigned int)bytes_written,
                      PsFatFsStorage_StrError(fs_result));
        if ((fs_result != FR_OK) || (bytes_written != chunk_bytes)) {
            (void)f_close(&file);
            goto cleanup;
        }
        if (chunk_index == 1U) {
            PS_SAVEWR_LOG("[SAVEWR] first chunk ok wrote=%u\r\n", (unsigned int)bytes_written);
        }

        offset += bytes_written;
        PsFatFsStorage_YieldAfterWriteChunk();
    }

    PS_SAVEWR_LOG("[SAVEWR] sync begin\r\n");
    fs_result = f_sync(&file);
    if (result_out != NULL) {
        result_out->fs_result = fs_result;
    }
    PS_SAVEWR_LOG("[SAVEWR] sync fs=%s\r\n",
                  PsFatFsStorage_StrError(fs_result));
    if (fs_result != FR_OK) {
        (void)f_close(&file);
        goto cleanup;
    }
    PS_SAVEWR_LOG("[SAVEWR] sync ok\r\n");

    PS_SAVEWR_LOG("[SAVEWR] close begin\r\n");
    fs_result = f_close(&file);
    PS_SAVEWR_LOG("[SAVEWR] close fs=%s\r\n",
                  PsFatFsStorage_StrError(fs_result));

    PS_SAVEWR_LOG("[SAVEWR] close done\r\n");

    if (result_out != NULL) {
        result_out->bytes_written = bytes_to_write;
        result_out->fs_result = FR_OK;
    }
    status = XST_SUCCESS;

cleanup:
    PS_SAVEWR_LOG("[SAVEWR] end status=%s fs=%s bytes=%u\r\n",
                  (status == XST_SUCCESS) ? "ok" : "fail",
                  (result_out != NULL) ? PsFatFsStorage_StrError(result_out->fs_result) : "na",
                  (unsigned int)offset);
    PsFatFsStorage_Unlock();
    return status;
}

XStatus PsFatFsStorage_DeleteFileIfExists(const char *path, u8 *deleted_out) {
    FRESULT fs_result;
    XStatus status;

    if (deleted_out != NULL) {
        *deleted_out = 0U;
    }
    if ((path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    if (PsFatFsStorage_Lock() != XST_SUCCESS) {
        return XST_FAILURE;
    }

    status = XST_FAILURE;
    fs_result = PsFatFsStorage_Mount();
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    /* 刻意不做 f_stat 预检查：
     * 1) f_unlink 本身就能给出“文件不存在/路径不存在”的结果；
     * 2) 少一次 FatFs API 交互，缩短临界区；
     * 3) 历史上在高压并发路径里，f_stat 额外分支更容易放大异常。 */
    fs_result = f_unlink(path);
    if ((fs_result == FR_NO_FILE) || (fs_result == FR_NO_PATH)) {
        status = XST_SUCCESS;
        goto cleanup;
    }
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    if (deleted_out != NULL) {
        *deleted_out = 1U;
    }
    status = XST_SUCCESS;

cleanup:
    PsFatFsStorage_Unlock();
    return status;
}

XStatus PsFatFsStorage_EnsureDirectory(const char *path) {
    DIR dir;
    FRESULT fs_result;
    XStatus status;

    if ((path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    status = XST_FAILURE;
    if (PsFatFsStorage_Lock() != XST_SUCCESS) {
        return XST_FAILURE;
    }

    fs_result = PsFatFsStorage_Mount();
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    /* 目录存在性同样不走 f_stat：
     * 直接 f_opendir 检查最简路径，必要时再 mkdir。 */
    memset(&dir, 0, sizeof(dir));
    fs_result = f_opendir(&dir, path);
    if (fs_result == FR_OK) {
        (void)f_closedir(&dir);
        status = XST_SUCCESS;
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

XStatus PsFatFsStorage_ListDirectory(const char *path,
                                     PsFatFsStorageListEntryCallback callback,
                                     void *user_ctx) {
    DIR dir;
    FILINFO info;
    FRESULT fs_result;
    XStatus status;
    size_t base_len;
    u8 need_separator;
    u32 visited_count;

    if ((path == NULL) || (*path == '\0')) {
        return XST_INVALID_PARAM;
    }

    status = XST_FAILURE;
    visited_count = 0U;

    if (s_ps_fatfs_list_cfg_logged == 0U) {
        s_ps_fatfs_list_cfg_logged = 1U;
        /* 启动后首次扫描打印一次 FatFs 关键配置，便于现场定位：
         * - lfn: 是否启用长文件名
         * - cp:  代码页
         * - FILINFO/DIR 大小：可快速判断编译配置是否一致 */
        xil_printf("[FATFS] list cfg lfn=%d cp=%d filinfo=%u dir=%u\r\n",
                   (int)FF_USE_LFN,
                   (int)FF_CODE_PAGE,
                   (unsigned int)sizeof(FILINFO),
                   (unsigned int)sizeof(DIR));
    }

    /* 目录遍历和读写共用同一把全局存储锁，确保 FatFs 单入口访问。 */
    if (PsFatFsStorage_Lock() != XST_SUCCESS) {
        return XST_FAILURE;
    }

    fs_result = PsFatFsStorage_Mount();
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    memset(&dir, 0, sizeof(dir));
    fs_result = f_opendir(&dir, path);
    if (fs_result != FR_OK) {
        goto cleanup;
    }

    base_len = strlen(path);
    need_separator = (u8)((base_len > 0U) &&
                          (path[base_len - 1U] != '/') &&
                          (path[base_len - 1U] != '\\') ? 1U : 0U);

    for (;;) {
        char full_path[320];
        size_t name_len;
        size_t full_len;
        u8 is_dir;

        memset(&info, 0, sizeof(info));
        fs_result = f_readdir(&dir, &info);
        if (fs_result != FR_OK) {
            if (visited_count > 0U) {
                /* 这里最常见的是 FR_INVALID_OBJECT：
                 * 说明目录对象在遍历过程中失效（例如底层状态变化）。
                 * 日志保留“已读取条目数”，方便区分“全失败”还是“中途失败”。 */
                xil_printf("[FATFS] list warn: %s (%u entries kept)\r\n",
                           PsFatFsStorage_StrError(fs_result),
                           (unsigned int)visited_count);
            }
            /* 返回失败让上层感知“扫描不完整”，由 UI 决定显示“部分可用”。 */
            status = XST_FAILURE;
            break;
        }

        if (info.fname[0] == '\0') {
            status = XST_SUCCESS;
            break;
        }

        /* 跳过伪目录项。 */
        if ((strcmp(info.fname, ".") == 0) || (strcmp(info.fname, "..") == 0)) {
            continue;
        }
        visited_count++;

        name_len = strlen(info.fname);
        full_len = base_len + (size_t)need_separator + name_len;
        /* 超过缓冲上限则跳过该项，避免路径拼接溢出。 */
        if (full_len >= sizeof(full_path)) {
            continue;
        }

        memcpy(full_path, path, base_len);
        if (need_separator != 0U) {
            full_path[base_len] = '/';
            memcpy(&full_path[base_len + 1U], info.fname, name_len);
            full_path[base_len + 1U + name_len] = '\0';
        } else {
            memcpy(&full_path[base_len], info.fname, name_len);
            full_path[base_len + name_len] = '\0';
        }

        is_dir = (u8)(((info.fattrib & AM_DIR) != 0U) ? 1U : 0U);
        if (callback != NULL) {
            /* 回调返回 0 表示上层主动停止遍历（如达到上限）。 */
            if (callback(info.fname, full_path, is_dir, user_ctx) == 0U) {
                status = XST_SUCCESS;
                break;
            }
        }
    }

    (void)f_closedir(&dir);

cleanup:
    PsFatFsStorage_Unlock();
    return status;
}

