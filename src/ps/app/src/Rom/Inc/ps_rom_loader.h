#ifndef PS_ROM_LOADER_H
#define PS_ROM_LOADER_H

#include "xil_types.h"
#include "xstatus.h"

#include "Storage/Inc/ps_fatfs_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 bytes_loaded;
    u32 bytes_aligned;
    u32 max_pak_addr;
    u32 flash1m_offset;
    u32 flash_offset;
    u32 sram_offset;
    u32 eeprom_offset;
    char game_code[5];
    char maker_code[3];
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
