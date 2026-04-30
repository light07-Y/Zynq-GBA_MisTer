#ifndef PS_APP_DIAG_H
#define PS_APP_DIAG_H

#include "Diagnostics/Inc/ps_app_diag_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void PsAppDiag_PrintStatus(PsAppDiagContext *ctx);
void PsAppDiag_PrintDiag(PsAppDiagContext *ctx);
void PsAppDiag_PrintProbe(PsAppDiagContext *ctx);
void PsAppDiag_PrintTrace(PsAppDiagContext *ctx, u32 samples, u32 interval_ms);
void PsAppDiag_PrintHangSnapshot(PsAppDiagContext *ctx, u32 samples, u32 interval_ms);
void PsAppDiag_PrintLaunchTrace(PsAppDiagContext *ctx, u32 samples, u32 interval_ms);
void PsAppDiag_PrintRuntimeSample(PsAppDiagContext *ctx, const char *tag);
void PsAppDiag_PrintDdrLogSnapshot(PsAppDiagContext *ctx, const char *tag);
void PsAppDiag_ServiceDdrLog(PsAppDiagContext *ctx,
                             u32 status1,
                             u32 dbg_pc,
                             u32 dbg_mem,
                             u32 dbg_dma);
void PsAppDiag_PrintConfigReadback(PsAppDiagContext *ctx, const char *tag);
void PsAppDiag_PrintChainSnapshot(PsAppDiagContext *ctx, const char *tag);
void PsAppDiag_PrintVdmaSnapshot(PsAppDiagContext *ctx, const char *tag);
void PsAppDiag_PrintRomHeader(PsAppDiagContext *ctx);
void PsAppDiag_PrintRomProbe(PsAppDiagContext *ctx);
void PsAppDiag_PrintAutoBootAudit(PsAppDiagContext *ctx);
void PsAppDiag_MaybePrintStallAudit(PsAppDiagContext *ctx,
                                    u32 status1,
                                    u32 dbg_pc,
                                    u32 dbg_mem,
                                    u32 dbg_dma);
void PsAppDiag_ScanFramebufferForAnomaly(PsAppDiagContext *ctx, const char *tag, u8 force_print);
void PsAppDiag_PrintPixcapSummary(PsAppDiagContext *ctx, u32 buf_idx);
void PsAppDiag_PrintPixcapRow(PsAppDiagContext *ctx, u32 buf_idx, u32 y);
void PsAppDiag_PrintPixcapCompare(PsAppDiagContext *ctx, u32 buf_idx, u32 frame_idx, u32 x, u32 y);

#ifdef __cplusplus
}
#endif

#endif
