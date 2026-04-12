#ifndef PS_APP_VIDEO_H
#define PS_APP_VIDEO_H

#include "xstatus.h"

#include "Video/Inc/ps_app_video_context.h"

#ifdef __cplusplus
extern "C" {
#endif

XStatus PsAppVideo_InitInterrupts(PsAppVideoContext *ctx);
XStatus PsAppVideo_RequestFrame(PsAppVideoContext *ctx, u32 frame_idx);
void PsAppVideo_AttemptRecover(PsAppVideoContext *ctx, u32 vdma_status, u32 vdma_errs);
void PsAppVideo_SyncDisplayFrame(PsAppVideoContext *ctx);
XStatus PsAppVideo_RenderBootFrames(PsAppVideoContext *ctx);
void PsAppVideo_PresentCapturedFrameIfReady(PsAppVideoContext *ctx);

u32 PsAppVideo_DefaultCaptureBuffer(PsAppVideoContext *ctx);
u32 PsAppVideo_DefaultFrameIndex(PsAppVideoContext *ctx);
void PsAppVideo_ReadCaptureStatus(PsAppVideoContext *ctx, u32 *seq_out, u32 *buf_out);
u32 PsAppVideo_ReadCaptureWord(u32 buf_idx, u32 word_idx);
u16 PsAppVideo_ReadCapturePixelRgb565(u32 buf_idx, u32 x, u32 y);
u32 PsAppVideo_ComputeCaptureSparseHash(u32 buf_idx, u32 samples);
u32 PsAppVideo_ComputeCaptureRowHash(u32 buf_idx, u32 y);
u32 PsAppVideo_ComputeSparseHash(UINTPTR base_addr, u32 bytes, u32 samples);
u32 PsAppVideo_ReadHdmiFramePixel(PsAppVideoContext *ctx, u32 frame_idx, u32 x, u32 y);
u32 PsAppVideo_ConvertRgb565ToXrgb8888(u16 pixel);
void PsAppVideo_ComputeDisplayWindow(PsAppVideoContext *ctx,
                                     u32 *x_out,
                                     u32 *y_out,
                                     u32 *w_out,
                                     u32 *h_out);

#ifdef __cplusplus
}
#endif

#endif
