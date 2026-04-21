#include "Diagnostics/Inc/ps_app_diag.h"

#include "xaxivdma_hw.h"
#include "xil_cache.h"
#include "xil_printf.h"

#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Video/Inc/ps_app_video.h"

void PsAppDiag_PrintPixcapSummary(PsAppDiagContext *ctx, u32 buf_idx) {
    u32 latest_seq;
    u32 latest_buf;
    u32 raw_hash;
    u32 row0_hash;
    u32 row_mid_hash;
    u32 row_last_hash;
    u32 word0;
    u32 word_mid;
    u32 word_last;
    u32 frame_idx;
    u32 hdmi_hash;

    if (ctx == NULL) {
        return;
    }

    PsAppVideo_ReadCaptureStatus(ctx->video, &latest_seq, &latest_buf);
    raw_hash = PsAppVideo_ComputeCaptureSparseHash(buf_idx, PS_APP_FRAME_HASH_SAMPLES);
    row0_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, 0U);
    row_mid_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, PS_APP_GBA_FRAME_HEIGHT / 2U);
    row_last_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, PS_APP_GBA_FRAME_HEIGHT - 1U);
    word0 = PsAppVideo_ReadCaptureWord(buf_idx, 0U);
    word_mid = PsAppVideo_ReadCaptureWord(buf_idx, PS_APP_GBA_FB_CAPTURE_WORDS / 2U);
    word_last = PsAppVideo_ReadCaptureWord(buf_idx, PS_APP_GBA_FB_CAPTURE_WORDS - 1U);
    frame_idx = PsAppVideo_DefaultFrameIndex(ctx->video);
    hdmi_hash = 0U;
    if ((ctx->vdma->is_ready != 0U) && (frame_idx < ctx->vdma->frame_count)) {
        hdmi_hash = PsAppVideo_ComputeSparseHash(ctx->vdma->frame_addrs[frame_idx],
                                                 ctx->vdma->frame_size_bytes,
                                                 PS_APP_FRAME_HASH_SAMPLES);
    }

    xil_printf("[PIXCAP] summary latest_seq=%u latest_buf=%u use_buf=%u last_seq=%u last_buf=%u last_frame=%u raw_hash=0x%08x hdmi_frame=%u hdmi_hash=0x%08x\r\n",
               (unsigned int)latest_seq,
               (unsigned int)latest_buf,
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)ctx->video->state->fbcap_last_frame_seq,
               (unsigned int)ctx->video->state->fbcap_last_buf_idx,
               (unsigned int)ctx->video->state->fbcap_last_frame_idx,
               (unsigned int)raw_hash,
               (unsigned int)frame_idx,
               (unsigned int)hdmi_hash);
    xil_printf("[PIXCAP] rows r0=0x%08x rmid=0x%08x rlast=0x%08x w0=0x%08x wmid=0x%08x wlast=0x%08x\r\n",
               (unsigned int)row0_hash,
               (unsigned int)row_mid_hash,
               (unsigned int)row_last_hash,
               (unsigned int)word0,
               (unsigned int)word_mid,
               (unsigned int)word_last);
}

void PsAppDiag_PrintPixcapRow(PsAppDiagContext *ctx, u32 buf_idx, u32 y) {
    const u32 sample_x0 = 0U;
    const u32 sample_x1 = 60U;
    const u32 sample_x2 = 120U;
    const u32 sample_x3 = 239U;
    u32 row_hash;
    u32 word0;
    u32 word_mid;
    u32 word_last;
    u32 base_word_idx;

    if ((ctx == NULL) || (y >= PS_APP_GBA_FRAME_HEIGHT)) {
        return;
    }

    row_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, y);
    base_word_idx = y * PS_APP_GBA_FB_CAPTURE_ROW_WORDS;
    word0 = PsAppVideo_ReadCaptureWord(buf_idx, base_word_idx + 0U);
    word_mid = PsAppVideo_ReadCaptureWord(buf_idx,
                                          base_word_idx + (PS_APP_GBA_FB_CAPTURE_ROW_WORDS / 2U));
    word_last = PsAppVideo_ReadCaptureWord(buf_idx,
                                           base_word_idx + (PS_APP_GBA_FB_CAPTURE_ROW_WORDS - 1U));

    xil_printf("[PIXCAP] row buf=%u y=%u hash=0x%08x w0=0x%08x wmid=0x%08x wlast=0x%08x\r\n",
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)y,
               (unsigned int)row_hash,
               (unsigned int)word0,
               (unsigned int)word_mid,
               (unsigned int)word_last);
    xil_printf("[PIXCAP] row samples x0=0x%04x x60=0x%04x x120=0x%04x x239=0x%04x\r\n",
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x0, y),
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x1, y),
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x2, y),
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x3, y));
}

