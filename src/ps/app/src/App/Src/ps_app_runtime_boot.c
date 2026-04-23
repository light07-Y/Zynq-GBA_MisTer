#include "App/Inc/ps_app_runtime.h"

#include <string.h>

#include "xil_io.h"
#include "xparameters.h"
#include "xuartps.h"
#include "xil_printf.h"

#include "Audio/Inc/ps_audio_codec.h"
#include "Diagnostics/Inc/ps_app_diag.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Input/Inc/ps_app_input.h"
#include "Save/Inc/ps_app_save.h"
#include "Video/Inc/ps_app_video.h"
#include "Hdmi/Inc/ps_app_hdmi_link.h"
#include "UsbHost/Inc/ps_app_usbhost.h"

/*
 * 这里显式保留 MIO50/MIO51 的 SLCR 地址，是为了在应用启动阶段兜底覆盖
 * 旧 platform/ps7_init 残留的错误配置。实测错误配置会把 BTN4/BTN5 钉死，
 * 导致 raw50/raw51 始终不变，UART 侧也看不到按键边沿。
 */
#define PS_APP_SLCR_LOCK_ADDR          0xF8000004U
#define PS_APP_SLCR_UNLOCK_ADDR        0xF8000008U
#define PS_APP_SLCR_LOCK_CODE          0x0000767BU
#define PS_APP_SLCR_UNLOCK_CODE        0x0000DF0DU
#define PS_APP_MIO50_CFG_ADDR          0xF80007C8U
#define PS_APP_MIO51_CFG_ADDR          0xF80007CCU
#define PS_APP_MIO_GPIO_INPUT_CFG      0x00000200U
/* UI 音量参数（百分比域）：0..100，按键步进 5，默认开机 45。 */
#define PS_APP_AUDIO_VOLUME_MAX        100U
#define PS_APP_AUDIO_VOLUME_STEP       5U
#define PS_APP_AUDIO_DEFAULT_VOLUME    45U
/* SSM2603 音量寄存器边界：0x30=-73 dB，0x79=0 dB(unity gain)。 */
#define PS_APP_AUDIO_CODEC_MIN_DB_VOL  0x30U
#define PS_APP_AUDIO_CODEC_UNITY_VOL   0x79U


static void PsAppRuntime_PrintPsButtons(u32 ps_btn_mask) {
    xil_printf("[BTN] ps BTN4=%u BTN5=%u mask=0x%02x\r\n",
               (unsigned int)((ps_btn_mask & PS_APP_BTN4_MASK) != 0U),
               (unsigned int)((ps_btn_mask & PS_APP_BTN5_MASK) != 0U),
               (unsigned int)(ps_btn_mask & (PS_APP_BTN4_MASK | PS_APP_BTN5_MASK)));
}

static u8 PsAppRuntime_ClampAudioVolumePercent(u32 volume_percent) {
    /* 所有外部入口统一钳制到 0..100，避免异常值进入 codec 映射。 */
    if (volume_percent > PS_APP_AUDIO_VOLUME_MAX) {
        return (u8)PS_APP_AUDIO_VOLUME_MAX;
    }
    return (u8)volume_percent;
}

static u8 PsAppRuntime_EncodeCodecVolume(u8 volume_percent) {
    u32 codec_range;

    if (volume_percent == 0U) {
        return PS_APP_AUDIO_CODEC_MIN_DB_VOL;
    }

    /*
     * SSM2603 (Rev.D) R2/R3 音量控制定义：
     * - 0x30 = -73 dB
     * - 0x79 = 0 dB (默认)
     * - 0x7F = +6 dB
     *
     * 行业常见做法是把 UI 的 100% 定义为 0 dB（unity gain），
     * 只在“增益/Boost”场景才进入 >0 dB 区间，避免默认档位过驱和失真风险。
     * 因此这里将 1..100 映射到 [-73 dB, 0 dB] => [0x30, 0x79]。
     */
    codec_range = (u32)PS_APP_AUDIO_CODEC_UNITY_VOL - (u32)PS_APP_AUDIO_CODEC_MIN_DB_VOL;
    return (u8)((u32)PS_APP_AUDIO_CODEC_MIN_DB_VOL +
                (((u32)volume_percent * codec_range) + (PS_APP_AUDIO_VOLUME_MAX / 2U)) /
                    PS_APP_AUDIO_VOLUME_MAX);
}

