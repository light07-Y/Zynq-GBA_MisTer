#include "Video/Inc/ps_app_video.h"

#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
#include "xaxivdma_hw.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xiltimer.h"
#include "xinterrupt_wrap.h"
#include "xparameters.h"

#if defined(XPAR_ZYNQ_GBA_SYSTEM_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR)
#define PS_APP_FB_CAP_BRAM_BASE_ADDR XPAR_ZYNQ_GBA_SYSTEM_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR
#elif defined(XPAR_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR)
#define PS_APP_FB_CAP_BRAM_BASE_ADDR XPAR_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR
#else
#define PS_APP_FB_CAP_BRAM_BASE_ADDR 0x44000000U
#endif

#if defined(XPAR_XAXIVDMA_0_INTERRUPTS) && defined(XPAR_XAXIVDMA_0_INTERRUPT_PARENT)
#define PS_APP_VDMA_READ_INTR_ID XPAR_XAXIVDMA_0_INTERRUPTS
#define PS_APP_VDMA_READ_INTR_PARENT XPAR_XAXIVDMA_0_INTERRUPT_PARENT
#elif defined(XPAR_HDMI_VDMA_INTERRUPTS) && defined(XPAR_HDMI_VDMA_INTERRUPT_PARENT)
#define PS_APP_VDMA_READ_INTR_ID XPAR_HDMI_VDMA_INTERRUPTS
#define PS_APP_VDMA_READ_INTR_PARENT XPAR_HDMI_VDMA_INTERRUPT_PARENT
#else
#define PS_APP_VDMA_READ_INTR_ID 0U
#define PS_APP_VDMA_READ_INTR_PARENT 0U
#endif

/*
 * HDMI 这里使用的是 MM2S 持续显示链路。
 * 该场景下必须保持 circular/parking 语义，不能再把 READ 通道切到
 * FrameCounter 模式，否则 VDMA 会在固定帧数后自动 halted，最终黑屏。
 * 因此这里只保留 error interrupt，用于故障检测；显示推进仍由现有
 * VSYNC/服务循环驱动。
 */
#define PS_APP_VDMA_READ_IRQ_MASK XAXIVDMA_IXR_ERROR_MASK

static u16 s_capture_shadow[PS_APP_GBA_FRAME_WIDTH * PS_APP_GBA_FRAME_HEIGHT];
static u16 s_scale_x_lut[PS_APP_HDMI_WIDTH];
static u16 s_scale_y_lut[PS_APP_HDMI_HEIGHT];
static u32 s_scale_lut_w = 0U;
static u32 s_scale_lut_h = 0U;

static void PsAppVideo_UpdateDisplayFrameState(PsAppVideoContext *ctx) {
    u32 frame_idx;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return;
    }

    frame_idx = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    if (frame_idx >= ctx->vdma->frame_count) {
        frame_idx = 0U;
    }

    if ((ctx->state->pending_frame_idx >= ctx->vdma->frame_count) ||
        (ctx->state->pending_frame_idx == (u8)frame_idx)) {
        ctx->state->pending_frame_idx = (u8)frame_idx;
    }

    if (ctx->state->display_frame_idx != (u8)frame_idx) {
        ctx->state->display_frame_idx = (u8)frame_idx;
        PsGbaRegs_SetDisplayFrameIdx(ctx->regs, frame_idx);
    }
}

static void PsAppVideo_VdmaGeneralCallback(void *ref, u32 intr_mask) {
    PsAppVideoContext *ctx = (PsAppVideoContext *)ref;

    if (ctx == NULL) {
        return;
    }

    ctx->state->vdma_last_intr_mask = intr_mask;
    ctx->state->vdma_irq_count++;
    PsAppVideo_UpdateDisplayFrameState(ctx);
}

static void PsAppVideo_VdmaErrorCallback(void *ref, u32 err_mask) {
    PsAppVideoContext *ctx = (PsAppVideoContext *)ref;

    if (ctx == NULL) {
        return;
    }

    ctx->state->vdma_last_err_mask = err_mask;
    ctx->state->vdma_err_count++;
}

