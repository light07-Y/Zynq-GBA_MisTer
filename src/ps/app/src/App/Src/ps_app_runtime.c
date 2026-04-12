#include "App/Inc/ps_app_runtime.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "sleep.h"
#include "xaxivdma_hw.h"
#include "xparameters.h"
#include "xuartps.h"
#include "xil_printf.h"

#include "Diagnostics/Inc/ps_app_diag.h"
#include "Video/Inc/ps_app_video.h"
#include "Rom/Inc/ps_rom_loader.h"

static void PsAppRuntime_CopyText(char *dst, size_t dst_size, const char *src) {
    size_t src_len;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    src_len = strlen(src);
    if (src_len >= dst_size) {
        src_len = dst_size - 1U;
    }

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}
static int PsAppRuntime_ResolveRomPath(const char *input,
                                       char *resolved_path,
                                       size_t resolved_path_size) {
    const char *effective_path;
    size_t input_len;

    if ((resolved_path == NULL) || (resolved_path_size == 0U)) {
        return -1;
    }

    effective_path = (input != NULL) ? input : PS_APP_DEFAULT_ROM_SD_PATH;
    input_len = strlen(effective_path);

    if (strstr(effective_path, ":/") != NULL) {
        if (input_len >= resolved_path_size) {
            return -1;
        }
        PsAppRuntime_CopyText(resolved_path, resolved_path_size, effective_path);
        return 0;
    }

    if ((input_len + 3U) >= resolved_path_size) {
        return -1;
    }

    resolved_path[0] = '0';
    resolved_path[1] = ':';
    resolved_path[2] = '/';
    memcpy(&resolved_path[3], effective_path, input_len);
    resolved_path[input_len + 3U] = '\0';
    return 0;
}
static void PsAppRuntime_PrintPhysicalKeys(u32 keys_mask) {
    static const char *kNames[10] = {
        "A", "B", "SELECT", "START", "RIGHT",
        "LEFT", "UP", "DOWN", "R", "L"
    };
    u32 idx;
    u32 any;

    xil_printf("[INPUT] phys=0x%03x", (unsigned int)(keys_mask & 0x3FFU));
    any = 0U;
    for (idx = 0U; idx < 10U; ++idx) {
        if ((keys_mask & (1U << idx)) != 0U) {
            xil_printf("%s%s", any != 0U ? "+" : " ", kNames[idx]);
            any = 1U;
        }
    }
    if (any == 0U) {
        xil_printf(" (idle)");
    }
    xil_printf("\r\n");
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
    ctx->audio->volume = 0x79U;
    ctx->audio->tone_enable = 1U;

    ctx->video->display_frame_idx = 0xFFU;
    ctx->video->pending_frame_idx = 0xFFU;
    ctx->video->fbcap_last_frame_seq = 0U;
    ctx->video->fbcap_last_buf_idx = 0xFFU;
    ctx->video->fbcap_last_frame_idx = 0xFFU;

    ctx->rom->loaded = 0U;
    ctx->rom->is_loading = 0U;
    ctx->rom->size_bytes = 0U;
    ctx->rom->size_aligned = 0U;
    ctx->rom->path[0] = '\0';

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
    ctx->diag->delayed_chain_tick = 0U;
    ctx->diag->delayed_chain_printed = 0U;
    ctx->diag->fbscan_tick = 0U;
    ctx->diag->fbscan_dump_count = 0U;
    ctx->diag->fbscan_last_anomaly_tick = 0U;
}

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

    status = PsAudioCodec_SetHeadphoneVolume(ctx->codec, ctx->audio->volume);
    if (status != XST_SUCCESS) {
        return status;
    }

    return PsAudioCodec_SetMute(ctx->codec, ctx->audio->mute);
}

