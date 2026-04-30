#include "App/Inc/ps_app_runtime.h"

#include "FreeRTOS.h"
#include "task.h"
#include "xaxivdma_hw.h"
#include "xil_printf.h"

#include "Diagnostics/Inc/ps_app_diag.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Input/Inc/ps_app_input.h"
#include "Save/Inc/ps_app_save.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Video/Inc/ps_app_video.h"
#include "Hdmi/Inc/ps_app_hdmi_link.h"
#include "UsbHost/Inc/ps_app_usbhost.h"

#define PS_APP_AUDIO_VOLUME_STEP       5U

static const char *const s_ps_app_runtime_gba_key_names[10] = {
    "A", "B", "SELECT", "START", "RIGHT",
    "LEFT", "UP", "DOWN", "R", "L"
};

static PsAppSaveContext *s_ps_app_runtime_save_task_context = NULL;

static u8 PsAppRuntime_IsPcLikelyValid(u32 pc) {
    if (pc < 0x00004000U) {
        return 1U;
    }
    if ((pc >= 0x02000000U) && (pc <= 0x0203FFFFU)) {
        return 1U;
    }
    if ((pc >= 0x03000000U) && (pc <= 0x03007FFFU)) {
        return 1U;
    }
    if ((pc >= 0x08000000U) && (pc <= 0x0DFFFFFFU)) {
        return 1U;
    }
    return 0U;
}

static u8 PsAppRuntime_ReconfirmInvalidPc(PsAppRuntimeContext *ctx,
                                          u32 first_pc,
                                          u32 *confirmed_pc,
                                          u32 *confirmed_mem,
                                          u32 *confirmed_dma) {
    u32 pc_retry1;
    u32 pc_retry2;
    u32 mem_retry;
    u32 dma_retry;

    if ((ctx == NULL) || (ctx->regs == NULL)) {
        if (confirmed_pc != NULL) {
            *confirmed_pc = first_pc;
        }
        return (u8)(PsAppRuntime_IsPcLikelyValid(first_pc) == 0U);
    }

    pc_retry1 = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    if (PsAppRuntime_IsPcLikelyValid(pc_retry1) != 0U) {
        return 0U;
    }

    pc_retry2 = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    if (PsAppRuntime_IsPcLikelyValid(pc_retry2) != 0U) {
        return 0U;
    }

    mem_retry = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    dma_retry = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    /* Require memory state to be meaningful before classifying as runaway:
     * upper byte carries memory-mux state ID; 0x00 is typically reset/idle
     * and often appears in torn CDC samples. */
    if (((mem_retry >> 24) & 0xFFU) == 0U) {
        return 0U;
    }

    if (confirmed_pc != NULL) {
        *confirmed_pc = pc_retry2;
    }
    if (confirmed_mem != NULL) {
        *confirmed_mem = mem_retry;
    }
    if (confirmed_dma != NULL) {
        *confirmed_dma = dma_retry;
    }

    return 1U;
}

static void PsAppRuntime_PcGuardReleaseReset(PsAppRuntimeContext *ctx) {
    if ((ctx == NULL) || (ctx->diag == NULL) || (ctx->config == NULL) || (ctx->regs == NULL)) {
        return;
    }

    PsGbaRegs_SetSwReset(ctx->regs, 0U);
    ctx->config->ctrl |= (GBA_CTRL_CORE_ON | PS_APP_GBA_CTRL_BOOT_REQUIRED | GBA_CTRL_ROM_DDR_SAFE);
    ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
    PsAppRuntime_ApplyShadowConfig(ctx);
    ctx->diag->pc_guard_recovery_active = 0U;
    ctx->diag->pc_guard_reset_hold_ticks = 0U;
    ctx->diag->pc_guard_invalid_streak = 0U;
    ctx->diag->rom_ddr_safe_recovery_active = 1U;
    ctx->diag->rom_ddr_safe_recovery_count++;
    ctx->diag->rom_ddr_safe_recovery_tick = ctx->diag->pc_guard_tick;
    xil_printf("[GUARD] recovery released count=%u cooldown=%u ddr_safe=1\r\n",
               (unsigned int)ctx->diag->pc_guard_recovery_count,
               (unsigned int)ctx->diag->pc_guard_cooldown_ticks);
}

