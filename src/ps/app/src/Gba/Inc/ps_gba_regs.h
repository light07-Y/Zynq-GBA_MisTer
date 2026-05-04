#ifndef PS_GBA_REGS_H
#define PS_GBA_REGS_H

#include "xstatus.h"
#include "xil_io.h"
#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsGbaRegs {
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
    GBA_REG_BIOS_WR_ACK            = 0x0C0,
    GBA_REG_FEATURE_CTRL           = 0x0C4,
    GBA_REG_FEATURE_ACTION         = 0x0C8,
    GBA_REG_SAVESTATE_SLOT         = 0x0CC,
    GBA_REG_CHEAT_FLAGS            = 0x0D0,
    GBA_REG_CHEAT_ADDR             = 0x0D4,
    GBA_REG_CHEAT_COMPARE          = 0x0D8,
    GBA_REG_CHEAT_REPLACE          = 0x0DC,
    GBA_REG_SENSOR_INPUT           = 0x0E0,
    GBA_REG_RTC_IN_SAVEDTIME_LO    = 0x0E4,
    GBA_REG_RTC_IN_SAVEDTIME_HI    = 0x0E8,
    GBA_REG_RTC_OUT_TIMESTAMP      = 0x0EC,
    GBA_REG_RTC_OUT_SAVEDTIME_LO   = 0x0F0,
    GBA_REG_RTC_OUT_SAVEDTIME_HI   = 0x0F4,
    GBA_REG_FEATURE_STATUS         = 0x0F8,
    GBA_REG_FEATURE_STATUS_CLR     = 0x0FC,
    GBA_REG_RTC_TIMESTAMP_SAVED    = 0x100,
    GBA_REG_DEBUG_IRQ_EXT          = 0x104,
    GBA_REG_DEBUG_CPU_R0           = 0x108,
    GBA_REG_DEBUG_CPU_R1           = 0x10C,
    GBA_REG_DEBUG_CPU_R2           = 0x110,
    GBA_REG_DEBUG_CPU_R4           = 0x114,
    GBA_REG_DEBUG_CPU_CPUSET       = 0x118,
    GBA_REG_DDR_ATOM_WORD0         = 0x11C,
    GBA_REG_DDR_ATOM_WORD1         = 0x120,
    GBA_REG_DDR_ATOM_NEXT0         = 0x124,
    GBA_REG_DDR_ATOM_NEXT1         = 0x128,
    GBA_REG_DDR_ATOM_STAT          = 0x12C,
    GBA_REG_DEBUG_VRAM_REGS0       = 0x130,
    GBA_REG_DEBUG_VRAM_REGS1       = 0x134,
    GBA_REG_DEBUG_VRAM_REGS2       = 0x138,
    GBA_REG_DEBUG_VRAM_REGS3       = 0x13C,
    GBA_REG_DEBUG_VRAM_REGS4       = 0x140,
    GBA_REG_DEBUG_VRAM_HITS        = 0x144,
    GBA_REG_DEBUG_VRAM_RING0       = 0x148,
    GBA_REG_DEBUG_VRAM_RING1       = 0x14C,
    GBA_REG_DEBUG_VRAM_RING2       = 0x150,
    GBA_REG_DEBUG_VRAM_RING3       = 0x154,
    GBA_REG_DEBUG_VRAM_DIST0       = 0x158,
    GBA_REG_DEBUG_VRAM_DIST1       = 0x15C,
    GBA_REG_DEBUG_DMA_TR0          = 0x160,
    GBA_REG_DEBUG_DMA_TR1          = 0x164,
    GBA_REG_DEBUG_DMA_TR2          = 0x168,
    GBA_REG_DEBUG_DMA_TR3          = 0x16C,
    GBA_REG_DEBUG_DMA_TR4          = 0x170,
    GBA_REG_DEBUG_DMA_TR5          = 0x174,
    GBA_REG_DEBUG_DMA_TR6          = 0x178,
    GBA_REG_DEBUG_DMA_TR7          = 0x17C,
    GBA_REG_DEBUG_DMA_TR8          = 0x180,
    GBA_REG_DEBUG_WAIT0            = 0x184,
    GBA_REG_DEBUG_WAIT1            = 0x188,
    GBA_REG_DEBUG_WAIT2            = 0x18C,
    GBA_REG_DEBUG_WAIT3            = 0x190
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
    GBA_CTRL_SRAM_32K_MIRROR_TEST = (1U << 13),
    /* Root-cause verification mode:
     * force ROM DDR path to single-beat fetch + no read-ahead/prefetch. */
    GBA_CTRL_ROM_DDR_SAFE    = (1U << 14)
};

