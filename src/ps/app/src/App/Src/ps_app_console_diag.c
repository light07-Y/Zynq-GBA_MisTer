#include "App/Src/ps_app_console_internal.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xaxivdma_hw.h"
#include "xil_printf.h"
#include "xiltimer.h"

#include "Diagnostics/Inc/ps_app_diag.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Video/Inc/ps_app_video.h"

static u32 PsAppConsole_TicksToMs(u64 ticks) {
#if defined(COUNTS_PER_SECOND) && (COUNTS_PER_SECOND != 0)
    u64 scaled = (ticks * 1000ULL) + ((u64)COUNTS_PER_SECOND / 2ULL);
    return (u32)(scaled / (u64)COUNTS_PER_SECOND);
#else
    return (u32)ticks;
#endif
}

static u32 PsAppConsole_DeltaMasked(u32 start, u32 end, u32 mask) {
    return (end - start) & mask;
}

static u32 PsAppConsole_ReadDisplayFrameIdx(PsAppRuntimeContext *app) {
    u32 frame_idx;

    if ((app == NULL) ||
        (app->vdma == NULL) ||
        (app->vdma->is_ready == 0U) ||
        (app->vdma->frame_count == 0U)) {
        return 0U;
    }

    frame_idx = XAxiVdma_CurrFrameStore(&app->vdma->vdma, XAXIVDMA_READ);
    if (frame_idx >= app->vdma->frame_count) {
        frame_idx = 0U;
    }
    return frame_idx;
}