void PsAppDiag_PrintPixcapCompare(PsAppDiagContext *ctx, u32 buf_idx, u32 frame_idx, u32 x, u32 y) {
    u16 src565;
    u32 expected;
    u32 active_x;
    u32 active_y;
    u32 active_w;
    u32 active_h;
    u32 fb_x;
    u32 fb_y;
    u32 fb_x1;
    u32 fb_y1;
    u32 fb00;
    u32 fb10;
    u32 fb01;
    u32 fb11;
    u32 match;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U) ||
        (x >= PS_APP_GBA_FRAME_WIDTH) || (y >= PS_APP_GBA_FRAME_HEIGHT) ||
        (frame_idx >= ctx->vdma->frame_count)) {
        return;
    }

    src565 = PsAppVideo_ReadCapturePixelRgb565(buf_idx, x, y);
    expected = PsAppVideo_ConvertRgb565ToXrgb8888(src565);
    PsAppVideo_ComputeDisplayWindow(ctx->video, &active_x, &active_y, &active_w, &active_h);
    if ((active_w == 0U) || (active_h == 0U)) {
        return;
    }

    fb_x = active_x + ((x * active_w) / PS_APP_GBA_FRAME_WIDTH);
    fb_y = active_y + ((y * active_h) / PS_APP_GBA_FRAME_HEIGHT);
    fb_x1 = active_x + (((x + 1U) * active_w) / PS_APP_GBA_FRAME_WIDTH);
    fb_y1 = active_y + (((y + 1U) * active_h) / PS_APP_GBA_FRAME_HEIGHT);
    if (fb_x1 > active_x) {
        fb_x1--;
    }
    if (fb_y1 > active_y) {
        fb_y1--;
    }
    if (fb_x1 < fb_x) {
        fb_x1 = fb_x;
    }
    if (fb_y1 < fb_y) {
        fb_y1 = fb_y;
    }

    fb00 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x + 0U, fb_y + 0U);
    fb10 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x1, fb_y + 0U);
    fb01 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x + 0U, fb_y1);
    fb11 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x1, fb_y1);
    match = (((fb00 & 0x00FFFFFFU) == expected) &&
             ((fb10 & 0x00FFFFFFU) == expected) &&
             ((fb01 & 0x00FFFFFFU) == expected) &&
             ((fb11 & 0x00FFFFFFU) == expected)) ? 1U : 0U;

    xil_printf("[PIXCAP] cmp buf=%u frame=%u x=%u y=%u src565=0x%04x exp=0x%08x match=%u\r\n",
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)frame_idx,
               (unsigned int)x,
               (unsigned int)y,
               (unsigned int)src565,
               (unsigned int)expected,
               (unsigned int)match);
    xil_printf("[PIXCAP] cmp fb00=0x%08x fb10=0x%08x fb01=0x%08x fb11=0x%08x\r\n",
               (unsigned int)fb00,
               (unsigned int)fb10,
               (unsigned int)fb01,
               (unsigned int)fb11);
}

