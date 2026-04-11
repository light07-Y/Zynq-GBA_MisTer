#ifndef PS_HDMI_VDMA_H
#define PS_HDMI_VDMA_H

#include "xaxivdma.h"
#include "xstatus.h"
#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    XAxiVdma vdma;
    UINTPTR frame_addrs[3];
    u32 width;
    u32 height;
    u32 bytes_per_pixel;
    u32 line_stride_bytes;
    u32 frame_size_bytes;
    u32 frame_count;
    u8 is_ready;
} PsHdmiVdma;

XStatus PsHdmiVdma_Init(PsHdmiVdma *ctx,
                        u32 vdma_base_addr,
                        UINTPTR frame0_addr,
                        u32 frame_stride_bytes,
                        u32 width,
                        u32 height,
                        u32 bytes_per_pixel);
XStatus PsHdmiVdma_Park(PsHdmiVdma *ctx, u32 frame_idx);
XStatus PsHdmiVdma_EnableCircular(PsHdmiVdma *ctx);
XStatus PsHdmiVdma_FillFrame(PsHdmiVdma *ctx, u32 frame_idx, u32 color_xrgb8888);
XStatus PsHdmiVdma_FillAllFrames(PsHdmiVdma *ctx, u32 color_xrgb8888);
XStatus PsHdmiVdma_DrawTestPattern(PsHdmiVdma *ctx, u32 frame_idx, u32 seed);

#ifdef __cplusplus
}
#endif

#endif