static UINTPTR PsAppVideo_CaptureBufferBaseAddr(u32 buf_idx) {
    return (UINTPTR)(PS_APP_FB_CAP_BRAM_BASE_ADDR +
                     ((buf_idx & 0x1U) * PS_APP_GBA_FB_CAPTURE_FRAME_BYTES));
}

static u32 PsAppVideo_ResolveBackbufferFrame(u32 display_frame_idx) {
    switch (display_frame_idx % 3U) {
        case 0U:
            return 2U;
        case 1U:
            return 0U;
        default:
            return 1U;
    }
}

static u32 PsAppVideo_SelectSafeFrame(PsAppVideoContext *ctx, u32 current_frame) {
    u32 preferred_frame;
    u32 idx;

    if ((ctx == NULL) || (ctx->vdma->frame_count == 0U)) {
        return 0U;
    }

    preferred_frame = PsAppVideo_ResolveBackbufferFrame(current_frame);
    if ((preferred_frame < ctx->vdma->frame_count) &&
        (preferred_frame != current_frame) &&
        ((ctx->state->pending_frame_idx >= ctx->vdma->frame_count) ||
         (ctx->state->pending_frame_idx == current_frame) ||
         (preferred_frame != ctx->state->pending_frame_idx))) {
        return preferred_frame;
    }

    for (idx = 0U; idx < ctx->vdma->frame_count; ++idx) {
        if (idx == current_frame) {
            continue;
        }
        if ((ctx->state->pending_frame_idx < ctx->vdma->frame_count) &&
            (ctx->state->pending_frame_idx != current_frame) &&
            (idx == ctx->state->pending_frame_idx)) {
            continue;
        }
        return idx;
    }

    return current_frame;
}

static void PsAppVideo_LoadCaptureShadowFromBram(u32 buf_idx) {
    volatile const u32 *src_words;
    u32 word_idx;
    u32 pixel_idx;

    src_words = (volatile const u32 *)(PS_APP_FB_CAP_BRAM_BASE_ADDR +
                                       ((buf_idx & 0x1U) * PS_APP_GBA_FB_CAPTURE_FRAME_BYTES));
    pixel_idx = 0U;
    for (word_idx = 0U; word_idx < PS_APP_GBA_FB_CAPTURE_WORDS; ++word_idx) {
        u32 packed = src_words[word_idx];
        s_capture_shadow[pixel_idx++] = (u16)(packed & 0xFFFFU);
        s_capture_shadow[pixel_idx++] = (u16)((packed >> 16) & 0xFFFFU);
    }
}

static void PsAppVideo_UpdateScaleLut(u32 dst_w, u32 dst_h) {
    u32 dst_px;
    u32 dst_py;

    if (dst_w > PS_APP_HDMI_WIDTH) {
        dst_w = PS_APP_HDMI_WIDTH;
    }
    if (dst_h > PS_APP_HDMI_HEIGHT) {
        dst_h = PS_APP_HDMI_HEIGHT;
    }
    if ((dst_w == s_scale_lut_w) && (dst_h == s_scale_lut_h)) {
        return;
    }

    for (dst_px = 0U; dst_px < dst_w; ++dst_px) {
        u32 src_x = (dst_px * PS_APP_GBA_FRAME_WIDTH) / dst_w;
        if (src_x >= PS_APP_GBA_FRAME_WIDTH) {
            src_x = PS_APP_GBA_FRAME_WIDTH - 1U;
        }
        s_scale_x_lut[dst_px] = (u16)src_x;
    }

    for (dst_py = 0U; dst_py < dst_h; ++dst_py) {
        u32 src_y = (dst_py * PS_APP_GBA_FRAME_HEIGHT) / dst_h;
        if (src_y >= PS_APP_GBA_FRAME_HEIGHT) {
            src_y = PS_APP_GBA_FRAME_HEIGHT - 1U;
        }
        s_scale_y_lut[dst_py] = (u16)src_y;
    }

    s_scale_lut_w = dst_w;
    s_scale_lut_h = dst_h;
}

static u32 PsAppVideo_TicksToUs(u64 ticks) {
#if defined(COUNTS_PER_SECOND) && (COUNTS_PER_SECOND != 0)
    u64 scaled = (ticks * 1000000ULL) + ((u64)COUNTS_PER_SECOND / 2ULL);
    return (u32)(scaled / (u64)COUNTS_PER_SECOND);
#else
    return (u32)ticks;
#endif
}