/*
 * Zybo Z7-20 上这两个 PS 按键最终应工作在 0x00000200 配置下。
 * 如果启动后仍保留旧的 0x00001201，按钮输入会被内部上拉扰乱，
 * 所以这里在 XGpioPs 初始化完成后再强制写一次，确保应用层读到真实电平。
 */
static void PsAppRuntime_ForcePsButtonMioConfig(void) {
    Xil_Out32(PS_APP_SLCR_UNLOCK_ADDR, PS_APP_SLCR_UNLOCK_CODE);
    Xil_Out32(PS_APP_MIO50_CFG_ADDR, PS_APP_MIO_GPIO_INPUT_CFG);
    Xil_Out32(PS_APP_MIO51_CFG_ADDR, PS_APP_MIO_GPIO_INPUT_CFG);
    Xil_Out32(PS_APP_SLCR_LOCK_ADDR, PS_APP_SLCR_LOCK_CODE);
}

/*
 * 这个调试接口保留 raw50/raw51、bank1、dir1、outen1 和 mio50/mio51，
 * 是为了以后再次遇到“按键没反应”时，能够一步区分：
 * 1) 应用逻辑问题；
 * 2) GPIO 方向/输出使能问题；
 * 3) MIO/SLCR 配置残留问题。
 */
void PsAppRuntime_ReadPsButtonRawLevels(PsAppRuntimeContext *ctx,
                                        u32 *btn4_level,
                                        u32 *btn5_level,
                                        u32 *bank1_data,
                                        u32 *bank1_dir,
                                        u32 *bank1_outen,
                                        u32 *mio50_cfg,
                                        u32 *mio51_cfg) {
    if (btn4_level != NULL) {
        *btn4_level = 0U;
    }
    if (btn5_level != NULL) {
        *btn5_level = 0U;
    }
    if (bank1_data != NULL) {
        *bank1_data = 0U;
    }
    if (bank1_dir != NULL) {
        *bank1_dir = 0U;
    }
    if (bank1_outen != NULL) {
        *bank1_outen = 0U;
    }
    if (mio50_cfg != NULL) {
        *mio50_cfg = 0U;
    }
    if (mio51_cfg != NULL) {
        *mio51_cfg = 0U;
    }

    if ((ctx == NULL) || (ctx->ps_gpio == NULL) || (ctx->ps_gpio_ready == 0U)) {
        return;
    }

#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    if (btn4_level != NULL) {
        *btn4_level = XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN);
    }
    if (btn5_level != NULL) {
        *btn5_level = XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN);
    }
    if (bank1_data != NULL) {
        *bank1_data = XGpioPs_Read(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (bank1_dir != NULL) {
        *bank1_dir = XGpioPs_GetDirection(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (bank1_outen != NULL) {
        *bank1_outen = XGpioPs_GetOutputEnable(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (mio50_cfg != NULL) {
        *mio50_cfg = Xil_In32(PS_APP_MIO50_CFG_ADDR);
    }
    if (mio51_cfg != NULL) {
        *mio51_cfg = Xil_In32(PS_APP_MIO51_CFG_ADDR);
    }
#endif
}

u32 PsAppRuntime_ReadPsButtonMask(PsAppRuntimeContext *ctx) {
    u32 ps_btn_mask;

    if ((ctx == NULL) || (ctx->ps_gpio == NULL) || (ctx->ps_gpio_ready == 0U)) {
        return 0U;
    }

    ps_btn_mask = 0U;
#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    /*
     * 当前板上的实际行为是：空闲为低电平，按下为高电平。
     * 因此这里按 active-high 解释 BTN4/BTN5，和 raw50/raw51 的诊断输出保持一致。
     */
    if (XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN) != 0U) {
        ps_btn_mask |= PS_APP_BTN4_MASK;
    }
    if (XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN) != 0U) {
        ps_btn_mask |= PS_APP_BTN5_MASK;
    }
#endif
    return ps_btn_mask;
}

XStatus PsAppRuntime_ApplyAudioOutputState(PsAppRuntimeContext *ctx) {
    XStatus status;
    u8 codec_volume;
    u8 effective_mute;

    if ((ctx == NULL) || (ctx->audio == NULL)) {
        return XST_FAILURE;
    }

    ctx->audio->volume = PsAppRuntime_ClampAudioVolumePercent(ctx->audio->volume);
    /* codec 尚未就绪时仅保留状态，不报错，后续 ProgramAudio 会重新下发。 */
    if ((ctx->codec == NULL) || (ctx->codec->is_ready == 0U)) {
        return XST_SUCCESS;
    }

    /* 先下发音量，再根据 mute/volume=0 统一决定静音位。 */
    codec_volume = PsAppRuntime_EncodeCodecVolume(ctx->audio->volume);
    status = PsAudioCodec_SetHeadphoneVolume(ctx->codec, codec_volume);
    if (status != XST_SUCCESS) {
        return status;
    }

    effective_mute = ((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)) ? 1U : 0U;
    return PsAudioCodec_SetMute(ctx->codec, effective_mute);
}

XStatus PsAppRuntime_SetAudioVolume(PsAppRuntimeContext *ctx, u32 volume_percent) {
    XStatus status;

    if ((ctx == NULL) || (ctx->audio == NULL)) {
        return XST_FAILURE;
    }

    ctx->audio->volume = PsAppRuntime_ClampAudioVolumePercent(volume_percent);
    status = PsAppRuntime_ApplyAudioOutputState(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XST_SUCCESS;
}

XStatus PsAppRuntime_InitPsGpio(PsAppRuntimeContext *ctx) {
    XStatus status;
#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    XGpioPs_Config *cfg;
#endif

    if ((ctx == NULL) || (ctx->ps_gpio == NULL)) {
        return XST_FAILURE;
    }

    ctx->ps_gpio_ready = 0U;
    ctx->ps_btn_last_mask = 0U;

#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
#if defined(SDT)
    cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_BASEADDR);
#else
    cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_DEVICE_ID);
#endif
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XGpioPs_CfgInitialize(ctx->ps_gpio, cfg, cfg->BaseAddr);
    if (status != XST_SUCCESS) {
        return status;
    }

    PsAppRuntime_ForcePsButtonMioConfig();

    XGpioPs_SetDirectionPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN, 0U);
    XGpioPs_SetOutputEnablePin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN, 0U);
    XGpioPs_SetDirectionPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN, 0U);
    XGpioPs_SetOutputEnablePin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN, 0U);

    ctx->ps_gpio_ready = 1U;
    ctx->ps_btn_last_mask = PsAppRuntime_ReadPsButtonMask(ctx);
    return XST_SUCCESS;