static void PsAppRuntime_ServicePcGuard(PsAppRuntimeContext *ctx,
                                        u32 status1,
                                        u32 dbg_pc,
                                        u32 dbg_mem,
                                        u32 dbg_dma) {
    u8 pc_valid;

    if ((ctx == NULL) || (ctx->diag == NULL) || (ctx->config == NULL) || (ctx->regs == NULL)) {
        return;
    }

    if ((ctx->rom == NULL) || (ctx->rom->loaded == 0U) || (ctx->rom->is_loading != 0U)) {
        ctx->diag->pc_guard_invalid_streak = 0U;
        return;
    }

    ctx->diag->pc_guard_tick++;
    if (ctx->diag->pc_guard_cooldown_ticks > 0U) {
        ctx->diag->pc_guard_cooldown_ticks--;
    }

    if (ctx->diag->pc_guard_enable == 0U) {
        if (ctx->diag->pc_guard_recovery_active != 0U) {
            PsAppRuntime_PcGuardReleaseReset(ctx);
        }
        if ((ctx->diag->pc_guard_audio_restore_pending != 0U) &&
            (ctx->audio != NULL) &&
            (ctx->audio->mute != 0U)) {
            ctx->audio->mute = 0U;
            (void)PsAppRuntime_ApplyAudioOutputState(ctx);
            ctx->diag->pc_guard_audio_restore_pending = 0U;
        }
        ctx->diag->pc_guard_invalid_streak = 0U;
        return;
    }

    if (ctx->diag->pc_guard_recovery_active != 0U) {
        if (ctx->diag->pc_guard_reset_hold_ticks > 0U) {
            ctx->diag->pc_guard_reset_hold_ticks--;
        }
        if (ctx->diag->pc_guard_reset_hold_ticks == 0U) {
            PsAppRuntime_PcGuardReleaseReset(ctx);
        }
        return;
    }

    pc_valid = PsAppRuntime_IsPcLikelyValid(dbg_pc);
    if (pc_valid != 0U) {
        ctx->diag->pc_guard_invalid_streak = 0U;
        if ((ctx->diag->pc_guard_audio_restore_pending != 0U) &&
            (ctx->diag->pc_guard_cooldown_ticks == 0U) &&
            (ctx->audio != NULL) &&
            (ctx->audio->mute != 0U)) {
            ctx->audio->mute = 0U;
            (void)PsAppRuntime_ApplyAudioOutputState(ctx);
            ctx->diag->pc_guard_audio_restore_pending = 0U;
            xil_printf("[GUARD] audio restored after cooldown\r\n");
        }
        return;
    }

    if (PsAppRuntime_ReconfirmInvalidPc(ctx, dbg_pc, &dbg_pc, &dbg_mem, &dbg_dma) == 0U) {
        if (ctx->diag->pc_guard_invalid_streak != 0U) {
            ctx->diag->pc_guard_invalid_streak = 0U;
        }
        return;
    }

    ctx->diag->pc_guard_last_bad_pc = dbg_pc;
    ctx->diag->pc_guard_last_bad_status1 = status1;
    ctx->diag->pc_guard_last_bad_mem = dbg_mem;
    ctx->diag->pc_guard_last_bad_dma = dbg_dma;

    if (ctx->diag->pc_guard_cooldown_ticks > 0U) {
        return;
    }

    if (ctx->diag->pc_guard_invalid_streak < 0xFFFFFFFFU) {
        ctx->diag->pc_guard_invalid_streak++;
    }
    if (ctx->diag->pc_guard_invalid_streak < PS_APP_PC_GUARD_TRIGGER_SAMPLES) {
        return;
    }

    ctx->diag->pc_guard_recovery_count++;
    ctx->diag->pc_guard_last_trigger_tick = ctx->diag->pc_guard_tick;
    ctx->diag->pc_guard_recovery_active = 1U;
    ctx->diag->pc_guard_reset_hold_ticks = PS_APP_PC_GUARD_RESET_HOLD_TICKS;
    ctx->diag->pc_guard_cooldown_ticks = PS_APP_PC_GUARD_COOLDOWN_TICKS;
    ctx->diag->pc_guard_invalid_streak = 0U;

    if ((ctx->audio != NULL) && (ctx->audio->mute == 0U) && (ctx->audio->volume > 0U)) {
        ctx->diag->pc_guard_audio_restore_pending = 1U;
        ctx->audio->mute = 1U;
        (void)PsAppRuntime_ApplyAudioOutputState(ctx);
    } else {
        ctx->diag->pc_guard_audio_restore_pending = 0U;
    }

    /* 先停 core，再抓快照，避免复位后观测值被清零。 */
    ctx->config->ctrl |= PS_APP_GBA_CTRL_BOOT_REQUIRED;
    ctx->config->ctrl &= ~GBA_CTRL_CORE_ON;
    PsAppRuntime_ApplyShadowConfig(ctx);

    xil_printf("[GUARD] runaway detected pc=0x%08x mem=0x%08x dma=0x%08x frame=%u miss=%u trigger=%u\r\n",
               (unsigned int)dbg_pc,
               (unsigned int)dbg_mem,
               (unsigned int)dbg_dma,
               (unsigned int)(status1 & 0x3U),
               (unsigned int)((status1 >> 2) & 0x3FFFU),
               (unsigned int)PS_APP_PC_GUARD_TRIGGER_SAMPLES);
    PsAppDiag_PrintRuntimeSample(ctx->diag_ctx, "runaway_pre");
    PsAppDiag_PrintDdrLogSnapshot(ctx->diag_ctx, "runaway_pre");
    PsAppDiag_PrintChainSnapshot(ctx->diag_ctx, "runaway_pre");
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "runaway_pre");

    PsGbaRegs_SetSwReset(ctx->regs, 1U);
    PsGbaRegs_ClearErrorLatch(ctx->regs);
}