static void PsAppVideo_UpdateBlitStats(PsAppVideoContext *ctx,
                                       u32 prev_frame_seq,
                                       u32 frame_seq,
                                       u32 blit_us) {
    u32 seq_gap;

    if (ctx == NULL) {
        return;
    }

    if (ctx->state->blit_count < 0xFFFFFFFFU) {
        ctx->state->blit_count++;
    }
    ctx->state->blit_last_us = blit_us;
    if (blit_us > ctx->state->blit_max_us) {
        ctx->state->blit_max_us = blit_us;
    }
    if ((0xFFFFFFFFU - ctx->state->blit_total_us) < blit_us) {
        ctx->state->blit_total_us = 0xFFFFFFFFU;
    } else {
        ctx->state->blit_total_us += blit_us;
    }

    if ((prev_frame_seq != 0U) && (frame_seq > prev_frame_seq)) {
        seq_gap = frame_seq - prev_frame_seq;
        if (seq_gap > ctx->state->blit_seq_gap_max) {
            ctx->state->blit_seq_gap_max = seq_gap;
        }
    }
}

static XStatus PsAppVideo_BlitCapturedFrameToHdmi(PsAppVideoContext *ctx, u32 frame_seq, u32 buf_idx) {
    u32 current_frame;
    u32 target_frame;
    u32 *fb32;
    u32 prev_frame_seq;
    u32 dst_x;
    u32 dst_y;
    u32 dst_w;
    u32 dst_h;
    u32 dst_py;
    XTime tick_begin;
    XTime tick_end;
    UINTPTR flush_base_addr;
    u32 flush_bytes;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return XST_FAILURE;
    }

    XTime_GetTime(&tick_begin);
    prev_frame_seq = ctx->state->fbcap_last_frame_seq;

    current_frame = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    if (current_frame >= ctx->vdma->frame_count) {
        current_frame = 0U;
    }

    target_frame = PsAppVideo_SelectSafeFrame(ctx, current_frame);
    if (target_frame >= ctx->vdma->frame_count) {
        target_frame = 0U;
    }

    fb32 = (u32 *)ctx->vdma->frame_addrs[target_frame];
    PsAppVideo_ComputeDisplayWindow(ctx, &dst_x, &dst_y, &dst_w, &dst_h);
    if ((dst_w == 0U) || (dst_h == 0U)) {
        return XST_FAILURE;
    }

    PsAppVideo_LoadCaptureShadowFromBram(buf_idx);
    PsAppVideo_UpdateScaleLut(dst_w, dst_h);

    for (dst_py = 0U; dst_py < dst_h; ++dst_py) {
        UINTPTR line_base =
            (UINTPTR)fb32 +
            (((dst_y + dst_py) * ctx->vdma->line_stride_bytes) + (dst_x * sizeof(u32)));
        u32 *line_ptr = (u32 *)line_base;
        const u16 *src_row = &s_capture_shadow[s_scale_y_lut[dst_py] * PS_APP_GBA_FRAME_WIDTH];
        u32 dst_px;

        for (dst_px = 0U; dst_px < dst_w; ++dst_px) {
            line_ptr[dst_px] =
                PsAppVideo_ConvertRgb565ToXrgb8888(src_row[s_scale_x_lut[dst_px]]);
        }
    }

    flush_base_addr = (UINTPTR)fb32 +
                      ((dst_y * ctx->vdma->line_stride_bytes) + (dst_x * sizeof(u32)));
    flush_bytes = ((dst_h - 1U) * ctx->vdma->line_stride_bytes) + (dst_w * sizeof(u32));
    Xil_DCacheFlushRange((INTPTR)flush_base_addr, (INTPTR)flush_bytes);

    if (PsAppVideo_RequestFrame(ctx, target_frame) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    XTime_GetTime(&tick_end);
    PsAppVideo_UpdateBlitStats(ctx,
                               prev_frame_seq,
                               frame_seq,
                               PsAppVideo_TicksToUs((u64)(tick_end - tick_begin)));
    ctx->state->fbcap_last_frame_seq = frame_seq;
    ctx->state->fbcap_last_buf_idx = (u8)(buf_idx & 0x1U);
    ctx->state->fbcap_last_frame_idx = (u8)target_frame;
    return XST_SUCCESS;
}

