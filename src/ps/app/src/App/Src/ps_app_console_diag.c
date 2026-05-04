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

#define PS_APP_CPUSET_PC_START 0x08056166U
#define PS_APP_CPUSET_PC_END   0x08056184U
#define PS_APP_VRAM_STATE_WRITE      21U
#define PS_APP_VRAM_STATE_WAITWRITE  22U

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

static const char *PsAppConsole_PcRegionName(u32 pc) {
    if (pc < 0x00004000U) {
        return "BIOS";
    }
    if ((pc >= 0x03000000U) && (pc <= 0x03007FFFU)) {
        return "IWRAM";
    }
    if ((pc >= 0x08000000U) && (pc <= 0x0DFFFFFFU)) {
        return "ROM";
    }
    return "OTHER";
}

static const char *PsAppConsole_MemStateName(u32 state) {
    switch (state) {
        case 0U:  return "IDLE";
        case 1U:  return "READBIOS";
        case 2U:  return "READSMALLRAM";
        case 11U: return "WAIT_SDRAM";
        case 14U: return "ROTATE";
        case PS_APP_VRAM_STATE_WRITE: return "WRITE_VRAM";
        case PS_APP_VRAM_STATE_WAITWRITE: return "VRAMWAITWRITE";
        default: return "OTHER";
    }
}