static void PsAppRuntime_PrintPsButtons(u32 ps_btn_mask) {
    xil_printf("[BTN] ps BTN4=%u BTN5=%u mask=0x%02x\r\n",
               (unsigned int)((ps_btn_mask & PS_APP_BTN4_MASK) != 0U),
               (unsigned int)((ps_btn_mask & PS_APP_BTN5_MASK) != 0U),
               (unsigned int)(ps_btn_mask & (PS_APP_BTN4_MASK | PS_APP_BTN5_MASK)));
}

static void PsAppRuntime_PrintPhysicalKeys(u32 keys_mask) {
    u32 idx;
    u32 any;

    xil_printf("[INPUT] phys=0x%03x", (unsigned int)(keys_mask & 0x3FFU));
    any = 0U;
    for (idx = 0U; idx < 10U; ++idx) {
        if ((keys_mask & (1U << idx)) != 0U) {
            xil_printf("%s%s", any != 0U ? "+" : " ", s_ps_app_runtime_gba_key_names[idx]);
            any = 1U;
        }
    }
    if (any == 0U) {
        xil_printf(" (idle)");
    }
    xil_printf("\r\n");
}

static const char *PsAppRuntime_GbaKeyNameByBit(u32 bit_index) {
    if (bit_index < 10U) {
        return s_ps_app_runtime_gba_key_names[bit_index];
    }
    return NULL;
}

static const char *PsAppRuntime_PadKeyNameByBit(u32 bit_index) {
    switch (bit_index) {
        case 0U: return "DPAD_UP";
        case 1U: return "DPAD_DOWN";
        case 2U: return "DPAD_LEFT";
        case 3U: return "DPAD_RIGHT";
        case 4U: return "START";
        case 5U: return "BACK";
        case 6U: return "L3";
        case 7U: return "R3";
        case 8U: return "LB";
        case 9U: return "RB";
        case 10U: return "GUIDE";
        case 12U: return "A_BOTTOM";
        case 13U: return "B_RIGHT";
        case 14U: return "X_LEFT";
        case 15U: return "Y_TOP";
        case 16U: return "LT";
        case 17U: return "RT";
        case 18U: return "LS_RIGHT";
        case 19U: return "LS_LEFT";
        case 20U: return "LS_UP";
        case 21U: return "LS_DOWN";
        default: break;
    }
    return NULL;
}

