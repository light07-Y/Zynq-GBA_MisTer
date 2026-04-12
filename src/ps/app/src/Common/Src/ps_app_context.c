#include "Common/Inc/ps_app_context.h"

#include <string.h>

void PsAppContext_Init(PsAppContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->video_ctx.vdma = &ctx->vdma;
    ctx->video_ctx.regs = &ctx->regs;
    ctx->video_ctx.state = &ctx->video;
    ctx->video_ctx.rom = &ctx->rom;

    ctx->diag_ctx.regs = &ctx->regs;
    ctx->diag_ctx.vdma = &ctx->vdma;
    ctx->diag_ctx.config = &ctx->config;
    ctx->diag_ctx.audio = &ctx->audio;
    ctx->diag_ctx.video = &ctx->video_ctx;
    ctx->diag_ctx.rom = &ctx->rom;
    ctx->diag_ctx.diag = &ctx->diag;

    ctx->runtime_ctx.codec = &ctx->codec;
    ctx->runtime_ctx.vdma = &ctx->vdma;
    ctx->runtime_ctx.regs = &ctx->regs;
    ctx->runtime_ctx.uart = &ctx->uart;
    ctx->runtime_ctx.config = &ctx->config;
    ctx->runtime_ctx.audio = &ctx->audio;
    ctx->runtime_ctx.video = &ctx->video;
    ctx->runtime_ctx.rom = &ctx->rom;
    ctx->runtime_ctx.diag = &ctx->diag;
    ctx->runtime_ctx.video_ctx = &ctx->video_ctx;
    ctx->runtime_ctx.diag_ctx = &ctx->diag_ctx;

    ctx->console_ctx.runtime = &ctx->runtime_ctx;
    ctx->console_ctx.video = &ctx->video_ctx;
    ctx->console_ctx.diag = &ctx->diag_ctx;
    ctx->console_ctx.uart = &ctx->uart;
}