static void PsAppConsole_PrintVramWhySnapshot(PsAppRuntimeContext *app) {
    u32 regs0;
    u32 regs1;
    u32 regs2;
    u32 regs3;
    u32 regs4;
    u32 hits;
    u32 rings[4];
    u32 dist0;
    u32 dist1;
    u32 dma_tr[9];
    u32 wait0;
    u32 wait1;
    u32 wait2;
    u32 wait3;
    u32 dma_dbg;
    u32 i;

    if (app == NULL) {
        return;
    }

    regs0 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_REGS0);
    regs1 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_REGS1);
    regs2 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_REGS2);
    regs3 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_REGS3);
    regs4 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_REGS4);
    hits  = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_HITS);
    rings[0] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_RING0);
    rings[1] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_RING1);
    rings[2] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_RING2);
    rings[3] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_RING3);
    dist0 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_DIST0);
    dist1 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_DIST1);
    dma_tr[0] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR0);
    dma_tr[1] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR1);
    dma_tr[2] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR2);
    dma_tr[3] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR3);
    dma_tr[4] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR4);
    dma_tr[5] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR5);
    dma_tr[6] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR6);
    dma_tr[7] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR7);
    dma_tr[8] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA_TR8);
    wait0 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_WAIT0);
    wait1 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_WAIT1);
    wait2 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_WAIT2);
    wait3 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_WAIT3);
    dma_dbg = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA);

    xil_printf("[VRREG] last dispcnt=0x%04x bldcnt=0x%04x bg0=0x%04x bg1=0x%04x bg2=0x%04x bg3=0x%04x\r\n",
               (unsigned int)(regs0 & 0xFFFFU),
               (unsigned int)((regs0 >> 16) & 0xFFFFU),
               (unsigned int)(regs1 & 0xFFFFU),
               (unsigned int)((regs1 >> 16) & 0xFFFFU),
               (unsigned int)(regs2 & 0xFFFFU),
               (unsigned int)((regs2 >> 16) & 0xFFFFU));
    xil_printf("[VRREG] cnt dispcnt=%u bldcnt=%u bg0=%u bg1=%u bg2=%u bg3=%u hits_cpu=%u hits_dma=%u\r\n",
               (unsigned int)(regs3 & 0xFFU),
               (unsigned int)((regs3 >> 8) & 0xFFU),
               (unsigned int)((regs3 >> 16) & 0xFFU),
               (unsigned int)((regs3 >> 24) & 0xFFU),
               (unsigned int)(regs4 & 0xFFU),
               (unsigned int)((regs4 >> 8) & 0xFFU),
               (unsigned int)(hits & 0xFFFFU),
               (unsigned int)((hits >> 16) & 0xFFFFU));
    xil_printf("[VRREG] dist pal=%u vram=%u oam=%u\r\n",
               (unsigned int)(dist0 & 0xFFFFU),
               (unsigned int)((dist0 >> 16) & 0xFFFFU),
               (unsigned int)(dist1 & 0xFFFFU));

    for (i = 0U; i < 4U; ++i) {
        u32 raw = rings[i];
        u32 src_dma = (raw >> 31) & 0x1U;
        u32 nz = (raw >> 30) & 0x1U;
        u32 be = (raw >> 26) & 0xFU;
        u32 acc = (raw >> 24) & 0x3U;
        u32 ofs = (raw >> 6) & 0x3FFFFU;
        xil_printf("[VRREG] ring%u src=%s ofs=0x%05x acc=%u be=0x%x nz=%u raw=0x%08x\r\n",
                   (unsigned int)i,
                   (src_dma != 0U) ? "DMA" : "CPU",
                   (unsigned int)ofs,
                   (unsigned int)acc,
                   (unsigned int)be,
                   (unsigned int)nz,
                   (unsigned int)raw);
    }

    for (i = 0U; i < 4U; ++i) {
        u32 start_src = dma_tr[i] & 0x0FFFFFFFU;
        u32 finish_dst = dma_tr[i + 4U] & 0x0FFFFFFFU;
        u32 start_cnt = (dma_tr[8] >> (i * 8U)) & 0xFFU;
        u32 start_valid = (dma_dbg >> (28U + i)) & 0x1U;
        u32 finish_valid = (dma_dbg >> (24U + i)) & 0x1U;
        u32 vram_seen = (dma_dbg >> (20U + i)) & 0x1U;
        xil_printf("[DMATR] ch%u start_src=0x%07x finish_dst=0x%07x start_cnt_l8=%u\r\n",
                   (unsigned int)i,
                   (unsigned int)start_src,
                   (unsigned int)finish_dst,
                   (unsigned int)start_cnt);
        xil_printf("[DMATR] ch%u start=%u finish=%u dst_vram_seen=%u\r\n",
                   (unsigned int)i,
                   (unsigned int)start_valid,
                   (unsigned int)finish_valid,
                   (unsigned int)vram_seen);
    }

    xil_printf("[WAIT] ie_chg=%u if_chg=%u wake_cnt=%u ime_chg=%u last_src=%u flags(ime/irq/dma/idle)=%u%u%u%u\r\n",
               (unsigned int)((wait0 >> 16) & 0xFFFFU),
               (unsigned int)(wait0 & 0xFFFFU),
               (unsigned int)((wait1 >> 16) & 0xFFFFU),
               (unsigned int)((wait1 >> 8) & 0xFFU),
               (unsigned int)((wait1 >> 4) & 0xFU),
               (unsigned int)((wait1 >> 3) & 0x1U),
               (unsigned int)((wait1 >> 2) & 0x1U),
               (unsigned int)((wait1 >> 1) & 0x1U),
               (unsigned int)(wait1 & 0x1U));
    xil_printf("[WAIT] last_ie=0x%04x last_if=0x%04x cur_ie=0x%04x cur_if=0x%04x\r\n",
               (unsigned int)((wait2 >> 16) & 0xFFFFU),
               (unsigned int)(wait2 & 0xFFFFU),
               (unsigned int)((wait3 >> 16) & 0xFFFFU),
               (unsigned int)(wait3 & 0xFFFFU));
}