static void PsAppRuntime_PrintMaskNamed(const char *tag,
                                        u32 mask,
                                        const char *(*name_of_bit)(u32),
                                        u32 max_bit) {
    u32 bit;
    u8 any;
    const char *name;

    xil_printf(" %s=", tag);
    any = 0U;
    for (bit = 0U; bit <= max_bit; ++bit) {
        if ((mask & (1UL << bit)) == 0U) {
            continue;
        }
        name = name_of_bit(bit);
        if (name == NULL) {
            continue;
        }
        xil_printf("%s%s", (any != 0U) ? "+" : "", name);
        any = 1U;
    }
    if (any == 0U) {
        xil_printf("-");
    }
}

static void PsAppRuntime_PrintInputDelta(PsAppRuntimeContext *ctx,
                                         u32 prev_pad_source_mask,
                                         u32 curr_pad_source_mask,
                                         u32 prev_gba_keys,
                                         u32 curr_gba_keys) {
    u32 pad_pressed;
    u32 pad_released;
    u32 gba_pressed;
    u32 gba_released;

    if ((ctx == NULL) || (ctx->input == NULL)) {
        return;
    }

    pad_pressed = curr_pad_source_mask & ~prev_pad_source_mask;
    pad_released = prev_pad_source_mask & ~curr_pad_source_mask;
    gba_pressed = curr_gba_keys & ~prev_gba_keys;
    gba_released = prev_gba_keys & ~curr_gba_keys;

    xil_printf("[INPUT] map ");
    PsAppRuntime_PrintMaskNamed("pad_press", pad_pressed, PsAppRuntime_PadKeyNameByBit, 21U);
    PsAppRuntime_PrintMaskNamed("pad_release", pad_released, PsAppRuntime_PadKeyNameByBit, 21U);
    PsAppRuntime_PrintMaskNamed("gba_press", gba_pressed, PsAppRuntime_GbaKeyNameByBit, 9U);
    PsAppRuntime_PrintMaskNamed("gba_release", gba_released, PsAppRuntime_GbaKeyNameByBit, 9U);
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    xil_printf(" ab_map=A<=B_RIGHT,B<=A_BOTTOM");
#else
    xil_printf(" ab_map=A<=A_BOTTOM,B<=B_RIGHT");
#endif
    xil_printf(" src=0x%08x gba=0x%03x\r\n",
               (unsigned int)curr_pad_source_mask,
               (unsigned int)(curr_gba_keys & 0x3FFU));
}


static void PsAppRuntime_ServiceInputFast(PsAppRuntimeContext *ctx) {
    u32 prev_gba_keys;
    u32 mapped_keys;
    u32 prev_pad_source_mask;
    u32 curr_pad_source_mask;
    u32 frame_token;
    u8 log_needed;
    u8 input_override;

    if (ctx == NULL) {
        return;
    }

    PsAppUsbHost_Service(ctx->usb_host_ctx);
    PsAppInput_Service(ctx->input_ctx);
    frame_token = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    PsAppInput_PublishFrame(ctx->input_ctx, frame_token);
    input_override = PsAppInput_ShouldOverrideKeys(ctx->input_ctx);
    if (input_override != 0U) {
        prev_gba_keys = ctx->input->last_committed_keys & 0x3FFU;
        mapped_keys = PsAppInput_GetMappedKeys(ctx->input_ctx) & 0x3FFU;
        prev_pad_source_mask = ctx->input->last_logged_source_mask;
        curr_pad_source_mask = ctx->input->source_snapshot_mask;
        log_needed = (u8)(((prev_gba_keys != mapped_keys) ||
                           (prev_pad_source_mask != curr_pad_source_mask)) ? 1U : 0U);
        if ((ctx->config->keys != mapped_keys) ||
            (PsAppInput_IsReleasePending(ctx->input_ctx) != 0U)) {
            ctx->config->keys = mapped_keys;
            PsAppRuntime_ApplyShadowConfig(ctx);
        }
        if ((log_needed != 0U) && (ctx->diag->log_input_delta_enable != 0U)) {
            PsAppRuntime_PrintInputDelta(ctx,
                                         prev_pad_source_mask,
                                         curr_pad_source_mask,
                                         prev_gba_keys,
                                         mapped_keys);
        }
        ctx->input->last_committed_keys = mapped_keys;
        ctx->input->last_logged_source_mask = curr_pad_source_mask;
        PsAppInput_ClearReleasePending(ctx->input_ctx);
    }

    PsAppRuntimeFeature_ServiceFast(ctx);
}