enum {
    GBA_FEATURE_CTRL_REWIND_ON         = (1U << 0),
    GBA_FEATURE_CTRL_CHEATS_ENABLED    = (1U << 1),
    GBA_FEATURE_CTRL_RUMBLE_FORWARD_EN = (1U << 2),
    GBA_FEATURE_CTRL_SENSOR_AUTO_EN    = (1U << 3),
    GBA_FEATURE_CTRL_RTC_AUTO_UPDATE_EN = (1U << 4),
    /* 扩展位：运行时按住 R3 时拉高该位驱动 core 的 rewind_active。 */
    GBA_FEATURE_CTRL_REWIND_ACTIVE_REQ = (1U << 5)
};

enum {
    GBA_FEATURE_ACTION_SAVE_TRIG       = (1U << 0),
    GBA_FEATURE_ACTION_LOAD_TRIG       = (1U << 1),
    GBA_FEATURE_ACTION_CHEAT_PUSH_TRIG = (1U << 2),
    GBA_FEATURE_ACTION_CHEAT_CLEAR_TRIG = (1U << 3),
    GBA_FEATURE_ACTION_RTC_NEW_TRIG    = (1U << 4)
};

enum {
    GBA_FEATURE_STATUS_SAVESTATE_BUSY   = (1U << 0),
    GBA_FEATURE_STATUS_LOAD_DONE_LATCHED = (1U << 1),
    GBA_FEATURE_STATUS_REWIND_ACTIVE    = (1U << 2),
    GBA_FEATURE_STATUS_RUMBLE_OUT       = (1U << 3),
    GBA_FEATURE_STATUS_CHEATS_ACTIVE    = (1U << 4),
    GBA_FEATURE_STATUS_RTC_INUSE        = (1U << 5),
    GBA_FEATURE_STATUS_SLOT_ECHO_SHIFT  = 6,
    GBA_FEATURE_STATUS_SLOT_ECHO_MASK   = (0x7U << GBA_FEATURE_STATUS_SLOT_ECHO_SHIFT),
    GBA_FEATURE_STATUS_SOLAR_ECHO_SHIFT = 9,
    GBA_FEATURE_STATUS_SOLAR_ECHO_MASK  = (0x7U << GBA_FEATURE_STATUS_SOLAR_ECHO_SHIFT)
};

typedef struct {
    u32 flags;
    u32 addr;
    u32 compare;
    u32 replace;
} PsGbaCheatCodeWords;

typedef struct {
    u32 timestamp;
    u64 savedtime;
    u8 save_loaded;
} PsGbaRtcOut;

void PsGbaRegs_Init(PsGbaRegs *ctx, UINTPTR base_addr);
void PsGbaRegs_CommitConfig(PsGbaRegs *ctx,
                            u32 ctrl,
                            u32 keys,
                            u32 max_pak_addr,
                            u32 cycle_precalc,
                            u32 rtc_timestamp,
                            u32 rtc_timestamp_saved);
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
void PsGbaRegs_SetFeatureCtrl(PsGbaRegs *ctx, u32 feature_ctrl);
void PsGbaRegs_SetSavestateSlot(PsGbaRegs *ctx, u32 slot);
void PsGbaRegs_SetSensorInput(PsGbaRegs *ctx, u32 solar_level, s8 tilt_x, s8 tilt_y);
void PsGbaRegs_SetCheatCodeWords(PsGbaRegs *ctx, const PsGbaCheatCodeWords *code_words);
void PsGbaRegs_TriggerFeatureAction(PsGbaRegs *ctx, u32 action_mask);
u32 PsGbaRegs_ReadFeatureStatus(PsGbaRegs *ctx);
void PsGbaRegs_ClearFeatureStatus(PsGbaRegs *ctx, u32 clear_mask);
void PsGbaRegs_ReadRtcOut(PsGbaRegs *ctx, PsGbaRtcOut *out);
void PsGbaRegs_WriteRtcSavedTimeIn(PsGbaRegs *ctx, u64 savedtime, u8 save_loaded);

u32 PsGbaRegs_Read(PsGbaRegs *ctx, u32 reg_offset);

#ifdef __cplusplus
}
#endif

#endif