static void PsAppConsole_RunVramProbe(PsAppConsoleContext *ctx,
                                      u32 samples,
                                      u32 interval_ms,
                                      u8 dump_bundle) {
    PsAppRuntimeContext *app;
    u32 idx;
    u32 first_vreq = 0U;
    u32 first_vwe = 0U;
    u32 first_vblk = 0U;
    u32 first_seq = 0U;
    u32 last_vreq = 0U;
    u32 last_vwe = 0U;
    u32 last_vblk = 0U;
    u32 last_seq = 0U;
    u32 first_pal = 0U;
    u32 first_vram = 0U;
    u32 first_oam = 0U;
    u32 last_pal = 0U;
    u32 last_vram = 0U;
    u32 last_oam = 0U;
    u32 prev_vreq = 0U;
    u32 prev_vwe = 0U;
    u32 prev_vblk = 0U;
    u32 sw_window_hits = 0U;
    u32 hw_window_hits = 0U;
    u32 write_state_hits = 0U;
    u32 waitwrite_hits = 0U;
    u32 rom_pc_hits = 0U;
    u32 iwram_pc_hits = 0U;
    u32 bios_pc_hits = 0U;
    u32 first_seen_set = 0U;

    if ((ctx == NULL) || (ctx->runtime == NULL)) {
        return;
    }
    app = ctx->runtime;

    if (samples == 0U) {
        samples = 16U;
    }
    if (samples > PS_APP_TRACE_MAX_SAMPLES) {
        samples = PS_APP_TRACE_MAX_SAMPLES;
    }
    if (interval_ms > 5000U) {
        interval_ms = 5000U;
    }

    xil_printf("[VRAM] begin samples=%u interval_ms=%u pc_win=0x%08x..0x%08x\r\n",
               (unsigned int)samples,
               (unsigned int)interval_ms,
               (unsigned int)PS_APP_CPUSET_PC_START,
               (unsigned int)PS_APP_CPUSET_PC_END);
    xil_printf("[VRAM] fields: state/miss/fbseq/vreq/vwe/vblk/vnz + cpuset(sw/hw) + iwram(iw_win/iw_seen/iw_cnt)\r\n");

    for (idx = 0U; idx < samples; ++idx) {
        u32 ctrl = PsGbaRegs_Read(app->regs, GBA_REG_CTRL);
        u32 pc = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_PC);
        u32 mix = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_MIX);
        u32 mem = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_MEM);
        u32 dma = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_DMA);
        u32 status1 = PsGbaRegs_Read(app->regs, GBA_REG_STATUS1);
        u32 fbseq = PsGbaRegs_Read(app->regs, GBA_REG_FB_CAP_SEQ);
        u32 fbcap = PsGbaRegs_Read(app->regs, GBA_REG_FB_CAP_STATUS);
        u32 cpuset = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_CPUSET);
        u32 dist0 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_DIST0);
        u32 dist1 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_VRAM_DIST1);
        u32 park = PsAppConsole_ReadDisplayFrameIdx(app);
        u32 state = mem & 0xFFU;
        u32 vreq = (mem >> 8) & 0xFFU;
        u32 vwe = (mem >> 16) & 0xFFU;
        u32 vblk = (mem >> 24) & 0xFU;
        u32 vnz = (mem >> 31) & 0x1U;
        u32 frame = status1 & 0x3U;
        u32 miss = (status1 >> 2) & 0x3FFFU;
        u32 vsync = (status1 >> 16) & 0xFFFFU;
        u32 sw_in_window = ((pc >= PS_APP_CPUSET_PC_START) && (pc <= PS_APP_CPUSET_PC_END)) ? 1U : 0U;
        u32 hw_in_window = (cpuset >> 17) & 0x1U;
        u32 cs_seen = (cpuset >> 16) & 0x1U;
        u32 cs_cnt = cpuset & 0xFFFFU;
        u32 iw_in_window = (cpuset >> 27) & 0x1U;
        u32 iw_seen      = (cpuset >> 26) & 0x1U;
        u32 iw_cnt       = (cpuset >> 18) & 0xFFU;
        u32 d_vreq = 0U;
        u32 d_vwe = 0U;
        u32 d_vblk = 0U;

        if (first_seen_set == 0U) {
            first_vreq = vreq;
            first_vwe = vwe;
            first_vblk = vblk;
            first_seq = fbseq;
            first_pal = dist0 & 0xFFFFU;
            first_vram = (dist0 >> 16) & 0xFFFFU;
            first_oam = dist1 & 0xFFFFU;
            first_seen_set = 1U;
        } else {
            d_vreq = PsAppConsole_DeltaMasked(prev_vreq, vreq, 0xFFU);
            d_vwe = PsAppConsole_DeltaMasked(prev_vwe, vwe, 0xFFU);
            d_vblk = PsAppConsole_DeltaMasked(prev_vblk, vblk, 0x0FU);
        }

        prev_vreq = vreq;
        prev_vwe = vwe;
        prev_vblk = vblk;
        last_vreq = vreq;
        last_vwe = vwe;
        last_vblk = vblk;
        last_seq = fbseq;
        last_pal = dist0 & 0xFFFFU;
        last_vram = (dist0 >> 16) & 0xFFFFU;
        last_oam = dist1 & 0xFFFFU;

        if (sw_in_window != 0U) {
            sw_window_hits++;
        }
        if (hw_in_window != 0U) {
            hw_window_hits++;
        }
        if (state == PS_APP_VRAM_STATE_WRITE) {
            write_state_hits++;
        }
        if (state == PS_APP_VRAM_STATE_WAITWRITE) {
            waitwrite_hits++;
        }
        if (pc < 0x00004000U) {
            bios_pc_hits++;
        } else if ((pc >= 0x03000000U) && (pc <= 0x03007FFFU)) {
            iwram_pc_hits++;
        } else if ((pc >= 0x08000000U) && (pc <= 0x0DFFFFFFU)) {
            rom_pc_hits++;
        }

        xil_printf("[VRAM] i=%u pc=0x%08x region=%s state=0x%02x(%s) thumb=%u mode=0x%x frame=%u miss=%u vsync=%u fbseq=%u fbbuf=%u park=%u vreq=%u dreq=%u vwe=%u dwe=%u vblk=%u dblk=%u vnz=%u sw_win=%u hw_win=%u seen=%u cs_cnt=%u iw_win=%u iw_seen=%u iw_cnt=%u mem=0x%08x dma=0x%08x romsafe=%u\r\n",
                   (unsigned int)idx,
                   (unsigned int)pc,
                   PsAppConsole_PcRegionName(pc),
                   (unsigned int)state,
                   PsAppConsole_MemStateName(state),
                   (unsigned int)((mix >> 5) & 0x1U),
                   (unsigned int)((mix >> 6) & 0xFU),
                   (unsigned int)frame,
                   (unsigned int)miss,
                   (unsigned int)vsync,
                   (unsigned int)fbseq,
                   (unsigned int)(fbcap & 0x1U),
                   (unsigned int)park,
                   (unsigned int)vreq,
                   (unsigned int)d_vreq,
                   (unsigned int)vwe,
                   (unsigned int)d_vwe,
                   (unsigned int)vblk,
                   (unsigned int)d_vblk,
                   (unsigned int)vnz,
                   (unsigned int)sw_in_window,
                   (unsigned int)hw_in_window,
                   (unsigned int)cs_seen,
                   (unsigned int)cs_cnt,
                   (unsigned int)iw_in_window,
                   (unsigned int)iw_seen,
                   (unsigned int)iw_cnt,
                   (unsigned int)mem,
                   (unsigned int)dma,
                   (unsigned int)((ctrl & GBA_CTRL_ROM_DDR_SAFE) != 0U));

        if ((idx + 1U) < samples) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    xil_printf("[VRAM] summary dseq=%u dvreq=%u dvwe=%u dvblk=%u dpal=%u dvram=%u doam=%u sw_hits=%u hw_hits=%u write_state=%u waitwrite_state=%u pc_hits(bios=%u iwram=%u rom=%u)\r\n",
               (unsigned int)PsAppConsole_DeltaMasked(first_seq, last_seq, 0xFFFFFFFFU),
               (unsigned int)PsAppConsole_DeltaMasked(first_vreq, last_vreq, 0xFFU),
               (unsigned int)PsAppConsole_DeltaMasked(first_vwe, last_vwe, 0xFFU),
               (unsigned int)PsAppConsole_DeltaMasked(first_vblk, last_vblk, 0x0FU),
               (unsigned int)PsAppConsole_DeltaMasked(first_pal, last_pal, 0xFFFFU),
               (unsigned int)PsAppConsole_DeltaMasked(first_vram, last_vram, 0xFFFFU),
               (unsigned int)PsAppConsole_DeltaMasked(first_oam, last_oam, 0xFFFFU),
               (unsigned int)sw_window_hits,
               (unsigned int)hw_window_hits,
               (unsigned int)write_state_hits,
               (unsigned int)waitwrite_hits,
               (unsigned int)bios_pc_hits,
               (unsigned int)iwram_pc_hits,
               (unsigned int)rom_pc_hits);

    {
        u32 w0 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_WORD0);
        u32 w1 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_WORD1);
        u32 n0 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_NEXT0);
        u32 n1 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_NEXT1);
        u32 st = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_STAT);
        xil_printf("[VRAM] ddratom w0=0x%08x w1=0x%08x n0=0x%08x n1=0x%08x st=0x%08x sw_d0=%u sw_d1=%u\r\n",
                   (unsigned int)w0,
                   (unsigned int)w1,
                   (unsigned int)n0,
                   (unsigned int)n1,
                   (unsigned int)st,
                   (unsigned int)((w0 != n0) ? 1U : 0U),
                   (unsigned int)((w1 != n1) ? 1U : 0U));
    }

    PsAppConsole_PrintVramWhySnapshot(app);

    if ((dump_bundle != 0U) && (ctx->diag != NULL)) {
        u32 default_buf = 0U;
        if (ctx->video != NULL) {
            default_buf = PsAppVideo_DefaultCaptureBuffer(ctx->video) & 0x1U;
        }
        PsAppDiag_PrintDdrLogSnapshot(ctx->diag, "vram");
        PsAppDiag_PrintChainSnapshot(ctx->diag, "vram");
        PsAppDiag_PrintVdmaSnapshot(ctx->diag, "vram");
        PsAppDiag_PrintPixcapSummary(ctx->diag, default_buf);
    }
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