XStatus PsAppRuntime_LoadRomFromSd(PsAppRuntimeContext *ctx, const char *requested_path) {
    PsRomLoadResult load_result;
    char resolved_path[PS_APP_ROM_PATH_MAX_CHARS];
    XStatus status;

    if ((ctx == NULL) ||
        (PsAppRuntime_ResolveRomPath(requested_path, resolved_path, sizeof(resolved_path)) != 0)) {
        xil_printf("[ROM] invalid path\r\n");
        return XST_INVALID_PARAM;
    }

    ctx->rom->is_loading = 1U;
    ctx->rom->loaded = 0U;
    ctx->rom->size_bytes = 0U;
    ctx->rom->size_aligned = 0U;
    ctx->diag->stall_last_pc = 0U;
    ctx->diag->stall_last_mem = 0U;
    ctx->diag->stall_last_dma = 0U;
    ctx->diag->stall_last_frame = 0U;
    ctx->diag->stall_same_sample_count = 0U;
    ctx->diag->auto_stall_audit_printed = 0U;
    PsAppRuntime_CopyText(ctx->rom->path, sizeof(ctx->rom->path), resolved_path);

    ctx->config->ctrl |= PS_APP_GBA_CTRL_BOOT_REQUIRED;
    ctx->config->ctrl &= ~GBA_CTRL_CORE_ON;
    ctx->config->ctrl |= GBA_CTRL_ROM_LOADING;
    ctx->config->max_pak_addr = 0U;
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "rom-preload");

    PsGbaRegs_SetSwReset(ctx->regs, 1U);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "reset-assert");

    xil_printf("[ROM] load %s\r\n", resolved_path);
    xil_printf("[ROM] target addr=0x%08x limit=%u\r\n",
               (unsigned int)PS_APP_GBA_ROM_REGION_BASE_ADDR,
               (unsigned int)PS_APP_GBA_ROM_REGION_MAX_BYTES);
    status = PsRomLoader_LoadSdFile(resolved_path,
                                    (UINTPTR)PS_APP_GBA_ROM_REGION_BASE_ADDR,
                                    PS_APP_GBA_ROM_REGION_MAX_BYTES,
                                    &load_result);
    if (status != XST_SUCCESS) {
        ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
        ctx->config->max_pak_addr = 0U;
        PsAppRuntime_ApplyShadowConfig(ctx);
        PsGbaRegs_SetSwReset(ctx->regs, 1U);
        ctx->rom->is_loading = 0U;
        xil_printf("[ROM] load failed: %s\r\n", PsRomLoader_StrError(load_result.fs_result));
        return status;
    }

    ctx->config->max_pak_addr = load_result.max_pak_addr & 0x1FFFFFFU;
    ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
    ctx->config->ctrl |= (GBA_CTRL_CORE_ON | PS_APP_GBA_CTRL_BOOT_REQUIRED);
    PsAppRuntime_ApplyShadowConfig(ctx);
    (void)PsHdmiVdma_FillAllFrames(ctx->vdma, 0x00000000U);
    (void)PsAppVideo_RequestFrame(ctx->video_ctx, 0U);
    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
    ctx->video->fbcap_last_frame_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "rom-postload");

    usleep(2000U);
    PsGbaRegs_SetSwReset(ctx->regs, 0U);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "reset-release");
    PsAppRuntime_ApplyShadowConfig(ctx);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "post-release-commit");

    ctx->rom->loaded = 1U;
    ctx->rom->is_loading = 0U;
    ctx->rom->size_bytes = load_result.bytes_loaded;
    ctx->rom->size_aligned = load_result.bytes_aligned;

    xil_printf("[ROM] ready bytes=%u aligned=%u maxpak=0x%08x addr=0x%08x\r\n",
               (unsigned int)ctx->rom->size_bytes,
               (unsigned int)ctx->rom->size_aligned,
               (unsigned int)ctx->config->max_pak_addr,
               (unsigned int)PS_APP_GBA_ROM_REGION_BASE_ADDR);
    PsAppDiag_PrintRomHeader(ctx->diag_ctx);

    return XST_SUCCESS;
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

    xil_printf("[INIT] 8: boot audio cue armed=%u\r\n", (unsigned int)ctx->audio->tone_enable);
    xil_printf("[INIT] 8.1: input map BTN3/2/1/0=left/up/down/right SW3/2/1/0=select(start-mod)/start/b/a SW3+BTN3=L SW3+BTN0=R\r\n");
    xil_printf("[INIT] 9: rom autoload\r\n");
    if (PsAppRuntime_AutoloadDefaultRom(ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 9 warning: ROM autoload skipped\r\n");
    }

    return XST_SUCCESS;
}

