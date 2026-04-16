#ifndef PS_GBA_REGS_H
#define PS_GBA_REGS_H

#include "xstatus.h"
#include "xil_io.h"
#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    UINTPTR base_addr;
} PsGbaRegs;

enum {
    GBA_REG_CTRL          = 0x000,
    GBA_REG_KEYS          = 0x004,
    GBA_REG_MAX_PAK_ADDR  = 0x008,
    GBA_REG_CYCLE_PRECALC = 0x00C,
    GBA_REG_RTC_TIMESTAMP = 0x010,
    GBA_REG_COMMIT        = 0x014,
    GBA_REG_STATUS0       = 0x018,
    GBA_REG_STATUS1       = 0x01C,
    GBA_REG_SW_RESET      = 0x020,
    GBA_REG_ROM_STATUS    = 0x024,
    GBA_REG_ERROR_LATCH   = 0x028,
    GBA_REG_IRQ_EN        = 0x02C,
    GBA_REG_IRQ_STS       = 0x030,
    GBA_REG_DEBUG_CPU_PC  = 0x034,
    GBA_REG_DEBUG_CPU_MIX = 0x038,
    GBA_REG_DEBUG_IRQ     = 0x03C,
    GBA_REG_DEBUG_DMA     = 0x040,
    GBA_REG_DEBUG_MEM     = 0x044,
    GBA_REG_DISPLAY_FRAME = 0x048,
    GBA_REG_DBG_CHAIN_FLAGS        = 0x04C,
    GBA_REG_DBG_CHAIN_COUNTS0      = 0x050,
    GBA_REG_DBG_CHAIN_COUNTS1      = 0x054,
    GBA_REG_DBG_CH1_FIRST_ADDR     = 0x058,
    GBA_REG_DBG_CH1_FIRST_META     = 0x05C,
    GBA_REG_DBG_CH1_LAST_ADDR      = 0x060,
    GBA_REG_DBG_CH1_LAST_META      = 0x064,
    GBA_REG_DBG_DDR_FIRST_ADDR     = 0x068,
    GBA_REG_DBG_DDR_FIRST_META     = 0x06C,
    GBA_REG_DBG_DDR_LAST_ADDR      = 0x070,
    GBA_REG_DBG_DDR_LAST_META      = 0x074,
    GBA_REG_DBG_AXI_AR_FIRST_ADDR  = 0x078,
    GBA_REG_DBG_AXI_AR_FIRST_META  = 0x07C,
    GBA_REG_DBG_AXI_AR_LAST_ADDR   = 0x080,
    GBA_REG_DBG_AXI_AR_LAST_META   = 0x084,
    GBA_REG_DBG_AXI_R_FIRST_ADDR   = 0x088,
    GBA_REG_DBG_AXI_R_FIRST_META   = 0x08C,
    GBA_REG_DBG_AXI_R_LAST_ADDR    = 0x090,
    GBA_REG_DBG_AXI_R_LAST_META    = 0x094,
    GBA_REG_DBG_DONE_FIRST_ADDR    = 0x098,
    GBA_REG_DBG_DONE_FIRST_META    = 0x09C,
    GBA_REG_DBG_DONE_LAST_ADDR     = 0x0A0,
    GBA_REG_DBG_DONE_LAST_META     = 0x0A4,
    GBA_REG_FB_CAP_STATUS          = 0x0A8,
    GBA_REG_FB_CAP_SEQ             = 0x0AC,
    GBA_REG_SAVE_STATUS            = 0x0B0,
    GBA_REG_BIOS_WR_ADDR           = 0x0B4,
    GBA_REG_BIOS_WR_DATA           = 0x0B8,
    GBA_REG_BIOS_WR_REQ            = 0x0BC,
    GBA_REG_BIOS_WR_ACK            = 0x0C0
};

enum {
    GBA_CTRL_CORE_ON         = (1U << 0),
    GBA_CTRL_LOCK_SPEED      = (1U << 1),
    GBA_CTRL_CPU_TURBO       = (1U << 2),
    GBA_CTRL_SRAM_FLASH_EN   = (1U << 4),
    GBA_CTRL_MEMORY_REMAP    = (1U << 5),
    GBA_CTRL_ROM_LOADING     = (1U << 8),
    GBA_CTRL_FLASH_1M        = (1U << 9),
    GBA_CTRL_SPECIAL_GPIO    = (1U << 10),
    GBA_CTRL_TILT            = (1U << 11),
    GBA_CTRL_SRAM_32K_MIRROR_TEST = (1U << 13)
};

void PsGbaRegs_Init(PsGbaRegs *ctx, UINTPTR base_addr);
void PsGbaRegs_CommitConfig(PsGbaRegs *ctx,
                            u32 ctrl,
                            u32 keys,
                            u32 max_pak_addr,
                            u32 cycle_precalc,
                            u32 rtc_timestamp);
void PsGbaRegs_ApplyBootDefaults(PsGbaRegs *ctx);
void PsGbaRegs_SetIrqEnable(PsGbaRegs *ctx, u32 irq_mask);
void PsGbaRegs_ClearIrqStatus(PsGbaRegs *ctx, u32 irq_w1c_bits);
void PsGbaRegs_ClearErrorLatch(PsGbaRegs *ctx);
void PsGbaRegs_SetSwReset(PsGbaRegs *ctx, u32 asserted);
void PsGbaRegs_SetDisplayFrameIdx(PsGbaRegs *ctx, u32 frame_idx);
u32 PsGbaRegs_ReadBiosAckSeq(PsGbaRegs *ctx);
void PsGbaRegs_StageBiosWord(PsGbaRegs *ctx, u32 word_addr, u32 word_data);
void PsGbaRegs_RequestBiosWrite(PsGbaRegs *ctx);
XStatus PsGbaRegs_WriteBiosWord(PsGbaRegs *ctx, u32 word_addr, u32 word_data, u32 timeout_loops);

u32 PsGbaRegs_Read(PsGbaRegs *ctx, u32 reg_offset);

#ifdef __cplusplus
}
#endif

#endif