static void PsAppConsole_RunCpuSetProbe(PsAppConsoleContext *ctx, u32 samples, u32 interval_ms) {
    PsAppRuntimeContext *app;
    u32 idx;
    u32 first_count = 0U;
    u32 last_count = 0U;
    u32 sw_window_hits = 0U;
    u32 hw_window_hits = 0U;

    if ((ctx == NULL) || (ctx->runtime == NULL)) {
        return;
    }
    app = ctx->runtime;

    if (samples == 0U) {
        samples = 12U;
    }
    if (samples > PS_APP_TRACE_MAX_SAMPLES) {
        samples = PS_APP_TRACE_MAX_SAMPLES;
    }
    if (interval_ms > 5000U) {
        interval_ms = 5000U;
    }

    xil_printf("[CPUSET] begin samples=%u interval_ms=%u range=0x%08x..0x%08x\r\n",
               (unsigned int)samples,
               (unsigned int)interval_ms,
               (unsigned int)PS_APP_CPUSET_PC_START,
               (unsigned int)PS_APP_CPUSET_PC_END);

    for (idx = 0U; idx < samples; ++idx) {
        u32 pc = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_PC);
        u32 r0 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_R0);
        u32 r1 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_R1);
        u32 r2 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_R2);
        u32 r4 = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_R4);
        u32 cs = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_CPUSET);
        u32 count = cs & 0xFFFFU;
        u32 seen = (cs >> 16) & 0x1U;
        u32 in_window = (cs >> 17) & 0x1U;
        u32 sw_in_window = ((pc >= PS_APP_CPUSET_PC_START) && (pc <= PS_APP_CPUSET_PC_END)) ? 1U : 0U;

        if (idx == 0U) {
            first_count = count;
        }
        last_count = count;
        if (sw_in_window != 0U) {
            sw_window_hits++;
        }
        if (in_window != 0U) {
            hw_window_hits++;
        }

        xil_printf("[CPUSET] i=%u pc=0x%08x sw_win=%u hw_win=%u seen=%u cnt=%u r0=0x%08x r1=0x%08x r2=0x%08x r4=0x%08x\r\n",
                   (unsigned int)idx,
                   (unsigned int)pc,
                   (unsigned int)sw_in_window,
                   (unsigned int)in_window,
                   (unsigned int)seen,
                   (unsigned int)count,
                   (unsigned int)r0,
                   (unsigned int)r1,
                   (unsigned int)r2,
                   (unsigned int)r4);

        if ((idx + 1U) < samples) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    xil_printf("[CPUSET] summary cnt_first=%u cnt_last=%u delta=%u sw_hits=%u hw_hits=%u\r\n",
               (unsigned int)first_count,
               (unsigned int)last_count,
               (unsigned int)((last_count - first_count) & 0xFFFFU),
               (unsigned int)sw_window_hits,
               (unsigned int)hw_window_hits);
}