static void PsAppRuntime_ServiceSlow(PsAppRuntimeContext *ctx) {
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
    u32 ps_btn_mask;
    u32 pressed_ps_btns;

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

    PsAppRuntime_ServicePcGuard(ctx, status1, dbg_pc, dbg_mem, dbg_dma);

    if (ctx->diag->last_physical_keys != physical_keys) {
        PsAppRuntime_PrintPhysicalKeys(physical_keys);
        ctx->diag->last_physical_keys = physical_keys;
    }
    ps_btn_mask = PsAppRuntime_ReadPsButtonMask(ctx);
    if ((ctx->ps_gpio_ready != 0U) && (ctx->ps_btn_last_mask != ps_btn_mask)) {
        pressed_ps_btns = ps_btn_mask & ~ctx->ps_btn_last_mask;
        if ((pressed_ps_btns & PS_APP_BTN4_MASK) != 0U) {
            u32 next_volume = (u32)ctx->audio->volume + PS_APP_AUDIO_VOLUME_STEP;
            if (PsAppRuntime_SetAudioVolume(ctx, next_volume) == XST_SUCCESS) {
                xil_printf("[AUDIO] volume=%u mute=%u\r\n",
                           (unsigned int)ctx->audio->volume,
                           (unsigned int)((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)));
            }
        }
        if ((pressed_ps_btns & PS_APP_BTN5_MASK) != 0U) {
            u32 next_volume = (ctx->audio->volume > PS_APP_AUDIO_VOLUME_STEP) ?
                              ((u32)ctx->audio->volume - PS_APP_AUDIO_VOLUME_STEP) : 0U;
            if (PsAppRuntime_SetAudioVolume(ctx, next_volume) == XST_SUCCESS) {
                xil_printf("[AUDIO] volume=%u mute=%u\r\n",
                           (unsigned int)ctx->audio->volume,
                           (unsigned int)((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)));
            }
        }
        PsAppRuntime_PrintPsButtons(ps_btn_mask);
        ctx->ps_btn_last_mask = ps_btn_mask;
    }

    PsAppHdmiLink_Service(ctx->hdmi_ctx);
    if ((ctx->hdmi != NULL) && (ctx->hdmi->blank_frame_pending != 0U)) {
        (void)PsHdmiVdma_FillAllFrames(ctx->vdma, 0x00000000U);
        (void)PsAppVideo_RequestFrame(ctx->video_ctx, 0U);
        PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
        ctx->hdmi->blank_frame_pending = 0U;
    }

    PsAppDiag_ServiceDdrLog(ctx->diag_ctx, status1, dbg_pc, dbg_mem, dbg_dma);

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

#if 0
    /* DDR_SAFE 自动切入已禁用：实测表明 burst=1 反而加速场景转换卡死，
     * 因为场景转换以顺序 ROM 读取为主，burst=4 的预取能显著减少 AXI 事务数。
     * 根因修复方向：增大 gamepak cache SIZE 降低冲突缺失率。 */
    {
        u8 ddr_safe_needed = 0U;
        u32 miss_delta_threshold = 4000U;
        u32 miss_samples_threshold = 40U;
        u32 miss_delta;

        if (ctx->diag->auto_stall_audit_printed != 0U) {
            ddr_safe_needed = 1U;
        }

        if (ctx->diag->miss_last_cycles == 0U) {
            miss_delta = 0U;
        } else if (cycles_missing >= ctx->diag->miss_last_cycles) {
            miss_delta = cycles_missing - ctx->diag->miss_last_cycles;
        } else {
            miss_delta = (0x4000U - ctx->diag->miss_last_cycles) + cycles_missing;
        }
        ctx->diag->miss_last_cycles = cycles_missing;

        if (miss_delta > miss_delta_threshold) {
            if (ctx->diag->miss_high_sample_count < 0xFFFFFFFFU) {
                ctx->diag->miss_high_sample_count++;
            }
        } else {
            if (ctx->diag->miss_high_sample_count > 0U) {
                ctx->diag->miss_high_sample_count--;
            }
        }

        if (ctx->diag->miss_high_sample_count >= miss_samples_threshold) {
            ddr_safe_needed = 1U;
        }

        if ((ddr_safe_needed != 0U) &&
            (ctx->diag->rom_ddr_safe_recovery_active == 0U) &&
            ((ctx->config->ctrl & GBA_CTRL_ROM_DDR_SAFE) == 0U)) {
            ctx->config->ctrl |= GBA_CTRL_ROM_DDR_SAFE;
            ctx->diag->rom_ddr_safe_recovery_active = 1U;
            ctx->diag->rom_ddr_safe_recovery_count++;
            ctx->diag->rom_ddr_safe_recovery_tick = ctx->diag->pc_guard_tick;
            xil_printf("[RECOVER] DDR_SAFE engaged count=%u tick=%u miss_delta=%u stall=%u\r\n",
                       (unsigned int)ctx->diag->rom_ddr_safe_recovery_count,
                       (unsigned int)ctx->diag->rom_ddr_safe_recovery_tick,
                       (unsigned int)miss_delta,
                       (unsigned int)ctx->diag->auto_stall_audit_printed);
            PsAppRuntime_ApplyShadowConfig(ctx);
        }
    }
#endif

    PsAppVideo_AttemptRecover(ctx->video_ctx, vdma_status, vdma_errs);

    /* 早期 BRAM 捕获诊断：ROM 加载后约 1 秒触发一次 */
    if (ctx->rom->loaded && ctx->diag->delayed_chain_printed == 0U &&
        ctx->diag->delayed_chain_tick >= 80U && ctx->diag->delayed_chain_tick <= 82U) {
        u32 cap_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
        u32 cap_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
        u32 bram_w0 = PsAppVideo_ReadCaptureWord(cap_sts & 0x1U, 0U);
        u32 bram_w1 = PsAppVideo_ReadCaptureWord(cap_sts & 0x1U, 1U);
        u32 ctrl_rb = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
        xil_printf("[CAPDIAG] seq=%u buf=%u w0=0x%08x w1=0x%08x ctrl=0x%08x last_seq=%u vdma_sr=0x%08x\r\n",
                   (unsigned int)cap_seq,
                   (unsigned int)(cap_sts & 0x1U),
                   (unsigned int)bram_w0,
                   (unsigned int)bram_w1,
                   (unsigned int)ctrl_rb,
                   (unsigned int)ctx->video->fbcap_last_frame_seq,
                   (unsigned int)vdma_status);
    }

    if (ctx->rom->loaded && ctx->diag->delayed_chain_printed < 2U) {
        ctx->diag->delayed_chain_tick++;
        if ((ctx->diag->delayed_chain_printed == 0U && ctx->diag->delayed_chain_tick >= 500U) ||
            (ctx->diag->delayed_chain_printed == 1U && ctx->diag->delayed_chain_tick >= 1500U)) {
            xil_printf("[AUTO] delayed chain snapshot @tick=%u\r\n",
                       (unsigned int)ctx->diag->delayed_chain_tick);
            PsAppDiag_PrintRuntimeSample(ctx->diag_ctx, "delayed");
            PsAppDiag_PrintChainSnapshot(ctx->diag_ctx, "delayed");
            PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "delayed");
            ctx->diag->delayed_chain_printed++;
        }
    }

    if (ctx->rom->loaded &&
        (ctx->diag->log_fbscan_auto_enable != 0U) &&
        (ctx->diag->delayed_chain_tick > 200U)) {
        ctx->diag->fbscan_tick++;
        if ((ctx->diag->fbscan_tick % PS_APP_FBSCAN_INTERVAL_TICKS) == 0U) {
            if (ctx->diag->fbscan_dump_count < PS_APP_FBSCAN_MAX_DUMPS) {
                PsAppDiag_ScanFramebufferForAnomaly(ctx->diag_ctx, "auto", 0U);
            }
        }
    }

    PsAppRuntimeFeature_ServiceSlow(ctx);
}

