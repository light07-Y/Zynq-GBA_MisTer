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

typedef struct {
    u32 bytes_written;
    UINTPTR src_addr;
    FRESULT fs_result;
} PsFatFsStorageWriteResult;

XStatus PsFatFsStorage_ReadFileToMemory(const char *path,
                                       UINTPTR dst_addr,
                                       u32 capacity_bytes,
                                       PsFatFsStorageReadResult *result_out);

XStatus PsFatFsStorage_WriteMemoryToFile(const char *path,
                                        UINTPTR src_addr,
                                        u32 bytes_to_write,
                                        PsFatFsStorageWriteResult *result_out);

XStatus PsFatFsStorage_EnsureDirectory(const char *path);

const char *PsFatFsStorage_StrError(FRESULT result);

#ifdef __cplusplus
}
#endif

#endif