#else
    (void)status;
    xil_printf("[INIT] 8 warning: BSP has no XGPIOPS instance, regenerate platform from updated XSA\r\n");
    return XST_SUCCESS;
#endif
}

static void PsAppRuntime_InitDefaults(PsAppRuntimeContext *ctx) {
    ctx->config->ctrl =
        (PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL) | PS_APP_GBA_CTRL_BOOT_REQUIRED) &
        ~GBA_CTRL_CORE_ON;
    ctx->config->keys = 0U;
    ctx->config->max_pak_addr = 0U;
    ctx->config->cycle_precalc = 100U;
    ctx->config->rtc_timestamp = 0U;
    ctx->config->irq_enable = PS_APP_IRQ_MASK_DEFAULT;

    ctx->audio->sample_rate_hz = 48000U;
    ctx->audio->bits_per_sample = 16U;
    ctx->audio->mute = 0U;
    /* 默认音量：开机即为用户可接受的中档位。 */
    ctx->audio->volume = (u8)PS_APP_AUDIO_DEFAULT_VOLUME;

    ctx->video->display_frame_idx = 0xFFU;
    ctx->video->pending_frame_idx = 0xFFU;
    ctx->video->fbcap_last_frame_seq = 0U;
    ctx->video->fbcap_last_buf_idx = 0xFFU;
    ctx->video->fbcap_last_frame_idx = 0xFFU;
    ctx->video->blit_count = 0U;
    ctx->video->blit_last_us = 0U;
    ctx->video->blit_max_us = 0U;
    ctx->video->blit_total_us = 0U;
    ctx->video->blit_seq_gap_max = 0U;
    ctx->video->blit_seq_glitch_drop = 0U;

    ctx->rom->loaded = 0U;
    ctx->rom->is_loading = 0U;
    ctx->rom->sig_flash1m = 0U;
    ctx->rom->sig_flash = 0U;
    ctx->rom->sig_sram = 0U;
    ctx->rom->sig_eeprom = 0U;
    ctx->rom->quirk_remap = 0U;
    ctx->rom->quirk_sram_disable = 0U;
    ctx->rom->quirk_gpio = 0U;
    ctx->rom->quirk_tilt = 0U;
    ctx->rom->quirk_solar = 0U;
    ctx->rom->bios_mode = (u8)PS_APP_BIOS_MODE_INTERNAL;
    ctx->rom->bios_load_ok = 0U;
    ctx->rom->bios_external_present = 0U;
    ctx->rom->bios_bytes_loaded = 0U;
    ctx->rom->size_bytes = 0U;
    ctx->rom->size_aligned = 0U;
    ctx->rom->flash1m_offset = 0xFFFFFFFFU;
    ctx->rom->flash_offset = 0xFFFFFFFFU;
    ctx->rom->sram_offset = 0xFFFFFFFFU;
    ctx->rom->eeprom_offset = 0xFFFFFFFFU;
    ctx->rom->game_code[0] = '\0';
    ctx->rom->maker_code[0] = '\0';
    ctx->rom->path[0] = '\0';

    ctx->save->event_counters = 0U;
    ctx->save->flush_count = 0U;
    ctx->save->quiet_ticks = 0U;
    ctx->save->last_bytes = 0U;
    ctx->save->last_checksum = 0U;
    ctx->save->active_kind = PS_APP_SAVE_KIND_NONE;
    ctx->save->dirty = 0U;
    ctx->save->loaded_from_sd = 0U;
    ctx->save->path[0] = '\0';

    ctx->diag->last_runtime_irq_sts = 0U;
    ctx->diag->last_physical_keys = 0U;
    ctx->diag->warn_print_count = 0U;
    ctx->diag->warn_suppress_ticks = 0U;
    ctx->diag->warn_last_err_latch = 0U;
    ctx->diag->warn_last_vdma_errs = 0U;
    ctx->diag->stall_last_pc = 0U;
    ctx->diag->stall_last_mem = 0U;
    ctx->diag->stall_last_dma = 0U;
    ctx->diag->stall_last_frame = 0U;
    ctx->diag->stall_same_sample_count = 0U;
    ctx->diag->auto_boot_audit_printed = 0U;
    ctx->diag->auto_stall_audit_printed = 0U;
    ctx->diag->log_input_delta_enable = 0U;
    ctx->diag->log_fbscan_auto_enable = 0U;
    ctx->diag->delayed_chain_tick = 0U;
    ctx->diag->delayed_chain_printed = 0U;
    ctx->diag->fbscan_tick = 0U;
    ctx->diag->fbscan_dump_count = 0U;
    ctx->diag->fbscan_last_anomaly_tick = 0U;

    ctx->usb_host->enabled = (u8)((PS_APP_USBHOST_ENABLE_DEFAULT != 0U) ? 1U : 0U);
    ctx->usb_host->initialized = 0U;
    ctx->usb_host->irq_connected = 0U;
    ctx->usb_host->device_present = 0U;
    ctx->usb_host->xbox_interface_active = 0U;
    ctx->usb_host->bus_id = 0U;
    ctx->usb_host->interface_number = 0U;
    ctx->usb_host->port_speed = 0U;
    ctx->usb_host->vendor_id = 0U;
    ctx->usb_host->product_id = 0U;
    ctx->usb_host->interface_class = 0U;
    ctx->usb_host->interface_subclass = 0U;
    ctx->usb_host->interface_protocol = 0U;
    ctx->usb_host->ep_in_addr = 0U;
    ctx->usb_host->ep_out_addr = 0U;
    ctx->usb_host->irq_count = 0U;
    ctx->usb_host->event_count = 0U;
    ctx->usb_host->attach_count = 0U;
    ctx->usb_host->detach_count = 0U;
    ctx->usb_host->in_report_count = 0U;
    ctx->usb_host->in_report_error_count = 0U;
    ctx->usb_host->out_report_count = 0U;
    ctx->usb_host->out_report_error_count = 0U;

    ctx->input->enabled = (u8)((PS_APP_INPUT_ENABLE_DEFAULT != 0U) ? 1U : 0U);
    ctx->input->active = 0U;
    ctx->input->release_pending = 0U;
    ctx->input->report_valid = 0U;
    ctx->input->protocol_is_xinput = 0U;
    ctx->input->output_capable = 0U;
    ctx->input->vendor_id = 0U;
    ctx->input->product_id = 0U;
    ctx->input->interface_number = 0U;
    ctx->input->interface_class = 0U;
    ctx->input->interface_subclass = 0U;
    ctx->input->interface_protocol = 0U;
    ctx->input->ep_in_addr = 0U;
    ctx->input->ep_out_addr = 0U;
    ctx->input->ep_in_interval_ms = 0U;
    ctx->input->ep_out_interval_ms = 0U;
    ctx->input->buttons = 0U;
    ctx->input->lt = 0U;
    ctx->input->rt = 0U;
    ctx->input->lx = 0;
    ctx->input->ly = 0;
    ctx->input->rx = 0;
    ctx->input->ry = 0;
    ctx->input->mapped_keys = 0U;
    ctx->input->last_committed_keys = 0U;
    ctx->input->report_count = 0U;
    ctx->input->parse_error_count = 0U;
    ctx->input->unsupported_report_count = 0U;
    ctx->input->last_report_len = 0U;

    ctx->hdmi->initialized = 0U;
    ctx->hdmi->irq_connected = 0U;
    ctx->hdmi->hpd_level = 0U;
    ctx->hdmi->present_enable = 0U;
    ctx->hdmi->hpd_pending = 0U;
    ctx->hdmi->edid_valid = 0U;
    ctx->hdmi->edid_refresh_pending = 0U;
    ctx->hdmi->blank_frame_pending = 0U;
    ctx->hdmi->preferred_is_640x480p60 = 0U;
    ctx->hdmi->preferred_timing_valid = 0U;
    ctx->hdmi->preferred_vic = 0U;
    ctx->hdmi->vendor_id = 0U;
    ctx->hdmi->product_code = 0U;
    ctx->hdmi->hpd_rise_count = 0U;
    ctx->hdmi->hpd_fall_count = 0U;
    ctx->hdmi->edid_read_ok_count = 0U;
    ctx->hdmi->edid_read_fail_count = 0U;
    ctx->hdmi->last_error = 0U;
    ctx->hdmi->last_service_tick = 0U;
    ctx->hdmi->last_hpd_change_tick = 0U;
    memset(ctx->hdmi->edid_block0, 0, sizeof(ctx->hdmi->edid_block0));

    ctx->ui->mode = (u8)PS_APP_UI_MODE_MENU;
    ctx->ui->selected_index = 0U;
    ctx->ui->game_count = 0U;
    ctx->ui->refresh_requested = 1U;
    ctx->ui->launch_requested = 0U;
    ctx->ui->launch_index = 0U;
    ctx->ui->lt_exit_latched = 0U;
    ctx->ui->board_exit_latched = 0U;
    ctx->ui->lt_hold_ms = 0U;
    ctx->ui->board_exit_hold_ms = 0U;
    ctx->ui->last_buttons = 0U;
    ctx->ui->launch_path[0] = '\0';
    ctx->ui->status_line[0] = '\0';

    ctx->ps_gpio_ready = 0U;
    ctx->ps_btn_last_mask = 0U;
}