static void PsAppConsole_RunFpsTest(PsAppConsoleContext *ctx, u32 duration_ms, u32 sample_ms) {
    PsAppRuntimeContext *app;
    u32 start_status1;
    u32 end_status1;
    u32 start_miss;
    u32 end_miss;
    u32 start_cap_seq;
    u32 end_cap_seq;
    u32 delta_cap_seq;
    u32 start_blit_count;
    u32 end_blit_count;
    u32 delta_blit_count;
    u32 start_blit_total_us;
    u32 end_blit_total_us;
    u32 delta_blit_total_us;
    u32 start_display_frame;
    u32 last_display_frame;
    u32 end_display_frame;
    u32 delta_display_flips;
    u32 elapsed_ms;
    u32 sample_ticks;
    u32 effective_sample_ms;
    u32 display_fps_x100;
    u32 cap_fps_x100;
    u32 blit_fps_x100;
    u32 present_ratio_x10;
    u32 window_blit_avg_us;
    XTime tick_begin;
    XTime tick_now;

    if ((ctx == NULL) || (ctx->runtime == NULL)) {
        return;
    }

    app = ctx->runtime;
    if (duration_ms == 0U) {
        duration_ms = 5000U;
    }
    if (sample_ms == 0U) {
        sample_ms = (u32)portTICK_PERIOD_MS;
    }
    if (sample_ms > duration_ms) {
        sample_ms = duration_ms;
    }
    if ((app->vdma == NULL) || (app->vdma->is_ready == 0U) || (app->vdma->frame_count == 0U)) {
        xil_printf("[FPS] hdmi vdma not ready\r\n");
        return;
    }

    start_status1 = PsGbaRegs_Read(app->regs, GBA_REG_STATUS1);
    start_miss = (start_status1 >> 2) & 0x3FFFU;
    start_cap_seq = PsGbaRegs_Read(app->regs, GBA_REG_FB_CAP_SEQ);
    start_blit_count = app->video->blit_count;
    start_blit_total_us = app->video->blit_total_us;
    start_display_frame = PsAppConsole_ReadDisplayFrameIdx(app);
    last_display_frame = start_display_frame;
    delta_display_flips = 0U;
    XTime_GetTime(&tick_begin);

    sample_ticks = pdMS_TO_TICKS(sample_ms);
    if (sample_ticks == 0U) {
        sample_ticks = 1U;
    }
    effective_sample_ms = sample_ticks * (u32)portTICK_PERIOD_MS;
    if (effective_sample_ms == 0U) {
        effective_sample_ms = sample_ms;
    }

    xil_printf("[FPS] test start mode=display window=%u ms sample_req=%u ms sample_eff=%u ms\r\n",
               (unsigned int)duration_ms,
               (unsigned int)sample_ms,
               (unsigned int)effective_sample_ms);

    do {
        u32 current_display_frame;

        vTaskDelay(sample_ticks);
        current_display_frame = PsAppConsole_ReadDisplayFrameIdx(app);
        if (current_display_frame != last_display_frame) {
            delta_display_flips++;
            last_display_frame = current_display_frame;
        }
        XTime_GetTime(&tick_now);
        elapsed_ms = PsAppConsole_TicksToMs((u64)(tick_now - tick_begin));
    } while (elapsed_ms < duration_ms);

    end_status1 = PsGbaRegs_Read(app->regs, GBA_REG_STATUS1);
    end_miss = (end_status1 >> 2) & 0x3FFFU;
    end_cap_seq = PsGbaRegs_Read(app->regs, GBA_REG_FB_CAP_SEQ);
    end_blit_count = app->video->blit_count;
    end_blit_total_us = app->video->blit_total_us;
    end_display_frame = last_display_frame;
    if (elapsed_ms == 0U) {
        elapsed_ms = 1U;
    }

    delta_cap_seq = end_cap_seq - start_cap_seq;
    delta_blit_count = end_blit_count - start_blit_count;
    delta_blit_total_us = end_blit_total_us - start_blit_total_us;

    display_fps_x100 = (u32)(((u64)delta_display_flips * 100000ULL + (u64)(elapsed_ms / 2U)) / (u64)elapsed_ms);
    cap_fps_x100 = (u32)(((u64)delta_cap_seq * 100000ULL + (u64)(elapsed_ms / 2U)) / (u64)elapsed_ms);
    blit_fps_x100 = (u32)(((u64)delta_blit_count * 100000ULL + (u64)(elapsed_ms / 2U)) / (u64)elapsed_ms);
    present_ratio_x10 = (delta_cap_seq != 0U) ?
                        (u32)(((u64)delta_blit_count * 1000ULL + (u64)(delta_cap_seq / 2U)) / (u64)delta_cap_seq) :
                        0U;
    window_blit_avg_us = (delta_blit_count != 0U) ? (delta_blit_total_us / delta_blit_count) : 0U;

    xil_printf("[FPS] elapsed=%u ms display_flip_delta=%u frame_begin=%u frame_end=%u cap_delta=%u blit_delta=%u miss_delta=%u\r\n",
               (unsigned int)elapsed_ms,
               (unsigned int)delta_display_flips,
               (unsigned int)start_display_frame,
               (unsigned int)end_display_frame,
               (unsigned int)delta_cap_seq,
               (unsigned int)delta_blit_count,
               (unsigned int)PsAppConsole_DeltaMasked(start_miss, end_miss, 0x3FFFU));
    xil_printf("[FPS] hdmi_fps=%u.%02u cap_fps=%u.%02u blit_fps=%u.%02u present=%u.%1u%% blit_avg_window_us=%u seq_gap_max=%u\r\n",
               (unsigned int)(display_fps_x100 / 100U),
               (unsigned int)(display_fps_x100 % 100U),
               (unsigned int)(cap_fps_x100 / 100U),
               (unsigned int)(cap_fps_x100 % 100U),
               (unsigned int)(blit_fps_x100 / 100U),
               (unsigned int)(blit_fps_x100 % 100U),
               (unsigned int)(present_ratio_x10 / 10U),
               (unsigned int)(present_ratio_x10 % 10U),
               (unsigned int)window_blit_avg_us,
               (unsigned int)app->video->blit_seq_gap_max);
}

#define CACHEPROF_BITMAP_WORDS 256U  /* 256*32 = 8192 bits */

static void PsAppConsole_RunCacheProf(PsAppConsoleContext *ctx, u32 duration_ms) {
    PsAppRuntimeContext *app;
    u32 bitmap[CACHEPROF_BITMAP_WORDS];
    u32 rom_samples   = 0U;
    u32 other_samples = 0U;
    u32 unique_sets   = 0U;
    XTime tick_start;
    XTime tick_end;
    u64 elapsed_us;

    if ((ctx == NULL) || (ctx->runtime == NULL)) return;
    app = ctx->runtime;
    if ((duration_ms == 0U) || (duration_ms > 30000U)) duration_ms = 5000U;

    for (u32 i = 0U; i < CACHEPROF_BITMAP_WORDS; i++) bitmap[i] = 0U;

    xil_printf("[CACHEPROF] sampling %ums (free-running read loop)\r\n",
               (unsigned)duration_ms);

    XTime_GetTime(&tick_start);
    tick_end = tick_start;

    do {
        u32 pc = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_PC);
        if ((pc >= 0x08000000U) && (pc < 0x0A000000U)) {
            u32 set = ((pc >> 2U) >> 1U) & 0x1FFFU;
            bitmap[set >> 5U] |= (1U << (set & 31U));
            rom_samples++;
        } else {
            other_samples++;
        }
        XTime_GetTime(&tick_end);
        elapsed_us = ((tick_end - tick_start) * 1000000ULL) / (u64)COUNTS_PER_SECOND;
    } while (elapsed_us < (u64)duration_ms * 1000ULL);

    for (u32 i = 0U; i < CACHEPROF_BITMAP_WORDS; i++) {
        u32 w = bitmap[i];
        while (w != 0U) { w &= (w - 1U); unique_sets++; }
    }

    xil_printf("[CACHEPROF] elapsed=%llu us rom_samples=%u other=%u sets=%u/8192 coverage=%.1f%%\r\n",
               elapsed_us, (unsigned)rom_samples, (unsigned)other_samples,
               (unsigned)unique_sets, ((float)unique_sets * 100.0f) / 8192.0f);
}
#undef CACHEPROF_BITMAP_WORDS