static void PsAppConsole_RunDdrAtomProbe(PsAppConsoleContext *ctx, u32 samples, u32 interval_ms) {
    PsAppRuntimeContext *app;
    u32 idx;
    u32 last_stat = 0U;

    if ((ctx == NULL) || (ctx->runtime == NULL)) {
        return;
    }
    app = ctx->runtime;

    if (samples == 0U) {
        samples = 12U;
    }
    if (samples > PS_APP_TRACE_MAX_SAMPLES) {
        samples = PS_APP_TRACE_MAX_SAMPLES;
    }
    if (interval_ms > 5000U) {
        interval_ms = 5000U;
    }

    xil_printf("[DDRATOM] begin samples=%u interval_ms=%u\r\n",
               (unsigned int)samples,
               (unsigned int)interval_ms);
    xil_printf("[DDRATOM] stat bits: b0=last_drift0 b1=last_drift1 b2=last_any b3=pending cnt_cmp/cnt_d0/cnt_d1 in [15:8]/[23:16]/[31:24]\r\n");

    for (idx = 0U; idx < samples; ++idx) {
        u32 w0 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_WORD0);
        u32 w1 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_WORD1);
        u32 n0 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_NEXT0);
        u32 n1 = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_NEXT1);
        u32 st = PsGbaRegs_Read(app->regs, GBA_REG_DDR_ATOM_STAT);
        u32 sw_d0 = (w0 != n0) ? 1U : 0U;
        u32 sw_d1 = (w1 != n1) ? 1U : 0U;

        last_stat = st;
        xil_printf("[DDRATOM] i=%u w0=0x%08x w1=0x%08x n0=0x%08x n1=0x%08x st=0x%08x sw_d0=%u sw_d1=%u\r\n",
                   (unsigned int)idx,
                   (unsigned int)w0,
                   (unsigned int)w1,
                   (unsigned int)n0,
                   (unsigned int)n1,
                   (unsigned int)st,
                   (unsigned int)sw_d0,
                   (unsigned int)sw_d1);

        if ((idx + 1U) < samples) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    xil_printf("[DDRATOM] summary last_stat=0x%08x cmp=%u drift0=%u drift1=%u last(b0,b1,b2,b3)=(%u,%u,%u,%u)\r\n",
               (unsigned int)last_stat,
               (unsigned int)((last_stat >> 8) & 0xFFU),
               (unsigned int)((last_stat >> 16) & 0xFFU),
               (unsigned int)((last_stat >> 24) & 0xFFU),
               (unsigned int)(last_stat & 0x1U),
               (unsigned int)((last_stat >> 1) & 0x1U),
               (unsigned int)((last_stat >> 2) & 0x1U),
               (unsigned int)((last_stat >> 3) & 0x1U));
}

