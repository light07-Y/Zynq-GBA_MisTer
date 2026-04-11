#ifndef PS_ROM_LOADER_H
#define PS_ROM_LOADER_H

#include "ff.h"
#include "xil_types.h"
#include "xstatus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 bytes_loaded;
    u32 bytes_aligned;
    u32 max_pak_addr;
    UINTPTR ddr_base_addr;
    FRESULT fs_result;
} PsRomLoadResult;

XStatus PsRomLoader_LoadSdFile(const char *path,
                               UINTPTR ddr_base_addr,
                               u32 ddr_capacity_bytes,
                               PsRomLoadResult *result_out);

const char *PsRomLoader_StrError(FRESULT result);

#ifdef __cplusplus
}
#endif

#endif