XStatus PsAppVideo_InitInterrupts(PsAppVideoContext *ctx) {
    XStatus status;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return XST_FAILURE;
    }

    if ((PS_APP_VDMA_READ_INTR_ID == 0U) || (PS_APP_VDMA_READ_INTR_PARENT == 0U)) {
        return XST_FAILURE;
    }

    ctx->state->vdma_irq_count = 0U;
    ctx->state->vdma_err_count = 0U;
    ctx->state->vdma_last_intr_mask = 0U;
    ctx->state->vdma_last_err_mask = 0U;

    XAxiVdma_IntrDisable(&ctx->vdma->vdma, XAXIVDMA_IXR_ALL_MASK, XAXIVDMA_READ);
    XAxiVdma_IntrClear(&ctx->vdma->vdma, XAXIVDMA_IXR_ALL_MASK, XAXIVDMA_READ);

    /*
     * 这里仍然注册 general callback，是因为官方驱动的 ReadIntrHandler()
     * 会先检查 CompletionCallBack 是否为空；若为空会直接 return，连
     * error callback 也不会继续分发。
     * 但我们不会打开 FRMCNT/DELAY completion 中断，只让它作为驱动入口
     * 的占位回调存在。
     */
    status = XAxiVdma_SetCallBack(&ctx->vdma->vdma,
                                  XAXIVDMA_HANDLER_GENERAL,
                                  (void *)PsAppVideo_VdmaGeneralCallback,
                                  (void *)ctx,
                                  XAXIVDMA_READ);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XAxiVdma_SetCallBack(&ctx->vdma->vdma,
                                  XAXIVDMA_HANDLER_ERROR,
                                  (void *)PsAppVideo_VdmaErrorCallback,
                                  (void *)ctx,
                                  XAXIVDMA_READ);
    if (status != XST_SUCCESS) {
        return status;
    }

    /*
     * 不调用 XAxiVdma_SetFrameCounter() / XAxiVdma_StartFrmCntEnable()。
     * 它们对应的是 frame-count 传输语义，适合测试/计数型中断示例，
     * 不适合当前 HDMI MM2S 持续输出。
     */
    status = XSetupInterruptSystem(&ctx->vdma->vdma,
                                   (void *)XAxiVdma_ReadIntrHandler,
                                   PS_APP_VDMA_READ_INTR_ID,
                                   PS_APP_VDMA_READ_INTR_PARENT,
                                   XINTERRUPT_DEFAULT_PRIORITY);
    if (status != XST_SUCCESS) {
        return status;
    }

    XAxiVdma_IntrClear(&ctx->vdma->vdma, XAXIVDMA_IXR_ALL_MASK, XAXIVDMA_READ);
    XAxiVdma_IntrEnable(&ctx->vdma->vdma, PS_APP_VDMA_READ_IRQ_MASK, XAXIVDMA_READ);
    PsAppVideo_UpdateDisplayFrameState(ctx);
    xil_printf("[VDMA] interrupt mode: read error irq enabled\r\n");
    return XST_SUCCESS;
}

XStatus PsAppVideo_RequestFrame(PsAppVideoContext *ctx, u32 frame_idx) {
    BaseType_t use_critical;
    XStatus status;
    u32 current_frame;
    u32 vdma_status;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return XST_FAILURE;
    }

    if (frame_idx >= ctx->vdma->frame_count) {
        frame_idx = 0U;
    }

    vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);
    current_frame = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    if (current_frame >= ctx->vdma->frame_count) {
        current_frame = 0U;
    }

    if (((vdma_status & XAXIVDMA_SR_HALTED_MASK) == 0U) &&
        (current_frame == frame_idx)) {
        ctx->state->pending_frame_idx = (u8)frame_idx;
        return XST_SUCCESS;
    }

    use_critical = (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) ? pdTRUE : pdFALSE;
    if (use_critical != pdFALSE) {
        taskENTER_CRITICAL();
    }

    status = PsHdmiVdma_Park(ctx->vdma, frame_idx);
    if (status != XST_SUCCESS) {
        vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);
        current_frame = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
        if (current_frame >= ctx->vdma->frame_count) {
            current_frame = 0U;
        }
        if (((vdma_status & XAXIVDMA_SR_HALTED_MASK) == 0U) &&
            (current_frame == frame_idx)) {
            status = XST_SUCCESS;
        }
    }
    if (status == XST_SUCCESS) {
        ctx->state->pending_frame_idx = (u8)frame_idx;
    }

    if (use_critical != pdFALSE) {
        taskEXIT_CRITICAL();
    }

    return status;
}