#define PCTRACE_MAX 4096U

static void PsAppConsole_RunPcTrace(PsAppConsoleContext *ctx, u32 samples) {
    PsAppRuntimeContext *app;
    static u32 pc_buf[PCTRACE_MAX];
    static u8  st_buf[PCTRACE_MAX];
    static u32 r0_buf[PCTRACE_MAX];
    static u32 r2_buf[PCTRACE_MAX];
    u32 i;

    if ((ctx == NULL) || (ctx->runtime == NULL)) return;
    app = ctx->runtime;
    if ((samples == 0U) || (samples > PCTRACE_MAX)) samples = 1024U;

    xil_printf("[PCTRACE] capturing %u samples (pc+r0+r2)...\r\n", (unsigned)samples);

    for (i = 0U; i < samples; i++) {
        u32 mem = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_MEM);
        pc_buf[i] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_PC);
        st_buf[i] = (u8)(mem & 0xFFU);
        r0_buf[i] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_R0);
        r2_buf[i] = PsGbaRegs_Read(app->regs, GBA_REG_DEBUG_CPU_R2);
    }

    for (i = 0U; i < samples; i++) {
        u32 pc = pc_buf[i];
        const char *region;
        u8 st = st_buf[i];
        u32 r0 = r0_buf[i];
        u32 r2 = r2_buf[i];

        if (pc < 0x00004000U)      region = "BIOS";
        else if (pc >= 0x03000000U && pc <= 0x03007FFFU) region = "IWRAM";
        else if (pc >= 0x08000000U && pc <= 0x0DFFFFFFU) region = "ROM";
        else if (pc == 0U)         region = "NULL";
        else                       region = "OTHER";

        xil_printf("%5u: 0x%08X %-5s st=0x%02X r0=0x%08X r2=0x%08X\r\n",
                   (unsigned)i, (unsigned)pc, region, (unsigned)st,
                   (unsigned)r0, (unsigned)r2);
    }
    xil_printf("[PCTRACE] done %u samples\r\n", (unsigned)samples);
}
#undef PCTRACE_MAX

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
    xil_printf("  cpusetprobe [samples] [interval_ms]\r\n");
    xil_printf("  ddratomprobe [samples] [interval_ms]\r\n");
    xil_printf("  vramonce\r\n");
    xil_printf("  vramprobe [samples] [interval_ms]\r\n");
    xil_printf("  vramdiag [samples] [interval_ms]\r\n");
    xil_printf("  fps [seconds] [sample_ms]\r\n");
    xil_printf("  cacheprof [duration_ms]\r\n");
    xil_printf("  pctrace [samples]\r\n");
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

    if (strcmp(cmd, "cpusetprobe") == 0) {
        u32 samples = 12U;
        u32 interval_ms = 20U;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &samples) != 0)) {
            xil_printf("[CMD] usage: cpusetprobe [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: cpusetprobe [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((samples == 0U) || (samples > PS_APP_TRACE_MAX_SAMPLES) || (interval_ms > 5000U)) {
            xil_printf("[CMD] range: samples=1..%u interval_ms=0..5000\r\n",
                       (unsigned int)PS_APP_TRACE_MAX_SAMPLES);
            return 1U;
        }

        PsAppConsole_RunCpuSetProbe(ctx, samples, interval_ms);
        return 1U;
    }

    if (strcmp(cmd, "ddratomprobe") == 0) {
        u32 samples = 12U;
        u32 interval_ms = 20U;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &samples) != 0)) {
            xil_printf("[CMD] usage: ddratomprobe [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: ddratomprobe [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((samples == 0U) || (samples > PS_APP_TRACE_MAX_SAMPLES) || (interval_ms > 5000U)) {
            xil_printf("[CMD] range: samples=1..%u interval_ms=0..5000\r\n",
                       (unsigned int)PS_APP_TRACE_MAX_SAMPLES);
            return 1U;
        }

        PsAppConsole_RunDdrAtomProbe(ctx, samples, interval_ms);
        return 1U;
    }

    if (strcmp(cmd, "vramonce") == 0) {
        PsAppConsole_RunVramProbe(ctx, 1U, 0U, 0U);
        return 1U;
    }

    if (strcmp(cmd, "vramprobe") == 0) {
        u32 samples = 16U;
        u32 interval_ms = 20U;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &samples) != 0)) {
            xil_printf("[CMD] usage: vramprobe [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: vramprobe [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((samples == 0U) || (samples > PS_APP_TRACE_MAX_SAMPLES) || (interval_ms > 5000U)) {
            xil_printf("[CMD] range: samples=1..%u interval_ms=0..5000\r\n",
                       (unsigned int)PS_APP_TRACE_MAX_SAMPLES);
            return 1U;
        }

        PsAppConsole_RunVramProbe(ctx, samples, interval_ms, 0U);
        return 1U;
    }

    if (strcmp(cmd, "vramdiag") == 0) {
        u32 samples = 8U;
        u32 interval_ms = 20U;

        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &samples) != 0)) {
            xil_printf("[CMD] usage: vramdiag [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: vramdiag [samples] [interval_ms]\r\n");
            return 1U;
        }
        if ((samples == 0U) || (samples > PS_APP_TRACE_MAX_SAMPLES) || (interval_ms > 5000U)) {
            xil_printf("[CMD] range: samples=1..%u interval_ms=0..5000\r\n",
                       (unsigned int)PS_APP_TRACE_MAX_SAMPLES);
            return 1U;
        }

        PsAppConsole_RunVramProbe(ctx, samples, interval_ms, 1U);
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

    if (strcmp(cmd, "pctrace") == 0) {
        u32 samples = 1024U;
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (PsAppConsole_ParseU32Value(arg1, &samples) != 0)) {
            xil_printf("[CMD] usage: pctrace [samples]\r\n");
            return 1U;
        }
        if ((samples == 0U) || (samples > 4096U)) {
            xil_printf("[CMD] range: samples=1..4096\r\n");
            return 1U;
        }
        PsAppConsole_RunPcTrace(ctx, samples);
        return 1U;
    }

    return 0U;
}
