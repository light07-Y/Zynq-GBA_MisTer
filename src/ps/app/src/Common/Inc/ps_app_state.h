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

typedef enum {
    PS_APP_BIOS_MODE_INTERNAL = 0U,
    PS_APP_BIOS_MODE_EXTERNAL = 1U,
    PS_APP_BIOS_MODE_FALLBACK = 2U
} PsAppBiosMode;

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
    u32 blit_count;
    u32 blit_last_us;
    u32 blit_max_us;
    u32 blit_total_us;
    u32 blit_seq_gap_max;
    u32 blit_seq_glitch_drop;
} PsAppVideoState;

typedef struct {
    u8 initialized;
    u8 irq_connected;
    u8 hpd_level;
    u8 present_enable;
    volatile u8 hpd_pending;
    u8 edid_valid;
    u8 edid_refresh_pending;
    u8 blank_frame_pending;
    u8 preferred_is_640x480p60;
    u8 preferred_timing_valid;
    u8 preferred_vic;
    u8 reserved0;
    u16 vendor_id;
    u16 product_code;
    u32 hpd_rise_count;
    u32 hpd_fall_count;
    u32 edid_read_ok_count;
    u32 edid_read_fail_count;
    u32 last_error;
    u32 last_service_tick;
    u32 last_hpd_change_tick;
    u8 edid_block0[128];
} PsAppHdmiLinkState;

typedef struct {
    u8 loaded;
    u8 is_loading;
    u8 sig_flash1m;
    u8 sig_flash;
    u8 sig_sram;
    u8 sig_eeprom;
    u8 quirk_remap;
    u8 quirk_sram_disable;
    u8 quirk_gpio;
    u8 quirk_tilt;
    u8 quirk_solar;
    u8 bios_mode;
    u8 bios_load_ok;
    u8 bios_external_present;
    u8 bios_reserved0;
    u32 size_bytes;
    u32 size_aligned;
    u32 bios_bytes_loaded;
    u32 flash1m_offset;
    u32 flash_offset;
    u32 sram_offset;
    u32 eeprom_offset;
    char game_code[5];
    char maker_code[3];
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
    u8 log_input_delta_enable;
    u8 log_fbscan_auto_enable;
    u32 delayed_chain_tick;
    u8 delayed_chain_printed;
    u32 fbscan_tick;
    u32 fbscan_dump_count;
    u32 fbscan_last_anomaly_tick;
} PsAppDiagState;

typedef struct {
    u8 enabled;
    u8 initialized;
    u8 irq_connected;
    u8 device_present;
    u8 xbox_interface_active;
    u8 bus_id;
    u8 interface_number;
    u8 port_speed;
    u16 vendor_id;
    u16 product_id;
    u8 interface_class;
    u8 interface_subclass;
    u8 interface_protocol;
    u8 ep_in_addr;
    u8 ep_out_addr;
    u8 reserved0;
    u32 irq_count;
    u32 event_count;
    u32 attach_count;
    u32 detach_count;
    u32 in_report_count;
    u32 in_report_error_count;
    u32 out_report_count;
    u32 out_report_error_count;
} PsAppUsbHostState;

typedef struct {
    u8 enabled;
    u8 active;
    u8 release_pending;
    u8 report_valid;
    u8 protocol_is_xinput;
    u8 output_capable;
    u8 reserved0;
    u8 reserved1;
    u16 vendor_id;
    u16 product_id;
    u8 interface_number;
    u8 interface_class;
    u8 interface_subclass;
    u8 interface_protocol;
    u8 ep_in_addr;
    u8 ep_out_addr;
    u8 ep_in_interval_ms;
    u8 ep_out_interval_ms;
    u16 buttons;
    u8 lt;
    u8 rt;
    s16 lx;
    s16 ly;
    s16 rx;
    s16 ry;
    u32 source_snapshot_mask;
    u32 last_logged_source_mask;
    u32 mapped_keys;
    u32 last_committed_keys;
    u32 raw_mapped_keys;
    u32 pending_mapped_keys;
    u32 debounced_nonstick_keys;
    u32 sampled_keys;
    u32 frame_press_keys;
    u32 report_count;
    u32 parse_error_count;
    u32 unsupported_report_count;
    u8 frame_sync_ready;
    u8 last_report_len;
    u16 reserved2;
    u8 reserved3;
    u32 frame_sync_token;
    u8 debounce_count[10];
    u8 short_pulse_ticks[10];
    u8 reserved4;
    u8 last_report[32];
} PsAppInputState;

#ifdef __cplusplus
}
#endif

#endif