void PsAppVideo_AttemptRecover(PsAppVideoContext *ctx, u32 vdma_status, u32 vdma_errs) {
    u32 is_halted;

    if (ctx == NULL) {
        return;
    }

    is_halted = (vdma_status & XAXIVDMA_SR_HALTED_MASK) ? 1U : 0U;
    if ((vdma_errs == 0U) && (is_halted == 0U)) {
        return;
    }

    xil_printf("[VDMA] recover: sr=0x%08x err=0x%03x halted=%u\r\n",
               (unsigned int)vdma_status,
               (unsigned int)vdma_errs,
               (unsigned int)is_halted);

    if (vdma_errs != 0U) {
        (void)XAxiVdma_ClearDmaChannelErrors(&ctx->vdma->vdma, XAXIVDMA_READ, vdma_errs);
    }

    if (is_halted != 0U) {
        /*
         * 恢复时只重新启动连续输出通道，不要重新打开 FrameCounter。
         * 之前黑屏的根因就是在 recover 路径里把 FRMCNT_EN 重新拉起，
         * 导致 VDMA 反复“启动后又按固定帧数停机”。
         */
        (void)XAxiVdma_DmaStart(&ctx->vdma->vdma, XAXIVDMA_READ);
    }

    (void)PsAppVideo_RequestFrame(ctx, 0U);
    /* 恢复后重新打开 error irq，继续监控真实的 DMA 故障。 */
    XAxiVdma_IntrClear(&ctx->vdma->vdma, XAXIVDMA_IXR_ALL_MASK, XAXIVDMA_READ);
    XAxiVdma_IntrEnable(&ctx->vdma->vdma, PS_APP_VDMA_READ_IRQ_MASK, XAXIVDMA_READ);
    PsAppVideo_UpdateDisplayFrameState(ctx);
}

void PsAppVideo_SyncDisplayFrame(PsAppVideoContext *ctx) {
    PsAppVideo_UpdateDisplayFrameState(ctx);
}

XStatus PsAppVideo_RenderBootFrames(PsAppVideoContext *ctx) {
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    status = PsHdmiVdma_FillAllFrames(ctx->vdma, 0x00000000U);
    if (status != XST_SUCCESS) {
        return status;
    }

    (void)PsAppVideo_RequestFrame(ctx, 0U);
    PsAppVideo_SyncDisplayFrame(ctx);
    return XST_SUCCESS;
}

void PsAppVideo_PresentCapturedFrameIfReady(PsAppVideoContext *ctx) {
    u32 seq0;
    u32 seq1;
    u32 status;
    u32 last_seq;
    u32 sampled_buf_idx;
    static u32 blit_fail_log_count = 0U;

    if ((ctx == NULL) || (ctx->rom->loaded == 0U)) {
        return;
    }

    seq0 = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
    seq1 = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    if (seq1 != seq0) {
        status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
    }
    sampled_buf_idx = status & 0x1U;
    last_seq = ctx->state->fbcap_last_frame_seq;
    if ((seq1 == 0U) || (seq1 == last_seq)) {
        return;
    }
    if ((last_seq != 0U) && (seq1 < last_seq)) {
        if (ctx->state->blit_seq_glitch_drop < 0xFFFFFFFFU) {
            ctx->state->blit_seq_glitch_drop++;
        }
    }

    if (PsAppVideo_BlitCapturedFrameToHdmi(ctx, seq1, sampled_buf_idx) != XST_SUCCESS) {
        if (blit_fail_log_count < 5U) {
            xil_printf("[BLIT] fail seq=%u buf=%u\r\n",
                       (unsigned int)seq1,
                       (unsigned int)sampled_buf_idx);
            blit_fail_log_count++;
        }
    }
}