void PsAppDiag_ScanFramebufferForAnomaly(PsAppDiagContext *ctx, const char *tag, u8 force_print) {
    u32 parked;
    UINTPTR fb_base;
    u32 line_step;
    u32 pixel_step;
    u32 white_lines;
    u32 black_lines;
    u32 total_sampled;
    u32 ly;
    u32 px;
    u32 anomaly;
    u32 active_x;
    u32 active_y;
    u32 active_w;
    u32 active_h;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return;
    }

    parked = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    if (parked >= ctx->vdma->frame_count) {
        parked = 0U;
    }
    fb_base = ctx->vdma->frame_addrs[parked];
    if (ctx->vdma->line_stride_bytes == 0U) {
        return;
    }

    PsAppVideo_ComputeDisplayWindow(ctx->video, &active_x, &active_y, &active_w, &active_h);
    if ((active_w == 0U) || (active_h == 0U)) {
        return;
    }

    line_step = active_h / PS_APP_FBSCAN_SAMPLE_LINES;
    if (line_step == 0U) {
        line_step = 1U;
    }
    pixel_step = active_w / PS_APP_FBSCAN_SAMPLE_PIXELS;
    if (pixel_step == 0U) {
        pixel_step = 1U;
    }

    Xil_DCacheInvalidateRange((INTPTR)fb_base, ctx->vdma->frame_size_bytes);

    white_lines = 0U;
    black_lines = 0U;
    total_sampled = 0U;
    anomaly = 0U;

    for (ly = 0U; ly < active_h; ly += line_step) {
        UINTPTR line_base = fb_base +
                            ((active_y + ly) * ctx->vdma->line_stride_bytes) +
                            (active_x * sizeof(u32));
        volatile const u32 *line_ptr = (volatile const u32 *)line_base;
        u32 white_count = 0U;
        u32 black_count = 0U;

        for (px = 0U; px < active_w; px += pixel_step) {
            u32 pixel = line_ptr[px];
            if ((pixel & 0x00FFFFFFU) == PS_APP_FBSCAN_WHITE_PIXEL) {
                white_count++;
            } else if ((pixel & 0x00FFFFFFU) == PS_APP_FBSCAN_BLACK_PIXEL) {
                black_count++;
            }
        }

        total_sampled++;

        if (white_count >= (PS_APP_FBSCAN_SAMPLE_PIXELS / 2U)) {
            white_lines++;
        }
        if (black_count >= PS_APP_FBSCAN_SAMPLE_PIXELS) {
            black_lines++;
        }
    }

    if (white_lines >= (total_sampled / 4U)) {
        anomaly = 1U;
    }
    if ((ctx->rom->loaded != 0U) && (ctx->diag->delayed_chain_tick > 300U) &&
        (black_lines >= ((total_sampled * 3U) / 4U))) {
        anomaly = 2U;
    }

    if ((anomaly != 0U) || (force_print != 0U)) {
        xil_printf("[FBSCAN] %s park=%u fb=0x%08x white_lines=%u black_lines=%u total=%u anomaly=%u\r\n",
                   (tag != NULL) ? tag : "scan",
                   (unsigned int)parked,
                   (unsigned int)fb_base,
                   (unsigned int)white_lines,
                   (unsigned int)black_lines,
                   (unsigned int)total_sampled,
                   (unsigned int)anomaly);

        {
            u32 fi;
            const u32 active_base_off =
                (active_y * ctx->vdma->line_stride_bytes) + (active_x * sizeof(u32));
            const u32 active_mid_off =
                ((active_y + (active_h / 2U)) * ctx->vdma->line_stride_bytes) +
                ((active_x + (active_w / 2U)) * sizeof(u32));
            const u32 active_last_off =
                ((active_y + active_h - 1U) * ctx->vdma->line_stride_bytes) +
                ((active_x + active_w - 4U) * sizeof(u32));

            for (fi = 0U; fi < ctx->vdma->frame_count; fi++) {
                UINTPTR addr = ctx->vdma->frame_addrs[fi];
                Xil_DCacheInvalidateRange((INTPTR)addr, ctx->vdma->frame_size_bytes);
                xil_printf("[FBSCAN] fb%u act0=0x%08x act1=0x%08x act2=0x%08x act3=0x%08x mid0=0x%08x mid1=0x%08x tail0=0x%08x tail1=0x%08x\r\n",
                           (unsigned int)fi,
                           (unsigned int)Xil_In32(addr + active_base_off),
                           (unsigned int)Xil_In32(addr + active_base_off + 4U),
                           (unsigned int)Xil_In32(addr + active_base_off + 8U),
                           (unsigned int)Xil_In32(addr + active_base_off + 12U),
                           (unsigned int)Xil_In32(addr + active_mid_off),
                           (unsigned int)Xil_In32(addr + active_mid_off + 4U),
                           (unsigned int)Xil_In32(addr + active_last_off),
                           (unsigned int)Xil_In32(addr + active_last_off + 4U));
            }
        }
    }

    if (anomaly != 0U) {
        xil_printf("[FBSCAN] *** ANOMALY DETECTED type=%u *** dumping full state\r\n",
                   (unsigned int)anomaly);
        PsAppDiag_PrintChainSnapshot(ctx, "fbscan");
        PsAppDiag_PrintVdmaSnapshot(ctx, "fbscan");
        PsAppDiag_PrintConfigReadback(ctx, "fbscan");
        {
            u32 status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
            u32 err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
            u32 dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
            u32 dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
            xil_printf("[FBSCAN] status1=0x%08x err=0x%08x pc=0x%08x mem=0x%08x\r\n",
                       (unsigned int)status1,
                       (unsigned int)err_latch,
                       (unsigned int)dbg_pc,
                       (unsigned int)dbg_mem);
        }
        ctx->diag->fbscan_last_anomaly_tick = ctx->diag->fbscan_tick;
        ctx->diag->fbscan_dump_count++;
    }
}