void PsAppConsole_PrintDiagnosticHelp(void) {
    xil_printf("  status\r\n");
    xil_printf("  diag\r\n");
    xil_printf("  probe\r\n");
    xil_printf("  chain\r\n");
    xil_printf("  audit\r\n");
    xil_printf("  fbscan\r\n");
    xil_printf("  log input on|off\r\n");
    xil_printf("  log fbscan on|off\r\n");
    xil_printf("  log ddr on|off [interval_ticks]\r\n");
    xil_printf("  log status\r\n");
    xil_printf("  guard status|on|off|clear\r\n");
    xil_printf("  ddrlog once\r\n");
    xil_printf("  pixcap summary [buf]\r\n");
    xil_printf("  pixcap row <y> [buf]\r\n");
    xil_printf("  pixcap cmp <x> <y> [buf] [frame]\r\n");
    xil_printf("  trace [samples] [interval_ms]\r\n");
    xil_printf("  hangsnap [samples] [interval_ms]\r\n");
    xil_printf("  fps [seconds] [sample_ms]\r\n");
    xil_printf("  cacheprof [duration_ms]\r\n");
}

u8 PsAppConsole_HandleDiagnosticCommands(PsAppConsoleContext *ctx, const char *cmd) {
    PsAppRuntimeContext *app;
    PsAppVideoContext *video;
    PsAppDiagContext *diag;
    char *arg1;
    char *arg2;
    char *arg3;
    char *arg4;
    char *arg5;
    u32 value;

    if ((ctx == NULL) || (ctx->runtime == NULL) || (cmd == NULL)) {
        return 0U;
    }

    app = ctx->runtime;
    video = ctx->video;
    diag = ctx->diag;

    if (strcmp(cmd, "status") == 0) {
        PsAppDiag_PrintStatus(diag);
        return 1U;
    }
    if (strcmp(cmd, "diag") == 0) {
        PsAppDiag_PrintDiag(diag);
        return 1U;
    }
    if (strcmp(cmd, "probe") == 0) {
        PsAppDiag_PrintProbe(diag);
        return 1U;
    }
    if (strcmp(cmd, "chain") == 0) {
        PsAppDiag_PrintChainSnapshot(diag, "cmd");
        return 1U;
    }
    if (strcmp(cmd, "audit") == 0) {
        PsAppDiag_PrintConfigReadback(diag, "cmd");
        PsAppDiag_PrintDiag(diag);
        PsAppDiag_PrintChainSnapshot(diag, "cmd");
        PsAppDiag_PrintProbe(diag);
        return 1U;
    }
    if (strcmp(cmd, "fbscan") == 0) {
        PsAppDiag_ScanFramebufferForAnomaly(diag, "cmd", 1U);
        return 1U;
    }
    if (strcmp(cmd, "guard") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 == NULL) || (strcmp(arg1, "status") == 0)) {
            xil_printf("[CMD] guard enable=%u active=%u cooldown=%u hold=%u recoveries=%u streak=%u last_bad_pc=0x%08x last_bad_mem=0x%08x last_bad_dma=0x%08x last_bad_status1=0x%08x\r\n",
                       (unsigned int)(app->diag->pc_guard_enable != 0U),
                       (unsigned int)(app->diag->pc_guard_recovery_active != 0U),
                       (unsigned int)app->diag->pc_guard_cooldown_ticks,
                       (unsigned int)app->diag->pc_guard_reset_hold_ticks,
                       (unsigned int)app->diag->pc_guard_recovery_count,
                       (unsigned int)app->diag->pc_guard_invalid_streak,
                       (unsigned int)app->diag->pc_guard_last_bad_pc,
                       (unsigned int)app->diag->pc_guard_last_bad_mem,
                       (unsigned int)app->diag->pc_guard_last_bad_dma,
                       (unsigned int)app->diag->pc_guard_last_bad_status1);
            return 1U;
        }

        if ((strcmp(arg1, "on") == 0) || (strcmp(arg1, "off") == 0)) {
            app->diag->pc_guard_enable = (u8)((strcmp(arg1, "on") == 0) ? 1U : 0U);
            if (app->diag->pc_guard_enable == 0U) {
                if (app->diag->pc_guard_recovery_active != 0U) {
                    PsGbaRegs_SetSwReset(app->regs, 0U);
                    app->config->ctrl |= (GBA_CTRL_CORE_ON | PS_APP_GBA_CTRL_BOOT_REQUIRED);
                    app->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
                    PsAppRuntime_ApplyShadowConfig(app);
                }
                if ((app->diag->pc_guard_audio_restore_pending != 0U) &&
                    (app->audio->mute != 0U)) {
                    app->audio->mute = 0U;
                    (void)PsAppRuntime_ApplyAudioOutputState(app);
                }
                app->diag->pc_guard_recovery_active = 0U;
                app->diag->pc_guard_audio_restore_pending = 0U;
                app->diag->pc_guard_invalid_streak = 0U;
                app->diag->pc_guard_cooldown_ticks = 0U;
                app->diag->pc_guard_reset_hold_ticks = 0U;
            }
            xil_printf("[CMD] guard=%s\r\n",
                       (app->diag->pc_guard_enable != 0U) ? "on" : "off");
            return 1U;
        }

        if (strcmp(arg1, "clear") == 0) {
            if (app->diag->pc_guard_recovery_active != 0U) {
                PsGbaRegs_SetSwReset(app->regs, 0U);
                app->config->ctrl |= (GBA_CTRL_CORE_ON | PS_APP_GBA_CTRL_BOOT_REQUIRED);
                app->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
                PsAppRuntime_ApplyShadowConfig(app);
            }
            if ((app->diag->pc_guard_audio_restore_pending != 0U) &&
                (app->audio->mute != 0U)) {
                app->audio->mute = 0U;
                (void)PsAppRuntime_ApplyAudioOutputState(app);
            }
            app->diag->pc_guard_recovery_active = 0U;
            app->diag->pc_guard_audio_restore_pending = 0U;
            app->diag->pc_guard_invalid_streak = 0U;
            app->diag->pc_guard_cooldown_ticks = 0U;
            app->diag->pc_guard_reset_hold_ticks = 0U;
            app->diag->pc_guard_last_bad_pc = 0U;
            app->diag->pc_guard_last_bad_status1 = 0U;
            app->diag->pc_guard_last_bad_mem = 0U;
            app->diag->pc_guard_last_bad_dma = 0U;
            app->diag->pc_guard_last_trigger_tick = 0U;
            xil_printf("[CMD] guard counters cleared\r\n");
            return 1U;
        }

        xil_printf("[CMD] usage: guard status|on|off|clear\r\n");
        return 1U;
    }

    if (strcmp(cmd, "log") == 0) {
        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");

        if ((arg1 == NULL) || (strcmp(arg1, "status") == 0)) {
            xil_printf("[CMD] log input=%s fbscan=%s ddr=%s interval_ticks=%u\r\n",
                       (app->diag->log_input_delta_enable != 0U) ? "on" : "off",
                       (app->diag->log_fbscan_auto_enable != 0U) ? "on" : "off",
                       (app->diag->log_ddr_enable != 0U) ? "on" : "off",
                       (unsigned int)app->diag->ddrlog_interval_ticks);
            return 1U;
        }

        if (strcmp(arg1, "input") == 0) {
            if (PsAppConsole_ParseOnOff(arg2, &value) != 0) {
                xil_printf("[CMD] usage: log input on|off\r\n");
                return 1U;
            }
            app->diag->log_input_delta_enable = (u8)(value & 0x1U);
            xil_printf("[CMD] log input=%s\r\n",
                       (app->diag->log_input_delta_enable != 0U) ? "on" : "off");
            return 1U;
        }

        if (strcmp(arg1, "fbscan") == 0) {
            if (PsAppConsole_ParseOnOff(arg2, &value) != 0) {
                xil_printf("[CMD] usage: log fbscan on|off\r\n");
                return 1U;
            }
            app->diag->log_fbscan_auto_enable = (u8)(value & 0x1U);
            if (app->diag->log_fbscan_auto_enable == 0U) {
                app->diag->fbscan_tick = 0U;
            }
            xil_printf("[CMD] log fbscan=%s\r\n",
                       (app->diag->log_fbscan_auto_enable != 0U) ? "on" : "off");
            return 1U;
        }

        if (strcmp(arg1, "ddr") == 0) {
            u32 interval_ticks;
            arg3 = strtok(NULL, " \t");
            if (PsAppConsole_ParseOnOff(arg2, &value) != 0) {
                xil_printf("[CMD] usage: log ddr on|off [interval_ticks]\r\n");
                return 1U;
            }

            interval_ticks = app->diag->ddrlog_interval_ticks;
            if (arg3 != NULL) {
                if ((PsAppConsole_ParseU32Value(arg3, &interval_ticks) != 0) ||
                    (interval_ticks < PS_APP_DDRLOG_MIN_INTERVAL_TICKS) ||
                    (interval_ticks > PS_APP_DDRLOG_MAX_INTERVAL_TICKS)) {
                    xil_printf("[CMD] range: interval_ticks=%u..%u\r\n",
                               (unsigned int)PS_APP_DDRLOG_MIN_INTERVAL_TICKS,
                               (unsigned int)PS_APP_DDRLOG_MAX_INTERVAL_TICKS);
                    return 1U;
                }
            }
            if (interval_ticks < PS_APP_DDRLOG_MIN_INTERVAL_TICKS) {
                interval_ticks = PS_APP_DDRLOG_DEFAULT_INTERVAL_TICKS;
            }

            app->diag->log_ddr_enable = (u8)(value & 0x1U);
            app->diag->ddrlog_interval_ticks = interval_ticks;
            app->diag->ddrlog_tick = 0U;
            app->diag->ddrlog_last_status1 = 0U;
            app->diag->ddrlog_last_pc = 0U;
            app->diag->ddrlog_last_mem = 0U;
            app->diag->ddrlog_last_dma = 0U;
            app->diag->ddrlog_last_err = 0U;
            app->diag->ddrlog_last_counts0 = 0U;
            app->diag->ddrlog_last_counts1 = 0U;
            app->diag->ddrlog_same_sample_count = 0U;
            xil_printf("[CMD] log ddr=%s interval_ticks=%u\r\n",
                       (app->diag->log_ddr_enable != 0U) ? "on" : "off",
                       (unsigned int)app->diag->ddrlog_interval_ticks);
            return 1U;
        }

        xil_printf("[CMD] usage: log input on|off | log fbscan on|off | log ddr on|off [interval_ticks] | log status\r\n");
        return 1U;
    }

    if (strcmp(cmd, "ddrlog") == 0) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 == NULL) || (strcmp(arg1, "once") == 0)) {
            PsAppDiag_PrintDdrLogSnapshot(diag, "cmd");
            return 1U;
        }
        xil_printf("[CMD] usage: ddrlog once\r\n");
        return 1U;
    }

    if (strcmp(cmd, "pixcap") == 0) {
        u32 buf_idx;
        u32 frame_idx;

        arg1 = strtok(NULL, " \t");
        if (arg1 == NULL) {
            xil_printf("[CMD] usage: pixcap summary [buf] | row <y> [buf] | cmp <x> <y> [buf] [frame]\r\n");
            return 1U;
        }

        if (strcmp(arg1, "summary") == 0) {
            arg2 = strtok(NULL, " \t");
            buf_idx = PsAppVideo_DefaultCaptureBuffer(video);
            if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap summary [buf]\r\n");
                return 1U;
            }
            PsAppDiag_PrintPixcapSummary(diag, buf_idx & 0x1U);
            return 1U;
        }

        if (strcmp(arg1, "row") == 0) {
            arg2 = strtok(NULL, " \t");
            arg3 = strtok(NULL, " \t");
            if ((arg2 == NULL) || (PsAppConsole_ParseU32Value(arg2, &value) != 0) ||
                (value >= PS_APP_GBA_FRAME_HEIGHT)) {
                xil_printf("[CMD] usage: pixcap row <y> [buf]\r\n");
                return 1U;
            }
            buf_idx = PsAppVideo_DefaultCaptureBuffer(video);
            if ((arg3 != NULL) && (PsAppConsole_ParseU32Value(arg3, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap row <y> [buf]\r\n");
                return 1U;
            }
            PsAppDiag_PrintPixcapRow(diag, buf_idx & 0x1U, value);
            return 1U;
        }

        if (strcmp(arg1, "cmp") == 0) {
            u32 x;
            u32 y;

            arg2 = strtok(NULL, " \t");
            arg3 = strtok(NULL, " \t");
            arg4 = strtok(NULL, " \t");
            arg5 = strtok(NULL, " \t");
            if ((arg2 == NULL) || (arg3 == NULL) ||
                (PsAppConsole_ParseU32Value(arg2, &x) != 0) ||
                (PsAppConsole_ParseU32Value(arg3, &y) != 0) ||
                (x >= PS_APP_GBA_FRAME_WIDTH) || (y >= PS_APP_GBA_FRAME_HEIGHT)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return 1U;
            }
            buf_idx = PsAppVideo_DefaultCaptureBuffer(video);
            if ((arg4 != NULL) && (PsAppConsole_ParseU32Value(arg4, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return 1U;
            }
            frame_idx = PsAppVideo_DefaultFrameIndex(video);
            if ((arg5 != NULL) && (PsAppConsole_ParseU32Value(arg5, &frame_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return 1U;
            }
            if ((app->vdma->is_ready == 0U) || (frame_idx >= app->vdma->frame_count)) {
                xil_printf("[CMD] pixcap cmp: invalid frame idx\r\n");
                return 1U;
            }
            PsAppDiag_PrintPixcapCompare(diag, buf_idx & 0x1U, frame_idx, x, y);
            return 1U;
        }

        xil_printf("[CMD] usage: pixcap summary [buf] | row <y> [buf] | cmp <x> <y> [buf] [frame]\r\n");
        return 1U;
    }

    if (strcmp(cmd, "trace") == 0) {
        u32 samples = PS_APP_TRACE_DEFAULT_SAMPLES;
        u32 interval_ms = PS_APP_TRACE_DEFAULT_INTERVAL_MS;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &samples) != 0)) {
            xil_printf("[CMD] usage: trace [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: trace [samples] [interval_ms]\r\n");
            return 1U;
        }
        PsAppDiag_PrintTrace(diag, samples, interval_ms);
        return 1U;
    }

    if (strcmp(cmd, "hangsnap") == 0) {
        u32 samples = 8U;
        u32 interval_ms = 20U;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &samples) != 0)) {
            xil_printf("[CMD] usage: hangsnap [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: hangsnap [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((samples == 0U) || (samples > PS_APP_TRACE_MAX_SAMPLES) ||
            (interval_ms > 5000U)) {
            xil_printf("[CMD] range: samples=1..%u interval_ms=0..5000\r\n",
                       (unsigned int)PS_APP_TRACE_MAX_SAMPLES);
            return 1U;
        }

        PsAppDiag_PrintHangSnapshot(diag, samples, interval_ms);
        return 1U;
    }

    if (strcmp(cmd, "fps") == 0) {
        u32 seconds = 5U;
        u32 sample_ms = (u32)portTICK_PERIOD_MS;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (strcmp(arg1, "test") == 0)) {
            arg1 = arg2;
            arg2 = strtok(NULL, " \t");
        }

        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &seconds) != 0)) {
            xil_printf("[CMD] usage: fps [seconds] [sample_ms]\r\n");
            return 1U;
        }
        if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &sample_ms) != 0)) {
            xil_printf("[CMD] usage: fps [seconds] [sample_ms]\r\n");
            return 1U;
        }
        if ((seconds == 0U) || (seconds > 120U) || (sample_ms == 0U) || (sample_ms > 5000U)) {
            xil_printf("[CMD] range: seconds=1..120 sample_ms=1..5000\r\n");
            return 1U;
        }

        PsAppConsole_RunFpsTest(ctx, seconds * 1000U, sample_ms);
        return 1U;
    }

    if (strcmp(cmd, "cacheprof") == 0) {
        u32 duration_ms = 5000U;
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &duration_ms) != 0)) {
            xil_printf("[CMD] usage: cacheprof [duration_ms]\r\n");
            return 1U;
        }
        if ((duration_ms == 0U) || (duration_ms > 30000U)) {
            xil_printf("[CMD] range: duration_ms=1..30000\r\n");
            return 1U;
        }
        PsAppConsole_RunCacheProf(ctx, duration_ms);
        return 1U;
    }

    return 0U;
}