u32 PsAppVideo_DefaultCaptureBuffer(PsAppVideoContext *ctx) {
    u32 seq;
    u32 buf_idx;

    if (ctx == NULL) {
        return 0U;
    }

    if ((ctx->state->fbcap_last_frame_seq != 0U) && (ctx->state->fbcap_last_buf_idx < 2U)) {
        return ctx->state->fbcap_last_buf_idx;
    }

    PsAppVideo_ReadCaptureStatus(ctx, &seq, &buf_idx);
    (void)seq;
    return buf_idx & 0x1U;
}

u32 PsAppVideo_DefaultFrameIndex(PsAppVideoContext *ctx) {
    u32 frame_idx;

    if (ctx == NULL) {
        return 0U;
    }

    if ((ctx->vdma->frame_count != 0U) && (ctx->state->display_frame_idx < ctx->vdma->frame_count)) {
        return ctx->state->display_frame_idx;
    }

    if (ctx->vdma->is_ready == 0U) {
        return 0U;
    }

    frame_idx = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    if (frame_idx >= ctx->vdma->frame_count) {
        frame_idx = 0U;
    }

    return frame_idx;
}

void PsAppVideo_ReadCaptureStatus(PsAppVideoContext *ctx, u32 *seq_out, u32 *buf_out) {
    u32 seq0;
    u32 seq1;
    u32 status;

    if ((ctx == NULL) || (seq_out == NULL) || (buf_out == NULL)) {
        return;
    }

    seq0 = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
    seq1 = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    if (seq1 != seq0) {
        status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
    }

    *seq_out = seq1;
    *buf_out = status & 0x1U;
}

u32 PsAppVideo_ReadCaptureWord(u32 buf_idx, u32 word_idx) {
    if (word_idx >= PS_APP_GBA_FB_CAPTURE_WORDS) {
        return 0U;
    }

    return Xil_In32(PsAppVideo_CaptureBufferBaseAddr(buf_idx) + (word_idx * sizeof(u32)));
}

u16 PsAppVideo_ReadCapturePixelRgb565(u32 buf_idx, u32 x, u32 y) {
    u32 packed;
    u32 word_idx;

    if ((x >= PS_APP_GBA_FRAME_WIDTH) || (y >= PS_APP_GBA_FRAME_HEIGHT)) {
        return 0U;
    }

    word_idx = (y * PS_APP_GBA_FB_CAPTURE_ROW_WORDS) + (x >> 1);
    packed = PsAppVideo_ReadCaptureWord(buf_idx, word_idx);
    if ((x & 0x1U) != 0U) {
        return (u16)((packed >> 16) & 0xFFFFU);
    }

    return (u16)(packed & 0xFFFFU);
}

u32 PsAppVideo_ComputeCaptureSparseHash(u32 buf_idx, u32 samples) {
    u32 word_count;
    u32 step_words;
    u32 idx;
    u32 word_idx;
    u32 hash;

    word_count = PS_APP_GBA_FB_CAPTURE_WORDS;
    if (word_count == 0U) {
        return 0U;
    }

    if ((samples == 0U) || (samples > word_count)) {
        samples = word_count;
    }

    step_words = word_count / samples;
    if (step_words == 0U) {
        step_words = 1U;
    }

    hash = 2166136261U;
    word_idx = 0U;
    for (idx = 0U; idx < samples; ++idx) {
        hash ^= PsAppVideo_ReadCaptureWord(buf_idx, word_idx);
        hash *= 16777619U;

        if ((word_count - word_idx) <= step_words) {
            break;
        }
        word_idx += step_words;
    }

    return hash;
}

u32 PsAppVideo_ComputeCaptureRowHash(u32 buf_idx, u32 y) {
    u32 hash;
    u32 idx;
    u32 base_word_idx;

    if (y >= PS_APP_GBA_FRAME_HEIGHT) {
        return 0U;
    }

    hash = 2166136261U;
    base_word_idx = y * PS_APP_GBA_FB_CAPTURE_ROW_WORDS;
    for (idx = 0U; idx < PS_APP_GBA_FB_CAPTURE_ROW_WORDS; ++idx) {
        hash ^= PsAppVideo_ReadCaptureWord(buf_idx, base_word_idx + idx);
        hash *= 16777619U;
    }

    return hash;
}