void PsAppRuntime_Service(PsAppRuntimeContext *ctx) {
    PsAppRuntime_ServiceInputFast(ctx);
    PsAppRuntime_ServiceSlow(ctx);
}

void PsAppMonitorTask(void *arg) {
    PsAppRuntimeContext *ctx = (PsAppRuntimeContext *)arg;
    TickType_t fast_delay_ticks;
    u32 slow_divider;
    u32 slow_tick_accum;

    fast_delay_ticks = pdMS_TO_TICKS(PS_APP_INPUT_SERVICE_INTERVAL_MS);
    if (fast_delay_ticks == 0U) {
        fast_delay_ticks = 1U;
    }
    slow_divider = PS_APP_MONITOR_INTERVAL_MS / PS_APP_INPUT_SERVICE_INTERVAL_MS;
    if (slow_divider == 0U) {
        slow_divider = 1U;
    }
    slow_tick_accum = slow_divider;

    for (;;) {
        /* 输入快路径每 1ms 跑一次，尽量贴近 USB IN 报告节拍；
         * 其余监控/诊断仍按原来的 10ms 慢节拍执行，避免打乱既有超时基准。 */
        PsAppRuntime_ServiceInputFast(ctx);
        if (slow_tick_accum >= slow_divider) {
            PsAppRuntime_ServiceSlow(ctx);
            slow_tick_accum = 0U;
        }
        slow_tick_accum++;
        vTaskDelay(fast_delay_ticks);
    }
}

