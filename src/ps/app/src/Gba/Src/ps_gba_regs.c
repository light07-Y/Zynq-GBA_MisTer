#include "Gba/Inc/ps_gba_regs.h"

void PsGbaRegs_Init(PsGbaRegs *ctx, UINTPTR base_addr) {
    if (ctx == 0) {
        return;
    }
    ctx->base_addr = base_addr;
}

u32 PsGbaRegs_Read(PsGbaRegs *ctx, u32 reg_offset) {
    if (ctx == 0) {
        return 0U;
    }
    return Xil_In32(ctx->base_addr + reg_offset);
}

void PsGbaRegs_CommitConfig(PsGbaRegs *ctx,
                            u32 ctrl,
                            u32 keys,
                            u32 max_pak_addr,
                            u32 cycle_precalc,
                            u32 rtc_timestamp) {
    if (ctx == 0) {
        return;
    }

    Xil_Out32(ctx->base_addr + GBA_REG_CTRL, ctrl);
    Xil_Out32(ctx->base_addr + GBA_REG_KEYS, keys & 0x3FFU);
    Xil_Out32(ctx->base_addr + GBA_REG_MAX_PAK_ADDR, max_pak_addr & 0x1FFFFFFU);
    Xil_Out32(ctx->base_addr + GBA_REG_CYCLE_PRECALC, cycle_precalc & 0xFFFFU);
    Xil_Out32(ctx->base_addr + GBA_REG_RTC_TIMESTAMP, rtc_timestamp);

    Xil_Out32(ctx->base_addr + GBA_REG_COMMIT, 1U);
}

void PsGbaRegs_ApplyBootDefaults(PsGbaRegs *ctx) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_Read(ctx, GBA_REG_CTRL);
    ctrl |= (GBA_CTRL_CORE_ON | GBA_CTRL_LOCK_SPEED | GBA_CTRL_SRAM_FLASH_EN);

    PsGbaRegs_CommitConfig(ctx,
                           ctrl,
                           0U,
                           0U,
                           100U,
                           0U);

    Xil_Out32(ctx->base_addr + GBA_REG_SW_RESET, 0U);
}

void PsGbaRegs_SetIrqEnable(PsGbaRegs *ctx, u32 irq_mask) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + GBA_REG_IRQ_EN, irq_mask);
}

void PsGbaRegs_ClearIrqStatus(PsGbaRegs *ctx, u32 irq_w1c_bits) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + GBA_REG_IRQ_STS, irq_w1c_bits & 0x3U);
}

void PsGbaRegs_ClearErrorLatch(PsGbaRegs *ctx) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + GBA_REG_ERROR_LATCH, 1U);
}

void PsGbaRegs_SetSwReset(PsGbaRegs *ctx, u32 asserted) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + GBA_REG_SW_RESET, asserted & 0x1U);
}

void PsGbaRegs_SetDisplayFrameIdx(PsGbaRegs *ctx, u32 frame_idx) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + GBA_REG_DISPLAY_FRAME, frame_idx & 0x3U);
}

u32 PsGbaRegs_ReadBiosAckSeq(PsGbaRegs *ctx) {
    if (ctx == 0) {
        return 0U;
    }
    return Xil_In32(ctx->base_addr + GBA_REG_BIOS_WR_ACK);
}

void PsGbaRegs_StageBiosWord(PsGbaRegs *ctx, u32 word_addr, u32 word_data) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + GBA_REG_BIOS_WR_ADDR, word_addr & 0xFFFU);
    Xil_Out32(ctx->base_addr + GBA_REG_BIOS_WR_DATA, word_data);
}

void PsGbaRegs_RequestBiosWrite(PsGbaRegs *ctx) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + GBA_REG_BIOS_WR_REQ, 1U);
}

XStatus PsGbaRegs_WriteBiosWord(PsGbaRegs *ctx,
                                u32 word_addr,
                                u32 word_data,
                                u32 timeout_loops) {
    u32 ack_before;
    u32 spin;

    if (ctx == 0) {
        return XST_FAILURE;
    }

    if (timeout_loops == 0U) {
        timeout_loops = 2000000U;
    }

    ack_before = PsGbaRegs_ReadBiosAckSeq(ctx);
    PsGbaRegs_StageBiosWord(ctx, word_addr, word_data);
    PsGbaRegs_RequestBiosWrite(ctx);

    for (spin = 0U; spin < timeout_loops; ++spin) {
        if (PsGbaRegs_ReadBiosAckSeq(ctx) != ack_before) {
            return XST_SUCCESS;
        }
    }

    return XST_FAILURE;
}