u32 PsAppVideo_ComputeSparseHash(UINTPTR base_addr, u32 bytes, u32 samples) {
    volatile const u32 *ptr;
    u32 word_count;
    u32 step_words;
    u32 idx;
    u32 word_idx;
    u32 hash;

    if (bytes < 4U) {
        return 0U;
    }

    Xil_DCacheInvalidateRange((INTPTR)base_addr, bytes);

    ptr = (volatile const u32 *)base_addr;
    word_count = bytes >> 2;
    if (word_count == 0U) {
        return 0U;
    }

    if ((samples == 0U) || (samples > word_count)) {
        samples = word_count;
    }

    step_words = word_count / samples;
    if (step_words == 0U) {
        step_words = 1U;
    }

    hash = 2166136261U;
    word_idx = 0U;
    for (idx = 0U; idx < samples; ++idx) {
        hash ^= ptr[word_idx];
        hash *= 16777619U;

        if ((word_count - word_idx) <= step_words) {
            break;
        }
        word_idx += step_words;
    }

    return hash;
}

u32 PsAppVideo_ReadHdmiFramePixel(PsAppVideoContext *ctx, u32 frame_idx, u32 x, u32 y) {
    UINTPTR addr;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U) ||
        (frame_idx >= ctx->vdma->frame_count) ||
        (x >= ctx->vdma->width) ||
        (y >= ctx->vdma->height)) {
        return 0U;
    }

    Xil_DCacheInvalidateRange((INTPTR)ctx->vdma->frame_addrs[frame_idx],
                              (INTPTR)ctx->vdma->frame_size_bytes);
    addr = ctx->vdma->frame_addrs[frame_idx] +
           (y * ctx->vdma->line_stride_bytes) +
           (x * sizeof(u32));
    return Xil_In32(addr);
}

u32 PsAppVideo_ConvertRgb565ToXrgb8888(u16 pixel) {
    u32 r5;
    u32 g6;
    u32 b5;
    u32 r8;
    u32 g8;
    u32 b8;

    r5 = (pixel >> 11) & 0x1FU;
    g6 = (pixel >> 5) & 0x3FU;
    b5 = pixel & 0x1FU;

    r8 = (r5 << 3) | (r5 >> 2);
    g8 = (g6 << 2) | (g6 >> 4);
    b8 = (b5 << 3) | (b5 >> 2);

    return (b8 << 16) | (g8 << 8) | r8;
}

void PsAppVideo_ComputeDisplayWindow(PsAppVideoContext *ctx,
                                     u32 *x_out,
                                     u32 *y_out,
                                     u32 *w_out,
                                     u32 *h_out) {
    u32 x;
    u32 y;
    u32 w;
    u32 h;

    x = 0U;
    y = 0U;
    w = 0U;
    h = 0U;

    if ((ctx != NULL) && (ctx->vdma->is_ready != 0U) &&
        (ctx->vdma->width != 0U) && (ctx->vdma->height != 0U)) {
        if ((ctx->vdma->width * PS_APP_GBA_FRAME_HEIGHT) <=
            (ctx->vdma->height * PS_APP_GBA_FRAME_WIDTH)) {
            w = ctx->vdma->width;
            h = (ctx->vdma->width * PS_APP_GBA_FRAME_HEIGHT) / PS_APP_GBA_FRAME_WIDTH;
        } else {
            h = ctx->vdma->height;
            w = (ctx->vdma->height * PS_APP_GBA_FRAME_WIDTH) / PS_APP_GBA_FRAME_HEIGHT;
        }

        if (w == 0U) {
            w = 1U;
        }
        if (h == 0U) {
            h = 1U;
        }

        x = (ctx->vdma->width - w) / 2U;
        y = (ctx->vdma->height - h) / 2U;
    }

    if (x_out != NULL) {
        *x_out = x;
    }
    if (y_out != NULL) {
        *y_out = y;
    }
    if (w_out != NULL) {
        *w_out = w;
    }
    if (h_out != NULL) {
        *h_out = h;
    }
}
