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

void PsGbaRegs_Write(PsGbaRegs *ctx, u32 reg_offset, u32 value) {
    if (ctx == 0) {
        return;
    }
    Xil_Out32(ctx->base_addr + reg_offset, value);
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

    ctrl &= GBA_CTRL_VALID_MASK;
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

    ctrl = PsGbaRegs_Read(ctx, GBA_REG_CTRL) & GBA_CTRL_VALID_MASK;
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

static u32 PsGbaRegs_ReadStateCtrlBase(PsGbaRegs *ctx) {
    u32 ctrl;

    ctrl = PsGbaRegs_Read(ctx, GBA_REG_STATE_CTRL);
    ctrl &= ~(GBA_STATE_CTRL_SAVE_TRIG | GBA_STATE_CTRL_LOAD_TRIG);
    return ctrl;
}

static u32 PsGbaRegs_ReadCheatCtrlBase(PsGbaRegs *ctx) {
    u32 ctrl;

    ctrl = PsGbaRegs_Read(ctx, GBA_REG_CHEAT_CTRL);
    ctrl &= ~(GBA_CHEAT_CTRL_CLEAR_TRIG | GBA_CHEAT_CTRL_PUSH_TRIG);
    return ctrl;
}

void PsGbaRegs_SetStateSlot(PsGbaRegs *ctx, u32 slot) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_ReadStateCtrlBase(ctx);
    ctrl &= ~GBA_STATE_CTRL_SLOT_MASK;
    ctrl |= (slot & GBA_STATE_CTRL_SLOT_MASK);
    PsGbaRegs_Write(ctx, GBA_REG_STATE_CTRL, ctrl);
}

void PsGbaRegs_SetRewindControl(PsGbaRegs *ctx, u32 enable, u32 active) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_ReadStateCtrlBase(ctx);
    ctrl &= ~(GBA_STATE_CTRL_REWIND_ENABLE | GBA_STATE_CTRL_REWIND_ACTIVE);
    if ((enable & 0x1U) != 0U) {
        ctrl |= GBA_STATE_CTRL_REWIND_ENABLE;
    }
    if ((active & 0x1U) != 0U) {
        ctrl |= GBA_STATE_CTRL_REWIND_ACTIVE;
    }
    PsGbaRegs_Write(ctx, GBA_REG_STATE_CTRL, ctrl);
}

void PsGbaRegs_TriggerSaveState(PsGbaRegs *ctx) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_ReadStateCtrlBase(ctx);
    ctrl |= GBA_STATE_CTRL_SAVE_TRIG;
    PsGbaRegs_Write(ctx, GBA_REG_STATE_CTRL, ctrl);
}

void PsGbaRegs_TriggerLoadState(PsGbaRegs *ctx) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_ReadStateCtrlBase(ctx);
    ctrl |= GBA_STATE_CTRL_LOAD_TRIG;
    PsGbaRegs_Write(ctx, GBA_REG_STATE_CTRL, ctrl);
}

void PsGbaRegs_SetCheatEnable(PsGbaRegs *ctx, u32 enable) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_ReadCheatCtrlBase(ctx);
    ctrl &= ~GBA_CHEAT_CTRL_ENABLE;
    if ((enable & 0x1U) != 0U) {
        ctrl |= GBA_CHEAT_CTRL_ENABLE;
    }
    PsGbaRegs_Write(ctx, GBA_REG_CHEAT_CTRL, ctrl);
}

void PsGbaRegs_TriggerCheatClear(PsGbaRegs *ctx) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_ReadCheatCtrlBase(ctx);
    ctrl |= GBA_CHEAT_CTRL_CLEAR_TRIG;
    PsGbaRegs_Write(ctx, GBA_REG_CHEAT_CTRL, ctrl);
}

void PsGbaRegs_TriggerCheatPush(PsGbaRegs *ctx) {
    u32 ctrl;

    if (ctx == 0) {
        return;
    }

    ctrl = PsGbaRegs_ReadCheatCtrlBase(ctx);
    ctrl |= GBA_CHEAT_CTRL_PUSH_TRIG;
    PsGbaRegs_Write(ctx, GBA_REG_CHEAT_CTRL, ctrl);
}

void PsGbaRegs_SetCheatWords(PsGbaRegs *ctx, u32 w0, u32 w1, u32 w2, u32 w3) {
    if (ctx == 0) {
        return;
    }

    PsGbaRegs_Write(ctx, GBA_REG_CHEAT_WORD0, w0);
    PsGbaRegs_Write(ctx, GBA_REG_CHEAT_WORD1, w1);
    PsGbaRegs_Write(ctx, GBA_REG_CHEAT_WORD2, w2);
    PsGbaRegs_Write(ctx, GBA_REG_CHEAT_WORD3, w3);
}

void PsGbaRegs_PushCheatWords(PsGbaRegs *ctx, u32 w0, u32 w1, u32 w2, u32 w3) {
    PsGbaRegs_SetCheatWords(ctx, w0, w1, w2, w3);
    PsGbaRegs_TriggerCheatPush(ctx);
}

void PsGbaRegs_SetRtcSavedState(PsGbaRegs *ctx, u32 timestamp_saved, u64 saved_time, u32 loaded) {
    u32 lo;
    u32 hi;

    if (ctx == 0) {
        return;
    }

    lo = (u32)(saved_time & 0xFFFFFFFFULL);
    hi = (u32)((saved_time >> 32U) & 0x3FFULL);
    if ((loaded & 0x1U) != 0U) {
        hi |= (1U << 10);
    }

    PsGbaRegs_Write(ctx, GBA_REG_RTC_SAVED_TS, timestamp_saved);
    PsGbaRegs_Write(ctx, GBA_REG_RTC_SAVEDTIME_LO, lo);
    PsGbaRegs_Write(ctx, GBA_REG_RTC_SAVEDTIME_HI, hi);
}

void PsGbaRegs_SetSensorState(PsGbaRegs *ctx, u32 solar, s8 tilt_x, s8 tilt_y) {
    u32 sensor;

    if (ctx == 0) {
        return;
    }

    sensor = (solar & 0x7U);
    sensor |= (((u32)(u8)tilt_x) & 0xFFU) << 8;
    sensor |= (((u32)(u8)tilt_y) & 0xFFU) << 16;
    PsGbaRegs_Write(ctx, GBA_REG_SENSOR, sensor);
}

u32 PsGbaRegs_ReadFeatureStatus(PsGbaRegs *ctx) {
    return PsGbaRegs_Read(ctx, GBA_REG_FEATURE_STATUS);
}

u32 PsGbaRegs_ReadRtcTimestampOut(PsGbaRegs *ctx) {
    return PsGbaRegs_Read(ctx, GBA_REG_RTC_OUT_TIMESTAMP);
}

u64 PsGbaRegs_ReadRtcSavedTimeOut(PsGbaRegs *ctx) {
    u32 lo;
    u32 hi;

    lo = PsGbaRegs_Read(ctx, GBA_REG_RTC_OUT_SAVEDTIME_LO);
    hi = PsGbaRegs_Read(ctx, GBA_REG_RTC_OUT_SAVEDTIME_HI) & 0x3FFU;
    return (((u64)hi) << 32U) | ((u64)lo);
}