void PsAppSaveTask(void *arg) {
    /* 关键防坑：
     * 这里使用 static 保存任务上下文，避免在异常栈压力下局部变量被破坏，
     * 进而把无效指针传入保存流程导致“写盘完成后整机卡死”。 */
    TickType_t save_delay_ticks;

    s_ps_app_runtime_save_task_context = (PsAppSaveContext *)arg;
    save_delay_ticks = pdMS_TO_TICKS(PS_APP_MONITOR_INTERVAL_MS);
    if (save_delay_ticks == 0U) {
        save_delay_ticks = 1U;
    }

    for (;;) {
        PsAppSave_Service(s_ps_app_runtime_save_task_context);
        vTaskDelay(save_delay_ticks);
    }
}

void PsAppVideoPresentTask(void *arg) {
    PsAppRuntimeContext *ctx = (PsAppRuntimeContext *)arg;
    TickType_t present_delay_ticks;

    present_delay_ticks = pdMS_TO_TICKS(PS_APP_VIDEO_PRESENT_INTERVAL_MS);
    if (present_delay_ticks == 0U) {
        present_delay_ticks = 1U;
    }

    for (;;) {
        if ((ctx != NULL) &&
            (ctx->rom != NULL) &&
            (ctx->rom->loaded != 0U) &&
            ((ctx->hdmi == NULL) || (ctx->hdmi->present_enable != 0U))) {
            PsAppVideo_PresentCapturedFrameIfReady(ctx->video_ctx);
        }
        vTaskDelay(present_delay_ticks);
    }
}