#if (PS_APP_AUTOLOAD_DEFAULT_ROM != 0U)
static XStatus PsAppRuntime_AutoloadDefaultRom(PsAppRuntimeContext *ctx) {
    XStatus status;

    xil_printf("[INIT] 9: default rom path=%s\r\n", PS_APP_DEFAULT_ROM_SD_PATH);
    status = PsAppRuntime_LoadRomFromSd(ctx, PS_APP_DEFAULT_ROM_SD_PATH);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 9 info: place a known-good ROM at %s\r\n",
                   PS_APP_DEFAULT_ROM_SD_PATH);
        xil_printf("[INIT] 9 info: or use 'rom load <path>' from the UART console\r\n");
        xil_printf("[INIT] 9 info: avoid relying on the previous hardcoded homebrew ROM for bring-up\r\n");
    }

    return status;
}
#endif

void PsAppRuntime_ApplyShadowConfig(PsAppRuntimeContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    PsGbaRegs_CommitConfig(ctx->regs,
                           ctx->config->ctrl,
                           ctx->config->keys,
                           ctx->config->max_pak_addr,
                           ctx->config->cycle_precalc,
                           ctx->config->rtc_timestamp);
}

XStatus PsAppRuntime_InitUart(PsAppRuntimeContext *ctx) {
    XUartPs_Config *cfg;
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    cfg = XUartPs_LookupConfig(XPAR_XUARTPS_0_BASEADDR);
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XUartPs_CfgInitialize(ctx->uart, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XUartPs_SetBaudRate(ctx->uart, PS_APP_UART_BAUDRATE);
}

XStatus PsAppRuntime_ProgramAudio(PsAppRuntimeContext *ctx) {
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    status = PsAudioCodec_ProgramPlayback(ctx->codec,
                                          ctx->audio->sample_rate_hz,
                                          ctx->audio->bits_per_sample);
    if (status != XST_SUCCESS) {
        return status;
    }

    return PsAppRuntime_ApplyAudioOutputState(ctx);
}


XStatus PsAppRuntime_InitSystem(PsAppRuntimeContext *ctx) {
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    xil_printf("[INIT] 0: context cleared\r\n");
    xil_printf("[INIT] 1: gba regs bootstrap\r\n");
    PsGbaRegs_Init(ctx->regs, XPAR_ZYNQ_GBA_TOP_0_BASEADDR);
    PsAppRuntime_InitDefaults(ctx);
    PsAppRuntimeFeature_InitDefaults(ctx);
    PsAppSave_Reset(ctx->save_ctx);
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsGbaRegs_SetIrqEnable(ctx->regs, ctx->config->irq_enable);
    PsGbaRegs_ClearIrqStatus(ctx->regs, PS_APP_IRQ_MASK_VSYNC | PS_APP_IRQ_MASK_ERROR);
    xil_printf("[INIT] 1: gba regs done\r\n");

    xil_printf("[INIT] 2: vdma init\r\n");
    status = PsHdmiVdma_Init(ctx->vdma,
                             XPAR_XAXIVDMA_0_BASEADDR,
                             (UINTPTR)PS_APP_FB_REGION_BASE_ADDR,
                             PS_APP_FB_FRAME_STORE_BYTES,
                             PS_APP_HDMI_WIDTH,
                             PS_APP_HDMI_HEIGHT,
                             PS_APP_HDMI_BPP);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 2 failed: %d\r\n", status);
        return status;
    }

    status = PsAppVideo_InitInterrupts(ctx->video_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 2.1 failed: %d\r\n", status);
        return status;
    }

    PsAppDiag_PrintVdmaSnapshot(ctx->diag_ctx, "post-init");
    xil_printf("[INIT] 2: vdma done\r\n");

    xil_printf("[INIT] 3: audio codec i2c init\r\n");
    status = PsAudioCodec_Init(ctx->codec,
                               XPAR_XIICPS_0_BASEADDR,
                               SSM2603_I2C_ADDR,
                               100000U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 3 warning: %d (audio disabled)\r\n", status);
        ctx->codec->is_ready = 0U;
    } else {
        xil_printf("[INIT] 3: audio i2c ready (defer program)\r\n");
    }

    xil_printf("[INIT] 5: clear boot framebuffers\r\n");
    status = PsAppVideo_RenderBootFrames(ctx->video_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 failed: %d\r\n", status);
        return status;
    }
    xil_printf("[INIT] 5: framebuffers ready\r\n");
    xil_printf("[INIT] 5: trying park frame 0\r\n");
    status = PsAppVideo_RequestFrame(ctx->video_ctx, 0U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 park failed: %d\r\n", status);
    } else {
        xil_printf("[INIT] 5 park ok\r\n");
    }
    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);

    if (ctx->codec->is_ready != 0U) {
        xil_printf("[INIT] 7: audio codec program\r\n");
        status = PsAppRuntime_ProgramAudio(ctx);
        if (status != XST_SUCCESS) {
            xil_printf("[INIT] 7 warning: %d (audio disabled)\r\n", status);
            ctx->codec->is_ready = 0U;
        } else {
            xil_printf("[INIT] 7: audio done\r\n");
        }
    }

    xil_printf("[INIT] 8: ps gpio BTN4/BTN5 init\r\n");
    status = PsAppRuntime_InitPsGpio(ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 8 failed: %d\r\n", status);
        return status;
    }
    if (ctx->ps_gpio_ready != 0U) {
        PsAppRuntime_PrintPsButtons(ctx->ps_btn_last_mask);
    }

    xil_printf("[INIT] 8.1: hdmi hpd/ddc init (HPD=GPIO54, DDC=IIC1)\r\n");
    status = PsAppHdmiLink_Init(ctx->hdmi_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 8.1 warning: hdmi link init failed: %d (keep fixed 640x480@60)\r\n",
                   status);
    }

    xil_printf("[INIT] 8.2: input map BTN3/2/1/0=left/up/down/right SW3/2/1/0=select(start-mod)/start/b/a SW3+BTN3=L SW3+BTN0=R\r\n");
    xil_printf("[INIT] 8.3: xinput parser init\r\n");
    PsAppInput_Init(ctx->input_ctx);
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    xil_printf("[INIT] 8.3: gba map A<=B(right) B<=A(bottom) deadzone=%d trig=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#else
    xil_printf("[INIT] 8.3: gba map A<=A(bottom) B<=B(right) deadzone=%d trig=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#endif
    xil_printf("[INIT] 8.4: usb host init\r\n");
    if (PsAppUsbHost_Init(ctx->usb_host_ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 8.4 warning: usb host unavailable\r\n");
    } else {
        (void)PsAppUsbHost_PrintStatusChecked(ctx->usb_host_ctx);
    }

    xil_printf("[INIT] 9: startup ui menu mode (autoload disabled)\r\n");
#if (PS_APP_AUTOLOAD_DEFAULT_ROM != 0U)
    if (PsAppRuntime_AutoloadDefaultRom(ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 9 warning: ROM autoload skipped\r\n");
    }
#else
    xil_printf("[INIT] 9 info: use UI launcher or 'rom load <path>' to start a game\r\n");
#endif

    return XST_SUCCESS;
}
