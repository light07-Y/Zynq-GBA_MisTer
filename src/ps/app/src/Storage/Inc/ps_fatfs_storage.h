#ifndef PS_FATFS_STORAGE_H
#define PS_FATFS_STORAGE_H

#include "ff.h"
#include "xil_types.h"
#include "xstatus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 bytes_loaded;
    UINTPTR dst_addr;
    FRESULT fs_result;
} PsFatFsStorageReadResult;

typedef void (*PsFatFsStorageReadChunkCallback)(const u8 *chunk_ptr,
                                                u32 chunk_offset,
                                                u32 chunk_bytes,
                                                void *user_ctx);

typedef struct {
    u32 bytes_written;
    UINTPTR src_addr;
    FRESULT fs_result;
} PsFatFsStorageWriteResult;

XStatus PsFatFsStorage_ReadFileToMemory(const char *path,
                                       UINTPTR dst_addr,
                                       u32 capacity_bytes,
                                       PsFatFsStorageReadResult *result_out);
XStatus PsFatFsStorage_ReadFileToMemoryEx(const char *path,
                                         UINTPTR dst_addr,
                                         u32 capacity_bytes,
                                         PsFatFsStorageReadResult *result_out,
                                         PsFatFsStorageReadChunkCallback chunk_callback,
                                         void *user_ctx);

XStatus PsFatFsStorage_WriteMemoryToFile(const char *path,
                                        UINTPTR src_addr,
                                        u32 bytes_to_write,
                                        PsFatFsStorageWriteResult *result_out);

XStatus PsFatFsStorage_QueryFileSize(const char *path,
                                     u32 *size_bytes_out,
                                     FRESULT *fs_result_out);

XStatus PsFatFsStorage_EnsureDirectory(const char *path);

const char *PsFatFsStorage_StrError(FRESULT result);

#ifdef __cplusplus
}
#endif

#endif