void PsAppRuntime_Service(PsAppRuntimeContext *ctx) {
    u32 status0;
    u32 status1;
    u32 err_latch;
    u32 irq_sts;
    u32 cycles_missing;
    u32 dbg_pc;
    u32 dbg_dma;
    u32 dbg_mem;
    u32 vdma_status;
    u32 vdma_errs;
    u32 physical_keys;

    if (ctx == NULL) {
        return;
    }

    status0 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0);
    status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS) & 0x3U;
    dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);
    vdma_errs = vdma_status & XAXIVDMA_SR_ERR_ALL_MASK;
    cycles_missing = (status1 >> 2) & 0x3FFFU;
    physical_keys = status0 & 0x3FFU;

    if (ctx->diag->last_physical_keys != physical_keys) {
        PsAppRuntime_PrintPhysicalKeys(physical_keys);
        ctx->diag->last_physical_keys = physical_keys;
    }

    if (ctx->rom->is_loading != 0U) {
        PsAppVideo_AttemptRecover(ctx->video_ctx, vdma_status, vdma_errs);
        return;
    }

    if ((cycles_missing != 0U) || (err_latch != 0U) || (vdma_errs != 0U)) {
        u32 new_error = 0U;

        if ((err_latch != ctx->diag->warn_last_err_latch) ||
            (vdma_errs != ctx->diag->warn_last_vdma_errs)) {
            new_error = 1U;
        }
        if ((ctx->diag->warn_print_count == 0U) && (cycles_missing != 0U)) {
            new_error = 1U;
        }

        if (new_error != 0U) {
            xil_printf("[WARN] miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x\r\n",
                       (unsigned int)cycles_missing,
                       (unsigned int)err_latch,
                       (unsigned int)vdma_status,
                       (unsigned int)vdma_errs);
            ctx->diag->warn_print_count++;
            ctx->diag->warn_suppress_ticks = 0U;
        } else {
            ctx->diag->warn_suppress_ticks++;
            {
                u32 threshold = 300U;
                u32 i;
                for (i = 1U; i < ctx->diag->warn_print_count && i < 4U; i++) {
                    threshold *= 10U;
                }
                if (ctx->diag->warn_suppress_ticks >= threshold) {
                    xil_printf("[WARN] summary miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x (suppressed %u ticks)\r\n",
                               (unsigned int)cycles_missing,
                               (unsigned int)err_latch,
                               (unsigned int)vdma_status,
                               (unsigned int)vdma_errs,
                               (unsigned int)ctx->diag->warn_suppress_ticks);
                    ctx->diag->warn_print_count++;
                    ctx->diag->warn_suppress_ticks = 0U;
                }
            }
        }

        ctx->diag->warn_last_err_latch = err_latch;
        ctx->diag->warn_last_vdma_errs = vdma_errs;
    } else {
        if (ctx->diag->warn_print_count != 0U) {
            xil_printf("[WARN] cleared (was miss=%u)\r\n",
                       (unsigned int)ctx->diag->warn_print_count);
        }
        ctx->diag->warn_print_count = 0U;
        ctx->diag->warn_suppress_ticks = 0U;
        ctx->diag->warn_last_err_latch = 0U;
        ctx->diag->warn_last_vdma_errs = 0U;
    }

    if (irq_sts != 0U) {
        if ((irq_sts & PS_APP_IRQ_MASK_VSYNC) != 0U) {
            PsAppVideo_PresentCapturedFrameIfReady(ctx->video_ctx);
        }

        if ((irq_sts & ~PS_APP_IRQ_MASK_VSYNC) != 0U) {
            if (ctx->diag->last_runtime_irq_sts != irq_sts) {
                xil_printf("[IRQ] sts=0x%x\r\n", (unsigned int)irq_sts);
                ctx->diag->last_runtime_irq_sts = irq_sts;
            }
        } else {
            ctx->diag->last_runtime_irq_sts = 0U;
        }

        PsGbaRegs_ClearIrqStatus(ctx->regs, irq_sts);
    } else {
        ctx->diag->last_runtime_irq_sts = 0U;
    }

    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
    PsAppDiag_MaybePrintStallAudit(ctx->diag_ctx, status1, dbg_pc, dbg_mem, dbg_dma);
    PsAppVideo_AttemptRecover(ctx->video_ctx, vdma_status, vdma_errs);

    if (ctx->rom->loaded && ctx->diag->delayed_chain_printed < 2U) {
        ctx->diag->delayed_chain_tick++;
        if ((ctx->diag->delayed_chain_printed == 0U && ctx->diag->delayed_chain_tick >= 500U) ||
            (ctx->diag->delayed_chain_printed == 1U && ctx->diag->delayed_chain_tick >= 1500U)) {
            xil_printf("[AUTO] delayed chain snapshot @tick=%u\r\n",
                       (unsigned int)ctx->diag->delayed_chain_tick);
            PsAppDiag_PrintChainSnapshot(ctx->diag_ctx, "delayed");
            PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "delayed");
            ctx->diag->delayed_chain_printed++;
        }
    }

    if (ctx->rom->loaded && ctx->diag->delayed_chain_tick > 200U) {
        ctx->diag->fbscan_tick++;
        if ((ctx->diag->fbscan_tick % PS_APP_FBSCAN_INTERVAL_TICKS) == 0U) {
            if (ctx->diag->fbscan_dump_count < PS_APP_FBSCAN_MAX_DUMPS) {
                PsAppDiag_ScanFramebufferForAnomaly(ctx->diag_ctx, "auto", 0U);
            }
        }
    }
}

void PsAppMonitorTask(void *arg) {
    PsAppRuntimeContext *ctx = (PsAppRuntimeContext *)arg;
    TickType_t delay_ticks;

    delay_ticks = pdMS_TO_TICKS(PS_APP_MONITOR_INTERVAL_MS);
    if (delay_ticks == 0U) {
        delay_ticks = 1U;
    }

    for (;;) {
        PsAppRuntime_Service(ctx);
        vTaskDelay(delay_ticks);
    }
}
