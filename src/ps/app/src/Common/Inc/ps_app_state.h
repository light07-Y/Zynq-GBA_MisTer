#ifndef PS_APP_STATE_H
#define PS_APP_STATE_H

#include "xil_types.h"

#include "Common/Inc/ps_project_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PS_APP_SAVE_KIND_NONE = 0U,
    PS_APP_SAVE_KIND_SRAM = 1U,
    PS_APP_SAVE_KIND_FLASH = 2U,
    PS_APP_SAVE_KIND_EEPROM = 3U
} PsAppSaveKind;

typedef struct {
    u32 ctrl;
    u32 keys;
    u32 max_pak_addr;
    u32 cycle_precalc;
    u32 rtc_timestamp;
    u32 irq_enable;
} PsAppShadowConfig;

typedef struct {
    u32 sample_rate_hz;
    u16 bits_per_sample;
    u8 mute;
    u8 volume;
} PsAppAudioState;

typedef struct {
    volatile u8 display_frame_idx;
    volatile u8 pending_frame_idx;
    u32 fbcap_last_frame_seq;
    u8 fbcap_last_buf_idx;
    u8 fbcap_last_frame_idx;
    volatile u32 vdma_irq_count;
    volatile u32 vdma_err_count;
    volatile u32 vdma_last_intr_mask;
    volatile u32 vdma_last_err_mask;
} PsAppVideoState;

typedef struct {
    u8 loaded;
    u8 is_loading;
    u32 size_bytes;
    u32 size_aligned;
    char path[PS_APP_ROM_PATH_MAX_CHARS];
} PsAppRomState;

typedef struct {
    u32 event_counters;
    u32 flush_count;
    u32 quiet_ticks;
    u32 last_bytes;
    u32 last_checksum;
    u8 active_kind;
    u8 dirty;
    u8 loaded_from_sd;
    u8 reserved0;
    char path[PS_APP_ROM_PATH_MAX_CHARS];
} PsAppSaveState;

typedef struct {
    u32 last_runtime_irq_sts;
    u32 last_physical_keys;
    u32 warn_print_count;
    u32 warn_suppress_ticks;
    u32 warn_last_err_latch;
    u32 warn_last_vdma_errs;
    u32 stall_last_pc;
    u32 stall_last_mem;
    u32 stall_last_dma;
    u32 stall_last_frame;
    u32 stall_same_sample_count;
    u8 auto_boot_audit_printed;
    u8 auto_stall_audit_printed;
    u32 delayed_chain_tick;
    u8 delayed_chain_printed;
    u32 fbscan_tick;
    u32 fbscan_dump_count;
    u32 fbscan_last_anomaly_tick;
} PsAppDiagState;

#ifdef __cplusplus
}
#endif

#endif
