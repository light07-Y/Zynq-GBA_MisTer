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

typedef enum {
    PS_APP_UI_MODE_MENU = 0U,
    PS_APP_UI_MODE_LOADING = 1U,
    PS_APP_UI_MODE_GAME = 2U
} PsAppUiMode;

typedef struct {
    u32 ctrl;
    u32 keys;
    u32 max_pak_addr;
    u32 cycle_precalc;
    u32 rtc_timestamp;
    u32 rtc_timestamp_saved;
    u32 irq_enable;
} PsAppShadowConfig;

typedef struct {
    u8 savestate_slot;
    u8 rewind_enabled;
    u8 rewind_active;
    u8 solar_level;
    s8 tilt_x;
    s8 tilt_y;
    u8 rumble_state;
    u8 cheats_enabled;
    u32 cheat_entry_count;
    u32 checkpoint_head;
    u32 checkpoint_last_ts;
    u32 rtc_last_tick_ts;
    u32 rtc_last_persist_ts;
    u32 rom_crc32;
    u32 input_prev_buttons;
    u8 input_prev_lt;
    u8 input_prev_rt;
    u8 pending_save;
    u8 pending_load;
    u16 reserved0;
    char rom_id[24];
} PsAppFeatureState;

typedef struct {
    u32 sample_rate_hz;
    u16 bits_per_sample;
    u8 mute;
    u8 volume;
} PsAppAudioState;

typedef enum {
    PS_APP_VIDEO_INTERFRAME_OFF = 0U,
    PS_APP_VIDEO_INTERFRAME_BLEND = 1U,
    PS_APP_VIDEO_INTERFRAME_30HZ = 2U
} PsAppVideoInterframeMode;

typedef struct {
    u8 interframe_mode;
    u8 shade_mode;
    u8 non_eq_hd2x_hint;
    u8 non_eq_maxpixels_hint;
    u8 frame30_phase;
    u8 prev_capture_valid;
    u8 reserved0;
    u8 reserved1;
} PsAppVideoFxConfig;

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
    PsAppVideoFxConfig fx;
} PsAppVideoState;

typedef enum {
    PS_APP_STATE_IO_IDLE = 0U,
    PS_APP_STATE_IO_SAVE_WAIT_BUSY = 1U,
    PS_APP_STATE_IO_SAVE_WAIT_DONE = 2U,
    PS_APP_STATE_IO_LOAD_WAIT_BUSY = 3U,
    PS_APP_STATE_IO_LOAD_WAIT_DONE = 4U
} PsAppStateIoPhase;

typedef struct {
    u8 slot;
    u8 rewind_enable;
    u8 rewind_active;
    u8 cheats_enabled;
    u8 save_pending;
    u8 load_pending;
    u8 cheat_push_pending;
    u8 cheat_clear_pending;
    u8 io_phase;
    u8 io_busy_seen;
    u8 io_last_result;
    u8 reserved0;
    u32 feature_status;
    u32 io_wait_ticks;
    u32 io_timeout_ticks;
    u32 save_file_count;
    u32 load_file_count;
    u32 io_error_count;
    u32 cheat_words[4];
    char last_state_path[PS_APP_ROM_PATH_MAX_CHARS];
} PsAppStateFeatureConfig;

typedef struct {
    u8 model_ready;
    u8 uncert_infinite;
    u8 has_last_cal;
    u8 reserved0;
    u32 last_current_unix;
    u32 last_uncert_us;
    s32 ppm_cal_ppb;
    u32 ppm_sigma_ppb;
    u64 anchor_met_us;
    u64 anchor_utc_us;
    u64 last_cal_met_us;
    u64 last_cal_utc_us;
    u64 base_uncert_us;
    u32 last_saved_unix;
    u64 last_saved_time;
    char path[PS_APP_ROM_PATH_MAX_CHARS];
} PsAppRtcPersistState;

typedef struct {
    u8 solar;
    s8 tilt_x;
    s8 tilt_y;
    u8 rumble_enabled;
    u8 rumble_active;
    u8 rumble_strength;
    u8 rumble_dirty;
    u8 reserved0;
    u32 rumble_change_count;
    u32 sensor_update_count;
} PsAppSensorState;

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
    /* ROM 切换门控位：
     * 1 表示正在装载/切换 ROM。后台周期任务（保存、RTC 持久化等）应暂停，
     * 避免与主加载链路争用 FatFs 锁导致“切 ROM 卡死”。 */
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
    /* 来自 GBA_REG_SAVE_STATUS 的原始计数快照（8bit x 3 通道）：
     * [7:0] SRAM, [15:8] FLASH, [23:16] EEPROM。
     * 这里保存“上一次看到的计数值”，用于检测哪一类 save 发生新写入事件。 */
    u32 event_counters;
    /* 成功落盘次数，便于现场确认“确实写过 SD”。 */
    u32 flush_count;
    /* dirty 后累计静默时长；到 quiet 阈值会触发一次写盘。 */
    u32 quiet_ticks;
    /* dirty 后累计总时长；用于 hard 阈值兜底，防止持续小写入导致永不落盘。 */
    u32 dirty_age_ticks;
    u32 last_bytes;
    u32 last_checksum;
    /* 当前判定脏数据所属类型（SRAM/FLASH/EEPROM）。 */
    u8 active_kind;
    /* 1 表示有待落盘的变更。 */
    u8 dirty;
    /* 最近一次 PrepareForRom 是否从 SD 成功加载到窗口。 */
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
    u8 log_ddr_enable;
    u8 reserved0;
    u32 delayed_chain_tick;
    u8 delayed_chain_printed;
    u32 fbscan_tick;
    u32 fbscan_dump_count;
    u32 fbscan_last_anomaly_tick;
    u32 ddrlog_interval_ticks;
    u32 ddrlog_tick;
    u32 ddrlog_last_status1;
    u32 ddrlog_last_pc;
    u32 ddrlog_last_mem;
    u32 ddrlog_last_dma;
    u32 ddrlog_last_err;
    u32 ddrlog_last_counts0;
    u32 ddrlog_last_counts1;
    u32 ddrlog_same_sample_count;
    u8 pc_guard_enable;
    u8 pc_guard_recovery_active;
    u8 pc_guard_audio_restore_pending;
    u8 pc_guard_reserved0;
    u32 pc_guard_invalid_streak;
    u32 pc_guard_cooldown_ticks;
    u32 pc_guard_reset_hold_ticks;
    u32 pc_guard_recovery_count;
    u32 pc_guard_tick;
    u32 pc_guard_last_bad_pc;
    u32 pc_guard_last_bad_status1;
    u32 pc_guard_last_bad_mem;
    u32 pc_guard_last_bad_dma;
    u32 pc_guard_last_trigger_tick;
    u8  rom_ddr_safe_recovery_count;
    u32 rom_ddr_safe_recovery_tick;
    u8  rom_ddr_safe_recovery_active;
    u8  reserved1;
    u32 miss_high_sample_count;
    u32 miss_last_cycles;
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

typedef struct {
    u8 mode;
    u8 selected_index;
    u8 game_count;
    u8 refresh_requested;
    u8 launch_requested;
    u8 launch_index;
    u8 lt_exit_latched;
    u8 board_exit_latched;
    u8 reserved0;
    u8 reserved1;
    u32 lt_hold_ms;
    u32 board_exit_hold_ms;
    u32 last_buttons;
    char launch_path[PS_APP_ROM_PATH_MAX_CHARS];
    char status_line[PS_APP_UI_GAME_NAME_MAX_CHARS];
} PsAppUiState;

#ifdef __cplusplus
}
#endif

#endif
