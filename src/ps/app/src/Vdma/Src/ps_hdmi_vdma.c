#include "Vdma/Inc/ps_hdmi_vdma.h"

#include "xil_cache.h"

static XStatus PsHdmiVdma_ValidateFrameIndex(const PsHdmiVdma *ctx, u32 frame_idx) {
    if ((ctx == 0) || (ctx->is_ready == 0U)) {
        return XST_FAILURE;
    }

    if (frame_idx >= ctx->frame_count) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

XStatus PsHdmiVdma_Init(PsHdmiVdma *ctx,
                        u32 vdma_base_addr,
                        UINTPTR frame0_addr,
                        u32 frame_stride_bytes,
                        u32 width,
                        u32 height,
                        u32 bytes_per_pixel) {
    XAxiVdma_Config *cfg;
    XAxiVdma_DmaSetup setup;
    XStatus status;

    if ((ctx == 0) ||
        (frame_stride_bytes == 0U) ||
        (width == 0U) ||
        (height == 0U) ||
        (bytes_per_pixel == 0U)) {
        return XST_FAILURE;
    }

    cfg = XAxiVdma_LookupConfig(vdma_base_addr);
    if (cfg == 0) {
        return XST_FAILURE;
    }

    status = XAxiVdma_CfgInitialize(&ctx->vdma, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    if (cfg->HasMm2S == 0U) {
        return XST_FAILURE;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->bytes_per_pixel = bytes_per_pixel;
    ctx->line_stride_bytes = width * bytes_per_pixel;
    ctx->frame_size_bytes = ctx->line_stride_bytes * height;
    ctx->frame_count = 3U;
    ctx->is_ready = 0U;

    ctx->frame_addrs[0] = frame0_addr;
    ctx->frame_addrs[1] = frame0_addr + frame_stride_bytes;
    ctx->frame_addrs[2] = frame0_addr + (2U * frame_stride_bytes);

    setup.VertSizeInput = height;
    setup.HoriSizeInput = ctx->line_stride_bytes;
    setup.Stride = ctx->line_stride_bytes;
    setup.FrameDelay = 0;
    /*
     * HDMI 显示是持续输出场景，底层配置必须以 circular mode 启动。
     * 后续切换显示帧时，再通过 StartParking() 临时切到指定 frame。
     */
    setup.EnableCircularBuf = 1;
    setup.EnableSync = 0;
    setup.PointNum = 0;
    /*
     * 这里必须保持 0。对 MM2S 来说，EnableFrameCounter=1 不是“每帧中断”，
     * 而是“只传固定数量的 frame 后自动停机”。
     */
    setup.EnableFrameCounter = 0;
    setup.FixedFrameStoreAddr = 0;
    setup.GenLockRepeat = 0;
    setup.EnableVFlip = 0;

    status = XAxiVdma_DmaConfig(&ctx->vdma, XAXIVDMA_READ, &setup);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XAxiVdma_DmaSetBufferAddr(&ctx->vdma, XAXIVDMA_READ, ctx->frame_addrs);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XAxiVdma_DmaStart(&ctx->vdma, XAXIVDMA_READ);
    if (status != XST_SUCCESS) {
        return status;
    }

    /*
     * 先启动通道，再 park 到一个确定的初始 frame。
     * 不要把这一步和 EnableFrameCounter 混用，否则会出现每帧自停后
     * 软件再反复“recover”的假故障现象。
     */
    status = XAxiVdma_StartParking(&ctx->vdma, 0, XAXIVDMA_READ);
    if (status != XST_SUCCESS) {
        return status;
    }

    ctx->is_ready = 1U;
    return XST_SUCCESS;
}

XStatus PsHdmiVdma_Park(PsHdmiVdma *ctx, u32 frame_idx) {
    XStatus status;

    status = PsHdmiVdma_ValidateFrameIndex(ctx, frame_idx);
    if (status != XST_SUCCESS) {
        return status;
    }

    /* Park 仅切换当前显示帧，不会改变初始化时的 continuous/circular 策略。 */
    return XAxiVdma_StartParking(&ctx->vdma, (int)frame_idx, XAXIVDMA_READ);
}

XStatus PsHdmiVdma_EnableCircular(PsHdmiVdma *ctx) {
    if ((ctx == 0) || (ctx->is_ready == 0U)) {
        return XST_FAILURE;
    }

    /* 退出 park 后恢复 circular mode，让 VDMA 在 frame store 间持续运行。 */
    XAxiVdma_StopParking(&ctx->vdma, XAXIVDMA_READ);
    return XST_SUCCESS;
}

XStatus PsHdmiVdma_FillFrame(PsHdmiVdma *ctx, u32 frame_idx, u32 color_xrgb8888) {
    u32 pixel_count;
    u32 i;
    u32 *fb32;
    XStatus status;

    status = PsHdmiVdma_ValidateFrameIndex(ctx, frame_idx);
    if (status != XST_SUCCESS) {
        return status;
    }

    fb32 = (u32 *)ctx->frame_addrs[frame_idx];
    pixel_count = ctx->width * ctx->height;

    for (i = 0U; i < pixel_count; ++i) {
        fb32[i] = color_xrgb8888;
    }

    Xil_DCacheFlushRange((INTPTR)ctx->frame_addrs[frame_idx], (INTPTR)ctx->frame_size_bytes);
    return XST_SUCCESS;
}

XStatus PsHdmiVdma_FillAllFrames(PsHdmiVdma *ctx, u32 color_xrgb8888) {
    u32 idx;
    XStatus status;

    if ((ctx == 0) || (ctx->is_ready == 0U)) {
        return XST_FAILURE;
    }

    for (idx = 0U; idx < ctx->frame_count; ++idx) {
        status = PsHdmiVdma_FillFrame(ctx, idx, color_xrgb8888);
        if (status != XST_SUCCESS) {
            return status;
        }
    }

    return XST_SUCCESS;
}
