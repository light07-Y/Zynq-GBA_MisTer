#include "Diagnostics/Inc/ps_app_diag.h"
#include "Diagnostics/Inc/ps_app_diag_disasm.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xaxivdma_hw.h"
#include "xil_cache.h"
#include "xil_printf.h"

#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Video/Inc/ps_app_video.h"

static void PsAppDiag_SanitizeAsciiField(char *dst,
                                         size_t dst_size,
                                         const u8 *src,
                                         size_t src_len) {
    size_t idx;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    if (src_len >= dst_size) {
        src_len = dst_size - 1U;
    }

    for (idx = 0U; idx < src_len; ++idx) {
        u8 ch = src[idx];
        dst[idx] = ((ch >= 32U) && (ch < 127U)) ? (char)ch : '.';
    }
    dst[src_len] = '\0';
}
static u8 PsAppDiag_ComputeGbaHeaderChecksum(const u8 *rom) {
    u32 idx;
    u32 checksum;

    if (rom == NULL) {
        return 0U;
    }

    checksum = 0U;
    for (idx = 0xA0U; idx <= 0xBCU; ++idx) {
        checksum = (checksum + rom[idx]) & 0xFFU;
    }
    checksum = (0x100U - ((checksum + 0x19U) & 0xFFU)) & 0xFFU;

    return (u8)checksum;
}
static u32 PsAppDiag_ClampWordOffset(u32 offset, u32 limit_bytes) {
    if (limit_bytes < 4U) {
        return 0U;
    }
    if (offset >= limit_bytes) {
        offset = limit_bytes - 4U;
    }
    return offset & ~0x3U;
}

static const char *PsAppDiag_BiosModeName(u8 bios_mode) {
    switch ((PsAppBiosMode)bios_mode) {
        case PS_APP_BIOS_MODE_EXTERNAL:
            return "external";
        case PS_APP_BIOS_MODE_FALLBACK:
            return "fallback";
        case PS_APP_BIOS_MODE_INTERNAL:
        default:
            return "internal";
    }
}

static const char *PsAppDiag_MemStateName(u32 state) {
    static const char *const names[] = {
        "IDLE", "READBIOS", "READSMALLRAM", "READPALETTERAM",
        "PALETTEDONE", "READVRAM", "VRAMDONE", "READOAMRAM",
        "OAMDONE", "WAIT_GBBUS", "WAIT_PROCBUS", "WAIT_SDRAM",
        "READAFTERPAK", "READ_UNREADABLE", "ROTATE", "READ_GPIO",
        "WAIT_WRAMRMW", "WRITE_WRAMLARGE", "WRITE_WRAMSMALL", "WRITE_REG",
        "WRITE_PALETTE", "WRITE_VRAM", "VRAMWAITWRITE", "WRITE_OAM",
        "EEPROMREAD", "EEPROM_WAITREAD", "EEPROMWRITE", "FLASHREAD",
        "FLASH_WAITREAD", "FLASHSRAMDECIDE1", "FLASHSRAMDECIDE2",
        "SRAMWRITE", "FLASHWRITE", "FLASH_WRITEBLOCK", "FLASH_BLOCKWAIT"
    };

    if (state < (sizeof(names) / sizeof(names[0]))) {
        return names[state];
    }
    return "UNKNOWN";
}

static const char *PsAppDiag_InferDdrBlocker(u32 mem_state,
                                             u32 flags,
                                             u32 counts0,
                                             u32 counts1,
                                             u32 err_latch) {
    u32 ch1_cnt = counts0 & 0xFFU;
    u32 ddr_cnt = (counts0 >> 8) & 0xFFU;
    u32 ar_cnt = (counts0 >> 16) & 0xFFU;
    u32 r_cnt = (counts0 >> 24) & 0xFFU;
    u32 dout_cnt = counts1 & 0xFFU;
    u32 done_cnt = (counts1 >> 8) & 0xFFU;
    u32 saturated = ((ch1_cnt == 0xFFU) &&
                     (ddr_cnt == 0xFFU) &&
                     (ar_cnt == 0xFFU) &&
                     (r_cnt == 0xFFU) &&
                     (dout_cnt == 0xFFU) &&
                     (done_cnt == 0xFFU)) ? 1U : 0U;

    if (err_latch != 0U) {
        return "AXI_ERROR_OR_TIMEOUT";
    }
    if ((mem_state == PS_APP_MEM_STATE_WAIT_SDRAM) && (saturated != 0U)) {
        return "CHAIN_COUNTERS_SATURATED";
    }
    if (mem_state != PS_APP_MEM_STATE_WAIT_SDRAM) {
        return "CPU_NOT_IN_WAIT_SDRAM";
    }
    if (((flags & (1U << 0)) == 0U) || (ch1_cnt == 0U)) {
        return "CACHE_DID_NOT_REQUEST_CH1";
    }
    if (((flags & (1U << 1)) == 0U) || (ddr_cnt < ch1_cnt)) {
        return "DDRAM_MUX_DID_NOT_ISSUE_READ";
    }
    if (((flags & (1U << 2)) == 0U) || (ar_cnt < ddr_cnt)) {
        return "AXI_AR_NOT_ACCEPTED";
    }
    if (((flags & (1U << 3)) == 0U) || (r_cnt < ar_cnt)) {
        return "WAITING_AXI_R_BEAT";
    }
    if (((flags & (1U << 4)) == 0U) || (dout_cnt < r_cnt)) {
        return "BACKEND_DOUT_READY_MISSING";
    }
    if (((flags & (1U << 5)) == 0U) || (done_cnt < dout_cnt)) {
        return "MEMORYMUX_CACHE_DONE_MISSING";
    }
    return "WAIT_SDRAM_AFTER_DONE_OR_STALE_CHAIN";
}

static const char *PsAppDiag_CpuModeName(u32 mode) {
    switch (mode & 0xFU) {
        case 0x0U: return "USER";
        case 0x1U: return "FIQ";
        case 0x2U: return "IRQ";
        case 0x3U: return "SVC";
        case 0x7U: return "ABT";
        case 0xBU: return "UND";
        case 0xFU: return "SYS";
        default: return "UNK";
    }
}

static const char *PsAppDiag_PcRegionName(u32 pc) {
    if (pc < 0x00004000U) {
        return "BIOS";
    }
    if ((pc >= 0x02000000U) && (pc <= 0x0203FFFFU)) {
        return "EWRAM";
    }
    if ((pc >= 0x03000000U) && (pc <= 0x03007FFFU)) {
        return "IWRAM";
    }
    if ((pc >= 0x04000000U) && (pc <= 0x040003FFU)) {
        return "IO";
    }
    if ((pc >= 0x05000000U) && (pc <= 0x050003FFU)) {
        return "PAL";
    }
    if ((pc >= 0x06000000U) && (pc <= 0x06017FFFU)) {
        return "VRAM";
    }
    if ((pc >= 0x07000000U) && (pc <= 0x070003FFU)) {
        return "OAM";
    }
    if ((pc >= 0x08000000U) && (pc <= 0x09FFFFFFU)) {
        return "ROM0";
    }
    if ((pc >= 0x0A000000U) && (pc <= 0x0BFFFFFFU)) {
        return "ROM1";
    }
    if ((pc >= 0x0C000000U) && (pc <= 0x0DFFFFFFU)) {
        return "ROM2";
    }
    return "INVALID";
}

static const char *PsAppDiag_BiosPcHint(u32 pc) {
    if ((pc >= 0x00000480U) && (pc <= 0x000004C0U)) {
        return "BIOS_WAIT_IRQ";
    }
    if ((pc >= 0x00000600U) && (pc <= 0x000006B0U)) {
        return "BIOS_INTRWAIT";
    }
    if (pc < 0x00004000U) {
        return "BIOS_OTHER";
    }
    return "-";
}

static void PsAppDiag_PrintCpuIrqDecode(const char *tag,
                                        u32 idx,
                                        u32 ctrl,
                                        u32 key_shadow,
                                        u32 sw_reset,
                                        u32 ps_irq_en,
                                        u32 ps_irq_sts,
                                        u32 rom_status,
                                        u32 status0,
                                        u32 status1,
                                        u32 dbg_pc,
                                        u32 dbg_mix,
                                        u32 dbg_irq,
                                        u32 dbg_irq_ext,
                                        u32 dbg_mem,
                                        u32 dbg_dma,
                                        u32 fbcap_seq,
                                        u32 fbcap_buf,
                                        u32 parked,
                                        u32 vdma_status) {
    u32 halt = dbg_mix & 0x1U;
    u32 thumb = (dbg_mix >> 5) & 0x1U;
    u32 mode = (dbg_mix >> 6) & 0xFU;
    u32 irq_disable = (dbg_mix >> 10) & 0x1U;
    u32 fiq_disable = (dbg_mix >> 11) & 0x1U;
    u32 iflags = dbg_irq & 0xFFFFU;
    u32 ime = (dbg_irq >> 16) & 0x1U;
    u32 vcount = (dbg_irq >> 17) & 0xFFU;
    u32 disp = (dbg_irq >> 25) & 0x7FU;
    u32 ie = dbg_irq_ext & 0xFFFFU;
    u32 enabled_pending = (dbg_irq_ext >> 16) & 0xFFFFU;
    u32 cpu_can_irq = ((ime != 0U) && (irq_disable == 0U) && (enabled_pending != 0U)) ? 1U : 0U;
    u32 mem_state = dbg_mem & 0xFFU;
    u32 vram_req_cnt = (dbg_mem >> 8) & 0xFFU;
    u32 vram_we_cnt = (dbg_mem >> 16) & 0xFFU;
    u32 vram_blk_cnt = (dbg_mem >> 24) & 0xFU;
    u32 vram_nz_seen = (dbg_mem >> 31) & 0x1U;

    xil_printf("[HANG] %s i=%u cfg ctrl=0x%08x reset=%u rom=%u romsafe=%u ps_irq_en=0x%x ps_irq_sts=0x%x key_shadow=0x%03x physical=0x%03x\r\n",
               tag,
               (unsigned int)idx,
               (unsigned int)ctrl,
               (unsigned int)(sw_reset & 0x1U),
               (unsigned int)(rom_status & 0x1U),
               (unsigned int)((ctrl & GBA_CTRL_ROM_DDR_SAFE) != 0U),
               (unsigned int)(ps_irq_en & 0x3U),
               (unsigned int)(ps_irq_sts & 0x3U),
               (unsigned int)(key_shadow & 0x3FFU),
               (unsigned int)(status0 & 0x3FFU));
    xil_printf("[HANG] %s i=%u pc=0x%08x region=%s hint=%s mix=0x%08x halt=%u thumb=%u mode=%s irq_dis=%u fiq_dis=%u\r\n",
               tag,
               (unsigned int)idx,
               (unsigned int)dbg_pc,
               PsAppDiag_PcRegionName(dbg_pc),
               PsAppDiag_BiosPcHint(dbg_pc),
               (unsigned int)dbg_mix,
               (unsigned int)halt,
               (unsigned int)thumb,
               PsAppDiag_CpuModeName(mode),
               (unsigned int)irq_disable,
               (unsigned int)fiq_disable);
    xil_printf("[HANG] %s i=%u irq IF=0x%04x IE=0x%04x IF_IE=0x%04x IME=%u cpu_can_irq=%u dispstat=0x%02x vblank=%u hblank=%u vcnt=%u vblank_ie=%u hblank_ie=%u vcnt_ie=%u vcount=%u\r\n",
               tag,
               (unsigned int)idx,
               (unsigned int)iflags,
               (unsigned int)ie,
               (unsigned int)enabled_pending,
               (unsigned int)ime,
               (unsigned int)cpu_can_irq,
               (unsigned int)disp,
               (unsigned int)((disp >> 0) & 0x1U),
               (unsigned int)((disp >> 1) & 0x1U),
               (unsigned int)((disp >> 2) & 0x1U),
               (unsigned int)((disp >> 3) & 0x1U),
               (unsigned int)((disp >> 4) & 0x1U),
               (unsigned int)((disp >> 5) & 0x1U),
               (unsigned int)vcount);
    xil_printf("[HANG] %s i=%u bus mem=0x%08x state=0x%02x(%s) dma=0x%08x keys=0x%03x frame=%u miss=%u vsync=%u fbseq=%u fbbuf=%u park=%u vdma_sr=0x%08x vreq=%u vwe=%u vblk=%u vnz=%u\r\n",
               tag,
               (unsigned int)idx,
               (unsigned int)dbg_mem,
               (unsigned int)mem_state,
               PsAppDiag_MemStateName(mem_state),
               (unsigned int)dbg_dma,
               (unsigned int)(status0 & 0x3FFU),
               (unsigned int)(status1 & 0x3U),
               (unsigned int)((status1 >> 2) & 0x3FFFU),
               (unsigned int)((status1 >> 16) & 0xFFFFU),
               (unsigned int)fbcap_seq,
               (unsigned int)(fbcap_buf & 0x1U),
               (unsigned int)parked,
               (unsigned int)vdma_status,
               (unsigned int)vram_req_cnt,
               (unsigned int)vram_we_cnt,
               (unsigned int)vram_blk_cnt,
               (unsigned int)vram_nz_seen);
}

static void PsAppDiag_ReadDdrChain(PsAppDiagContext *ctx,
                                   u32 *flags,
                                   u32 *counts0,
                                   u32 *counts1,
                                   u32 *ch1_last_addr,
                                   u32 *ch1_last_meta,
                                   u32 *ddr_last_addr,
                                   u32 *ddr_last_meta,
                                   u32 *ar_last_addr,
                                   u32 *ar_last_meta,
                                   u32 *r_last_addr,
                                   u32 *r_last_meta,
                                   u32 *done_last_addr,
                                   u32 *done_last_meta) {
    *flags = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CHAIN_FLAGS);
    *counts0 = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CHAIN_COUNTS0);
    *counts1 = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CHAIN_COUNTS1);
    *ch1_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CH1_LAST_ADDR);
    *ch1_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CH1_LAST_META);
    *ddr_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DDR_LAST_ADDR);
    *ddr_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DDR_LAST_META);
    *ar_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_AR_LAST_ADDR);
    *ar_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_AR_LAST_META);
    *r_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_R_LAST_ADDR);
    *r_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_R_LAST_META);
    *done_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DONE_LAST_ADDR);
    *done_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DONE_LAST_META);
}

static void PsAppDiag_PrintCtrlDecode(u32 ctrl) {
    xil_printf("[PROBE] ctrl core=%u lock=%u turbo=%u sram=%u remap=%u flash1m=%u gpio=%u tilt=%u rom_loading=%u romsafe=%u unsafe=%u\r\n",
               (unsigned int)((ctrl >> 0) & 0x1U),
               (unsigned int)((ctrl >> 1) & 0x1U),
               (unsigned int)((ctrl >> 2) & 0x1U),
               (unsigned int)((ctrl >> 4) & 0x1U),
               (unsigned int)((ctrl >> 5) & 0x1U),
               (unsigned int)((ctrl >> 9) & 0x1U),
               (unsigned int)((ctrl >> 10) & 0x1U),
               (unsigned int)((ctrl >> 11) & 0x1U),
               (unsigned int)((ctrl >> 8) & 0x1U),
               (unsigned int)((ctrl >> 14) & 0x1U),
               (unsigned int)((ctrl >> 13) & 0x1U));
}

static void PsAppDiag_PrintFramebufferProbe(PsAppDiagContext *ctx) {
    u32 idx;
    u32 parked;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return;
    }

    parked = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    xil_printf("[PROBE] fb parked=%u frame_bytes=%u stride=%u\r\n",
               (unsigned int)parked,
               (unsigned int)ctx->vdma->frame_size_bytes,
               (unsigned int)ctx->vdma->line_stride_bytes);

    for (idx = 0U; idx < ctx->vdma->frame_count; ++idx) {
        UINTPTR base_addr;
        u32 hash;
        u32 word0;
        u32 word_mid;

        base_addr = ctx->vdma->frame_addrs[idx];
        hash = PsAppVideo_ComputeSparseHash(base_addr,
                                            ctx->vdma->frame_size_bytes,
                                            PS_APP_FRAME_HASH_SAMPLES);
        word0 = Xil_In32(base_addr + 0x000U);
        word_mid = Xil_In32(base_addr + ((ctx->vdma->frame_size_bytes / 2U) & ~0x3U));

        xil_printf("[PROBE] fb%u addr=0x%08x hash=0x%08x w0=0x%08x wmid=0x%08x\r\n",
                   (unsigned int)idx,
                   (unsigned int)base_addr,
                   (unsigned int)hash,
                   (unsigned int)word0,
                   (unsigned int)word_mid);
    }
}

void PsAppDiag_PrintVdmaSnapshot(PsAppDiagContext *ctx, const char *tag) {
    UINTPTR chan_base;
    UINTPTR cfg_base;
    u32 cr;
    u32 sr;
    u32 vsize;
    u32 hsize;
    u32 strd_frmdly;
    u32 parkptr;
    u32 fb0;
    u32 fb1;
    u32 fb2;
    u32 err_bits;
    const char *label;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return;
    }

    chan_base = ctx->vdma->vdma.BaseAddr + XAXIVDMA_TX_OFFSET;
    cfg_base = ctx->vdma->vdma.BaseAddr + XAXIVDMA_MM2S_ADDR_OFFSET;

    cr = XAxiVdma_ReadReg(chan_base, XAXIVDMA_CR_OFFSET);
    sr = XAxiVdma_ReadReg(chan_base, XAXIVDMA_SR_OFFSET);
    vsize = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_VSIZE_OFFSET);
    hsize = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_HSIZE_OFFSET);
    strd_frmdly = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_STRD_FRMDLY_OFFSET);
    parkptr = XAxiVdma_ReadReg(ctx->vdma->vdma.BaseAddr, XAXIVDMA_PARKPTR_OFFSET);
    fb0 = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_START_ADDR_OFFSET + (0U * 4U));
    fb1 = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_START_ADDR_OFFSET + (1U * 4U));
    fb2 = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_START_ADDR_OFFSET + (2U * 4U));
    err_bits = sr & XAXIVDMA_SR_ERR_ALL_MASK;
    label = (tag != NULL) ? tag : "snapshot";

    xil_printf("[VDMA] %s cr=0x%08x sr=0x%08x err=0x%03x v=%u h=%u strd=0x%08x park=0x%08x\r\n",
               label,
               (unsigned int)cr,
               (unsigned int)sr,
               (unsigned int)err_bits,
               (unsigned int)vsize,
               (unsigned int)hsize,
               (unsigned int)strd_frmdly,
               (unsigned int)parkptr);
    xil_printf("[VDMA] fb0=0x%08x fb1=0x%08x fb2=0x%08x\r\n",
               (unsigned int)fb0,
               (unsigned int)fb1,
               (unsigned int)fb2);
}

void PsAppDiag_PrintConfigReadback(PsAppDiagContext *ctx, const char *tag) {
    u32 ctrl;
    u32 keys;
    u32 max_pak;
    u32 cycle;
    u32 rtc;
    u32 rtc_saved;
    u32 sw_reset;
    u32 rom_status;
    u32 irq_en;
    u32 display_frame;
    const char *label;

    if (ctx == NULL) {
        return;
    }

    label = (tag != NULL) ? tag : "cfg";
    ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
    keys = PsGbaRegs_Read(ctx->regs, GBA_REG_KEYS);
    max_pak = PsGbaRegs_Read(ctx->regs, GBA_REG_MAX_PAK_ADDR);
    cycle = PsGbaRegs_Read(ctx->regs, GBA_REG_CYCLE_PRECALC);
    rtc = PsGbaRegs_Read(ctx->regs, GBA_REG_RTC_TIMESTAMP);
    rtc_saved = PsGbaRegs_Read(ctx->regs, GBA_REG_RTC_TIMESTAMP_SAVED);
    sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET);
    rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS);
    irq_en = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_EN);
    display_frame = PsGbaRegs_Read(ctx->regs, GBA_REG_DISPLAY_FRAME);

    xil_printf("[CFG] %s ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x rtc_saved=0x%08x reset=%u rombusy=%u irq_en=0x%x disp=%u\r\n",
               label,
               (unsigned int)ctrl,
               (unsigned int)(keys & 0x3FFU),
               (unsigned int)max_pak,
               (unsigned int)(cycle & 0xFFFFU),
               (unsigned int)rtc,
               (unsigned int)rtc_saved,
               (unsigned int)(sw_reset & 0x1U),
               (unsigned int)(rom_status & 0x1U),
               (unsigned int)(irq_en & 0x3U),
               (unsigned int)(display_frame & 0x3U));
}

void PsAppDiag_PrintChainSnapshot(PsAppDiagContext *ctx, const char *tag) {
    const char *label;
    u32 flags;
    u32 counts0;
    u32 counts1;
    u32 ch1_first_addr;
    u32 ch1_first_meta;
    u32 ch1_last_addr;
    u32 ch1_last_meta;
    u32 ddr_first_addr;
    u32 ddr_first_meta;
    u32 ddr_last_addr;
    u32 ddr_last_meta;
    u32 ar_first_addr;
    u32 ar_first_meta;
    u32 ar_last_addr;
    u32 ar_last_meta;
    u32 r_first_addr;
    u32 r_first_meta;
    u32 r_last_addr;
    u32 r_last_meta;
    u32 done_first_addr;
    u32 done_first_meta;
    u32 done_last_addr;
    u32 done_last_meta;

    if (ctx == NULL) {
        return;
    }

    label = (tag != NULL) ? tag : "chain";
    flags = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CHAIN_FLAGS);
    counts0 = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CHAIN_COUNTS0);
    counts1 = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CHAIN_COUNTS1);
    ch1_first_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CH1_FIRST_ADDR);
    ch1_first_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CH1_FIRST_META);
    ch1_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CH1_LAST_ADDR);
    ch1_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CH1_LAST_META);
    ddr_first_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DDR_FIRST_ADDR);
    ddr_first_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DDR_FIRST_META);
    ddr_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DDR_LAST_ADDR);
    ddr_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DDR_LAST_META);
    ar_first_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_AR_FIRST_ADDR);
    ar_first_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_AR_FIRST_META);
    ar_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_AR_LAST_ADDR);
    ar_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_AR_LAST_META);
    r_first_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_R_FIRST_ADDR);
    r_first_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_R_FIRST_META);
    r_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_R_LAST_ADDR);
    r_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_AXI_R_LAST_META);
    done_first_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DONE_FIRST_ADDR);
    done_first_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DONE_FIRST_META);
    done_last_addr = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DONE_LAST_ADDR);
    done_last_meta = PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_DONE_LAST_META);

    xil_printf("[CHAIN] %s flags ch1=%u ddr=%u ar=%u r=%u dout=%u done=%u err=%u rerr=%u\r\n",
               label,
               (unsigned int)((flags >> 0) & 0x1U),
               (unsigned int)((flags >> 1) & 0x1U),
               (unsigned int)((flags >> 2) & 0x1U),
               (unsigned int)((flags >> 3) & 0x1U),
               (unsigned int)((flags >> 4) & 0x1U),
               (unsigned int)((flags >> 5) & 0x1U),
               (unsigned int)((flags >> 6) & 0x1U),
               (unsigned int)((flags >> 7) & 0x1U));
    xil_printf("[CHAIN] %s INTERNAL gbaon=%u rst=%u ncv=%u step=%u slp_ss=%u slp_ch=%u slp_rw=%u loading=%u settle=%u pix=%u cpudone=%u dma=%u GBAon_raw=%u lock=%u\r\n",
               label,
               (unsigned int)((flags >> 8) & 0x1U),
               (unsigned int)((flags >> 9) & 0x1U),
               (unsigned int)((flags >> 10) & 0x1U),
               (unsigned int)((flags >> 11) & 0x1U),
               (unsigned int)((flags >> 12) & 0x1U),
               (unsigned int)((flags >> 13) & 0x1U),
               (unsigned int)((flags >> 14) & 0x1U),
               (unsigned int)((flags >> 15) & 0x1U),
               (unsigned int)((flags >> 16) & 0x1U),
               (unsigned int)((flags >> 17) & 0x1U),
               (unsigned int)((flags >> 18) & 0x1U),
               (unsigned int)((flags >> 19) & 0x1U),
               (unsigned int)((flags >> 20) & 0x1U),
               (unsigned int)((flags >> 21) & 0x1U));
    xil_printf("[CHAIN] %s counts ch1_req=%u ddr_rd=%u axi_ar=%u axi_r=%u ddr_dout=%u ch1_done=%u sys_err=%u ddr_we=%u pix_we=%u\r\n",
               label,
               (unsigned int)(counts0 & 0xFFU),
               (unsigned int)((counts0 >> 8) & 0xFFU),
               (unsigned int)((counts0 >> 16) & 0xFFU),
               (unsigned int)((counts0 >> 24) & 0xFFU),
               (unsigned int)(counts1 & 0xFFU),
               (unsigned int)((counts1 >> 8) & 0xFFU),
               (unsigned int)((counts1 >> 16) & 0xFFU),
               (unsigned int)((counts1 >> 24) & 0xFU),
               (unsigned int)((counts1 >> 28) & 0xFU));
    xil_printf("[CHAIN] %s ch1 %s first=0x%08x lane=%u busy=%u disp=%u meta=0x%08x last=0x%08x lane=%u busy=%u disp=%u meta=0x%08x\r\n",
               label,
               ((flags & 0x1U) != 0U) ? "ok" : "missing",
               (unsigned int)ch1_first_addr,
               (unsigned int)((ch1_first_meta >> 8) & 0x3U),
               (unsigned int)((ch1_first_meta >> 10) & 0x1U),
               (unsigned int)((ch1_first_meta >> 11) & 0x3U),
               (unsigned int)ch1_first_meta,
               (unsigned int)ch1_last_addr,
               (unsigned int)((ch1_last_meta >> 8) & 0x3U),
               (unsigned int)((ch1_last_meta >> 10) & 0x1U),
               (unsigned int)((ch1_last_meta >> 11) & 0x3U),
               (unsigned int)ch1_last_meta);
    xil_printf("[CHAIN] %s ddr %s first=0x%08x burst=%u rd=%u we=%u busy=%u meta=0x%08x last=0x%08x burst=%u rd=%u we=%u busy=%u meta=0x%08x\r\n",
               label,
               ((flags & 0x2U) != 0U) ? "ok" : "missing",
               (unsigned int)ddr_first_addr,
               (unsigned int)((ddr_first_meta >> 8) & 0xFFU),
               (unsigned int)((ddr_first_meta >> 16) & 0x1U),
               (unsigned int)((ddr_first_meta >> 17) & 0x1U),
               (unsigned int)((ddr_first_meta >> 18) & 0x1U),
               (unsigned int)ddr_first_meta,
               (unsigned int)ddr_last_addr,
               (unsigned int)((ddr_last_meta >> 8) & 0xFFU),
               (unsigned int)((ddr_last_meta >> 16) & 0x1U),
               (unsigned int)((ddr_last_meta >> 17) & 0x1U),
               (unsigned int)((ddr_last_meta >> 18) & 0x1U),
               (unsigned int)ddr_last_meta);
    xil_printf("[CHAIN] %s axi_ar %s first=0x%08x len=%u size=%u burst=%u meta=0x%08x last=0x%08x len=%u size=%u burst=%u meta=0x%08x\r\n",
               label,
               ((flags & 0x4U) != 0U) ? "ok" : "missing",
               (unsigned int)ar_first_addr,
               (unsigned int)((ar_first_meta >> 8) & 0xFFU),
               (unsigned int)((ar_first_meta >> 16) & 0x7U),
               (unsigned int)((ar_first_meta >> 19) & 0x3U),
               (unsigned int)ar_first_meta,
               (unsigned int)ar_last_addr,
               (unsigned int)((ar_last_meta >> 8) & 0xFFU),
               (unsigned int)((ar_last_meta >> 16) & 0x7U),
               (unsigned int)((ar_last_meta >> 19) & 0x3U),
               (unsigned int)ar_last_meta);
    xil_printf("[CHAIN] %s axi_r %s first=0x%08x resp=%u last=%u meta=0x%08x last=0x%08x resp=%u last=%u meta=0x%08x\r\n",
               label,
               ((flags & 0x8U) != 0U) ? "ok" : "missing",
               (unsigned int)r_first_addr,
               (unsigned int)((r_first_meta >> 8) & 0x3U),
               (unsigned int)((r_first_meta >> 10) & 0x1U),
               (unsigned int)r_first_meta,
               (unsigned int)r_last_addr,
               (unsigned int)((r_last_meta >> 8) & 0x3U),
               (unsigned int)((r_last_meta >> 10) & 0x1U),
               (unsigned int)r_last_meta);
    xil_printf("[CHAIN] %s done %s first=0x%08x lane=%u disp=%u rresp=%u rlast=%u meta=0x%08x last=0x%08x lane=%u disp=%u rresp=%u rlast=%u meta=0x%08x\r\n",
               label,
               ((flags & 0x20U) != 0U) ? "ok" : "missing",
               (unsigned int)done_first_addr,
               (unsigned int)((done_first_meta >> 8) & 0x3U),
               (unsigned int)((done_first_meta >> 10) & 0x3U),
               (unsigned int)((done_first_meta >> 12) & 0x3U),
               (unsigned int)((done_first_meta >> 14) & 0x1U),
               (unsigned int)done_first_meta,
               (unsigned int)done_last_addr,
               (unsigned int)((done_last_meta >> 8) & 0x3U),
               (unsigned int)((done_last_meta >> 10) & 0x3U),
               (unsigned int)((done_last_meta >> 12) & 0x3U),
               (unsigned int)((done_last_meta >> 14) & 0x1U),
               (unsigned int)done_last_meta);
}

void PsAppDiag_PrintRomHeader(PsAppDiagContext *ctx) {
    const u8 *rom;
    char title[13];
    char game_code[5];
    char maker_code[3];
    u32 entry_word;
    u32 logo_hash;
    u32 idx;
    u8 checksum_calc;

    if ((ctx == NULL) || (ctx->rom->loaded == 0U)) {
        xil_printf("[ROMHDR] no rom loaded\r\n");
        return;
    }

    Xil_DCacheInvalidateRange((INTPTR)PS_APP_GBA_ROM_REGION_BASE_ADDR, 0xC0U);
    rom = (const u8 *)PS_APP_GBA_ROM_REGION_BASE_ADDR;

    PsAppDiag_SanitizeAsciiField(title, sizeof(title), &rom[0xA0U], 12U);
    PsAppDiag_SanitizeAsciiField(game_code, sizeof(game_code), &rom[0xACU], 4U);
    PsAppDiag_SanitizeAsciiField(maker_code, sizeof(maker_code), &rom[0xB0U], 2U);

    entry_word = ((u32)rom[0]) |
                 ((u32)rom[1] << 8) |
                 ((u32)rom[2] << 16) |
                 ((u32)rom[3] << 24);

    logo_hash = 2166136261U;
    for (idx = 0x04U; idx < 0xA0U; ++idx) {
        logo_hash ^= rom[idx];
        logo_hash *= 16777619U;
    }

    checksum_calc = PsAppDiag_ComputeGbaHeaderChecksum(rom);

    xil_printf("[ROMHDR] entry=0x%08x title='%s' code='%s' maker='%s'\r\n",
               (unsigned int)entry_word,
               title,
               game_code,
               maker_code);
    xil_printf("[ROMHDR] fixed=0x%02x unit=0x%02x dev=0x%02x ver=0x%02x chk=0x%02x calc=0x%02x logo_hash=0x%08x\r\n",
               (unsigned int)rom[0xB2U],
               (unsigned int)rom[0xB3U],
               (unsigned int)rom[0xB4U],
               (unsigned int)rom[0xBCU],
               (unsigned int)rom[0xBDU],
               (unsigned int)checksum_calc,
               (unsigned int)logo_hash);
    xil_printf("[ROMHDR] w00=0x%08x w04=0x%08x wa0=0x%08x wa4=0x%08x\r\n",
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + 0x000U),
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + 0x004U),
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + 0x0A0U),
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + 0x0A4U));
}

void PsAppDiag_PrintRomProbe(PsAppDiagContext *ctx) {
    char title[13];
    char game_code[5];
    char maker_code[3];
    u32 size_bytes;
    u32 size_aligned;
    u32 pad_bytes;
    u32 maxpak_dword;
    u32 maxpak_bytes;
    u32 q1_off;
    u32 q2_off;
    u32 q3_off;
    u32 tail16_off;
    u32 tail4_off;
    u32 pad_off;
    u32 ctrl;
    const char *save_guess;

    if ((ctx == NULL) || (ctx->rom == NULL) || (ctx->rom->loaded == 0U)) {
        xil_printf("[ROMPROBE] no rom loaded\r\n");
        return;
    }

    size_bytes = ctx->rom->size_bytes;
    size_aligned = ctx->rom->size_aligned;
    pad_bytes = (size_aligned >= size_bytes) ? (size_aligned - size_bytes) : 0U;
    maxpak_dword = ctx->config->max_pak_addr & 0x1FFFFFFU;
    maxpak_bytes = maxpak_dword << 2;
    ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);

    Xil_DCacheInvalidateRange((INTPTR)PS_APP_GBA_ROM_REGION_BASE_ADDR, 0xC0U);
    PsAppDiag_SanitizeAsciiField(title, sizeof(title), (const u8 *)(PS_APP_GBA_ROM_REGION_BASE_ADDR + 0xA0U), 12U);
    PsAppDiag_SanitizeAsciiField(game_code, sizeof(game_code), (const u8 *)(PS_APP_GBA_ROM_REGION_BASE_ADDR + 0xACU), 4U);
    PsAppDiag_SanitizeAsciiField(maker_code, sizeof(maker_code), (const u8 *)(PS_APP_GBA_ROM_REGION_BASE_ADDR + 0xB0U), 2U);

    if (ctx->rom->sig_flash1m != 0U) save_guess = "FLASH1M";
    else if (ctx->rom->sig_flash != 0U) save_guess = "FLASH";
    else if (ctx->rom->sig_sram != 0U) save_guess = "SRAM";
    else if (ctx->rom->sig_eeprom != 0U) save_guess = "EEPROM";
    else save_guess = "UNKNOWN";

    q1_off = PsAppDiag_ClampWordOffset(size_bytes / 4U, size_bytes);
    q2_off = PsAppDiag_ClampWordOffset(size_bytes / 2U, size_bytes);
    q3_off = PsAppDiag_ClampWordOffset((size_bytes * 3U) / 4U, size_bytes);
    tail16_off = PsAppDiag_ClampWordOffset((size_aligned >= 16U) ? (size_aligned - 16U) : 0U,
                                           size_aligned);
    tail4_off = PsAppDiag_ClampWordOffset((size_aligned >= 4U) ? (size_aligned - 4U) : 0U,
                                          size_aligned);
    pad_off = PsAppDiag_ClampWordOffset(size_bytes, size_aligned);

    PsAppDiag_PrintRomHeader(ctx);
    xil_printf("[ROMPROBE] path=%s\r\n",
               ctx->rom->path[0] != '\0' ? ctx->rom->path : "(none)");
    xil_printf("[ROMPROBE] title='%s' code='%s' maker='%s' bytes=%u aligned=%u pad=%u maxpak_dw=0x%08x maxpak_bytes=0x%08x remap_ctrl=%u\r\n",
               title,
               game_code,
               maker_code,
               (unsigned int)size_bytes,
               (unsigned int)size_aligned,
               (unsigned int)pad_bytes,
               (unsigned int)maxpak_dword,
               (unsigned int)maxpak_bytes,
               (unsigned int)((ctrl >> 5) & 0x1U));
    xil_printf("[ROMPROBE] sig_hits flash1m=%u flash=%u sram=%u eeprom=%u save_guess=%s\r\n",
               (unsigned int)ctx->rom->sig_flash1m,
               (unsigned int)ctx->rom->sig_flash,
               (unsigned int)ctx->rom->sig_sram,
               (unsigned int)ctx->rom->sig_eeprom,
               save_guess);
    xil_printf("[ROMPROBE] bios mode=%s ext=%u load_ok=%u bytes=%u\r\n",
               PsAppDiag_BiosModeName(ctx->rom->bios_mode),
               (unsigned int)ctx->rom->bios_external_present,
               (unsigned int)ctx->rom->bios_load_ok,
               (unsigned int)ctx->rom->bios_bytes_loaded);
    if (ctx->rom->flash1m_offset != 0xFFFFFFFFU) {
        xil_printf("[ROMPROBE] sig FLASH1M_V @0x%08x\r\n", (unsigned int)ctx->rom->flash1m_offset);
    }
    if (ctx->rom->flash_offset != 0xFFFFFFFFU) {
        xil_printf("[ROMPROBE] sig FLASH_V   @0x%08x\r\n", (unsigned int)ctx->rom->flash_offset);
    }
    if (ctx->rom->sram_offset != 0xFFFFFFFFU) {
        xil_printf("[ROMPROBE] sig SRAM_V    @0x%08x\r\n", (unsigned int)ctx->rom->sram_offset);
    }
    if (ctx->rom->eeprom_offset != 0xFFFFFFFFU) {
        xil_printf("[ROMPROBE] sig EEPROM_V  @0x%08x\r\n", (unsigned int)ctx->rom->eeprom_offset);
    }
    xil_printf("[ROMPROBE] quirk_guess remap=%u sram=%u gpio=%u tilt=%u solar=%u flash1m=%u\r\n",
               (unsigned int)ctx->rom->quirk_remap,
               (unsigned int)ctx->rom->quirk_sram_disable,
               (unsigned int)ctx->rom->quirk_gpio,
               (unsigned int)ctx->rom->quirk_tilt,
               (unsigned int)ctx->rom->quirk_solar,
               (unsigned int)ctx->rom->sig_flash1m);
    xil_printf("[ROMPROBE] sample q1@0x%08x=0x%08x q2@0x%08x=0x%08x q3@0x%08x=0x%08x\r\n",
               (unsigned int)q1_off,
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + q1_off),
               (unsigned int)q2_off,
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + q2_off),
               (unsigned int)q3_off,
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + q3_off));
    xil_printf("[ROMPROBE] tail t16@0x%08x=0x%08x t4@0x%08x=0x%08x pad@0x%08x=0x%08x\r\n",
               (unsigned int)tail16_off,
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + tail16_off),
               (unsigned int)tail4_off,
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + tail4_off),
               (unsigned int)pad_off,
               (unsigned int)Xil_In32(PS_APP_GBA_ROM_REGION_BASE_ADDR + pad_off));
}

void PsAppDiag_PrintProbe(PsAppDiagContext *ctx) {
    u32 ctrl;
    u32 keys;
    u32 max_pak;
    u32 cycle;
    u32 rtc;
    u32 rtc_saved;
    u32 status0;
    u32 status1;
    u32 sw_reset;
    u32 rom_status;
    u32 err_latch;
    u32 irq_en;
    u32 irq_sts;
    u32 dbg_pc;
    u32 dbg_mix;
    u32 dbg_irq;
    u32 dbg_dma;
    u32 dbg_mem;
    u32 mem_sdram_timeout;
    u32 mem_eeprom_cmd;
    u32 mem_sram_enable;
    u32 gpu_vcount;
    u32 gpu_disp_low;

    if (ctx == NULL) {
        return;
    }

    ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
    keys = PsGbaRegs_Read(ctx->regs, GBA_REG_KEYS);
    max_pak = PsGbaRegs_Read(ctx->regs, GBA_REG_MAX_PAK_ADDR);
    cycle = PsGbaRegs_Read(ctx->regs, GBA_REG_CYCLE_PRECALC);
    rtc = PsGbaRegs_Read(ctx->regs, GBA_REG_RTC_TIMESTAMP);
    rtc_saved = PsGbaRegs_Read(ctx->regs, GBA_REG_RTC_TIMESTAMP_SAVED);
    status0 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0);
    status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
    sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET);
    rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    irq_en = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_EN);
    irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS);
    dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_mix = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_MIX);
    dbg_irq = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_IRQ);
    dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    mem_sdram_timeout = (dbg_mem >> 28) & 0x1U;
    mem_eeprom_cmd = (dbg_mem >> 29) & 0x1U;
    mem_sram_enable = (dbg_mem >> 30) & 0x1U;
    gpu_vcount = (dbg_irq >> 17) & 0xFFU;
    gpu_disp_low = (dbg_irq >> 25) & 0x7FU;

    xil_printf("[PROBE] shadow ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x rtc_saved=0x%08x irq_en=0x%x rom_loaded=%u rom_busy=%u path=%s\r\n",
               (unsigned int)ctx->config->ctrl,
               (unsigned int)(ctx->config->keys & 0x3FFU),
               (unsigned int)ctx->config->max_pak_addr,
               (unsigned int)ctx->config->cycle_precalc,
               (unsigned int)ctx->config->rtc_timestamp,
               (unsigned int)ctx->config->rtc_timestamp_saved,
               (unsigned int)(ctx->config->irq_enable & 0x3U),
               (unsigned int)ctx->rom->loaded,
               (unsigned int)ctx->rom->is_loading,
               ctx->rom->path[0] != '\0' ? ctx->rom->path : "(none)");
    xil_printf("[PROBE] raw ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x rtc_saved=0x%08x reset=%u rom=0x%08x irq_en=0x%x irq_sts=0x%x\r\n",
               (unsigned int)ctrl,
               (unsigned int)(keys & 0x3FFU),
               (unsigned int)max_pak,
               (unsigned int)(cycle & 0xFFFFU),
               (unsigned int)rtc,
               (unsigned int)rtc_saved,
               (unsigned int)(sw_reset & 0x1U),
               (unsigned int)rom_status,
               (unsigned int)(irq_en & 0x3U),
               (unsigned int)(irq_sts & 0x3U));
    xil_printf("[PROBE] stat status0=0x%08x status1=0x%08x frame=%u miss=%u vsync=%u err=0x%08x\r\n",
               (unsigned int)status0,
               (unsigned int)status1,
               (unsigned int)(status1 & 0x3U),
               (unsigned int)((status1 >> 2) & 0x3FFFU),
               (unsigned int)((status1 >> 16) & 0xFFFFU),
               (unsigned int)err_latch);
    xil_printf("[PROBE] dbg pc=0x%08x mix=0x%08x irq=0x%08x dma=0x%08x mem=0x%08x sdram_timeout=%u eeprom_cmd=%u sram=%u\r\n",
               (unsigned int)dbg_pc,
               (unsigned int)dbg_mix,
               (unsigned int)dbg_irq,
               (unsigned int)dbg_dma,
               (unsigned int)dbg_mem,
               (unsigned int)mem_sdram_timeout,
               (unsigned int)mem_eeprom_cmd,
               (unsigned int)mem_sram_enable);
    xil_printf("[PROBE] gpu vcount=%u disp_lo=0x%02x\r\n",
               (unsigned int)gpu_vcount,
               (unsigned int)gpu_disp_low);

    PsAppDiag_PrintCtrlDecode(ctrl);
    PsAppDiag_PrintRomProbe(ctx);
    PsAppDiag_PrintVdmaSnapshot(ctx, "probe");
    PsAppDiag_PrintFramebufferProbe(ctx);
}

void PsAppDiag_PrintTrace(PsAppDiagContext *ctx, u32 samples, u32 interval_ms) {
    u32 idx;

    if (ctx == NULL) {
        return;
    }

    if (samples == 0U) {
        samples = PS_APP_TRACE_DEFAULT_SAMPLES;
    } else if (samples > PS_APP_TRACE_MAX_SAMPLES) {
        samples = PS_APP_TRACE_MAX_SAMPLES;
    }

    if (interval_ms == 0U) {
        interval_ms = PS_APP_TRACE_DEFAULT_INTERVAL_MS;
    }

    xil_printf("[TRACE] begin samples=%u interval_ms=%u\r\n",
               (unsigned int)samples,
               (unsigned int)interval_ms);

    for (idx = 0U; idx < samples; ++idx) {
        u32 status1;
        u32 irq_sts;
        u32 sw_reset;
        u32 rom_status;
        u32 err_latch;
        u32 parked;
        u32 vdma_status;
        u32 vdma_errs;
        u32 dbg_pc;
        u32 dbg_mem;
        u32 dbg_dma;

        status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
        irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS) & 0x3U;
        sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET) & 0x1U;
        rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS) & 0x1U;
        err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
        parked = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
        vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);
        vdma_errs = vdma_status & XAXIVDMA_SR_ERR_ALL_MASK;
        dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
        dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
        dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);

        xil_printf("[TRACE] i=%u frame=%u miss=%u vsync=%u reset=%u rombusy=%u irq=0x%x err=0x%08x park=%u vdma_sr=0x%08x vdma_irq=%u vdma_err=%u dma_err=0x%03x pc=0x%08x mem=0x%08x dma=0x%08x\r\n",
                   (unsigned int)idx,
                   (unsigned int)(status1 & 0x3U),
                   (unsigned int)((status1 >> 2) & 0x3FFFU),
                   (unsigned int)((status1 >> 16) & 0xFFFFU),
                   (unsigned int)sw_reset,
                   (unsigned int)rom_status,
                   (unsigned int)irq_sts,
                   (unsigned int)err_latch,
                   (unsigned int)parked,
                   (unsigned int)vdma_status,
                   (unsigned int)ctx->video->state->vdma_irq_count,
                   (unsigned int)ctx->video->state->vdma_err_count,
                   (unsigned int)vdma_errs,
                   (unsigned int)dbg_pc,
                   (unsigned int)dbg_mem,
                   (unsigned int)dbg_dma);

        if ((idx + 1U) < samples) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    xil_printf("[TRACE] end\r\n");
}

void PsAppDiag_PrintHangSnapshot(PsAppDiagContext *ctx, u32 samples, u32 interval_ms) {
    u32 idx;
    u32 first_pc = 0U;
    u32 last_pc = 0U;
    u32 prev_pc = 0U;
    u32 first_fbseq = 0U;
    u32 last_fbseq = 0U;
    u32 prev_fbseq = 0U;
    u32 pc_change_cnt = 0U;
    u32 fbseq_change_cnt = 0U;
    u32 bios_cnt = 0U;
    u32 rom_cnt = 0U;
    u32 invalid_pc_cnt = 0U;
    u32 halt_cnt = 0U;
    u32 wait_sdram_cnt = 0U;
    u32 no_enabled_irq_cnt = 0U;
    u32 masked_irq_cnt = 0U;
    u32 can_irq_cnt = 0U;
    u32 vblank_flag_cnt = 0U;
    u32 vblank_ie_cnt = 0U;
    u32 has_prev = 0U;
    u32 rom_pcs[32];
    u32 rom_pc_count = 0U;

    if (ctx == NULL) {
        return;
    }

    if (samples == 0U) {
        samples = 8U;
    } else if (samples > PS_APP_TRACE_MAX_SAMPLES) {
        samples = PS_APP_TRACE_MAX_SAMPLES;
    }
    if (interval_ms == 0U) {
        interval_ms = 20U;
    }

    xil_printf("[HANG] begin samples=%u interval_ms=%u\r\n",
               (unsigned int)samples,
               (unsigned int)interval_ms);

    for (idx = 0U; idx < samples; ++idx) {
        u32 ctrl;
        u32 key_shadow;
        u32 sw_reset;
        u32 ps_irq_en;
        u32 ps_irq_sts;
        u32 rom_status;
        u32 status0;
        u32 status1;
        u32 dbg_pc;
        u32 dbg_mix;
        u32 dbg_irq;
        u32 dbg_irq_ext;
        u32 dbg_dma;
        u32 dbg_mem;
        u32 fbcap_status;
        u32 fbcap_seq;
        u32 parked;
        u32 vdma_status;
        u32 if_ie;
        u32 ime;
        u32 irq_disable;
        u32 disp;

        ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
        key_shadow = PsGbaRegs_Read(ctx->regs, GBA_REG_KEYS);
        sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET);
        ps_irq_en = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_EN);
        ps_irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS);
        rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS);
        status0 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0);
        status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
        dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
        if (dbg_pc >= 0x08000000U && dbg_pc < 0x0A000000U && rom_pc_count < 30U) {
            rom_pcs[rom_pc_count++] = dbg_pc;
        }
        dbg_mix = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_MIX);
        dbg_irq = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_IRQ);
        dbg_irq_ext = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_IRQ_EXT);
        dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
        dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
        fbcap_status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
        fbcap_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
        parked = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
        vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);

        if (idx == 0U) {
            first_pc = dbg_pc;
            first_fbseq = fbcap_seq;
        }
        last_pc = dbg_pc;
        last_fbseq = fbcap_seq;

        if (has_prev != 0U) {
            if (dbg_pc != prev_pc) {
                pc_change_cnt++;
            }
            if (fbcap_seq != prev_fbseq) {
                fbseq_change_cnt++;
            }
        }
        prev_pc = dbg_pc;
        prev_fbseq = fbcap_seq;
        has_prev = 1U;

        if (dbg_pc < 0x00004000U) {
            bios_cnt++;
        } else if ((dbg_pc >= 0x08000000U) && (dbg_pc <= 0x0DFFFFFFU)) {
            rom_cnt++;
        } else if ((strcmp(PsAppDiag_PcRegionName(dbg_pc), "INVALID") == 0)) {
            invalid_pc_cnt++;
        }
        if ((dbg_mix & 0x1U) != 0U) {
            halt_cnt++;
        }
        if ((dbg_mem & 0xFFU) == PS_APP_MEM_STATE_WAIT_SDRAM) {
            wait_sdram_cnt++;
        }

        if_ie = (dbg_irq_ext >> 16) & 0xFFFFU;
        ime = (dbg_irq >> 16) & 0x1U;
        irq_disable = (dbg_mix >> 10) & 0x1U;
        disp = (dbg_irq >> 25) & 0x7FU;
        if (if_ie == 0U) {
            no_enabled_irq_cnt++;
        } else if ((ime == 0U) || (irq_disable != 0U)) {
            masked_irq_cnt++;
        } else {
            can_irq_cnt++;
        }
        if ((disp & 0x1U) != 0U) {
            vblank_flag_cnt++;
        }
        if ((disp & 0x8U) != 0U) {
            vblank_ie_cnt++;
        }

        PsAppDiag_PrintCpuIrqDecode("sample", idx, ctrl, key_shadow, sw_reset,
                                    ps_irq_en, ps_irq_sts, rom_status,
                                    status0, status1, dbg_pc, dbg_mix,
                                    dbg_irq, dbg_irq_ext, dbg_mem, dbg_dma,
                                    fbcap_seq, fbcap_status & 0x1U, parked, vdma_status);

        if ((idx + 1U) < samples) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    xil_printf("[HANG] summary pc_first=0x%08x pc_last=0x%08x pc_change=%u bios=%u rom=%u invalid=%u halt=%u wait_sdram=%u fbseq_first=%u fbseq_last=%u fbseq_change=%u\r\n",
               (unsigned int)first_pc,
               (unsigned int)last_pc,
               (unsigned int)pc_change_cnt,
               (unsigned int)bios_cnt,
               (unsigned int)rom_cnt,
               (unsigned int)invalid_pc_cnt,
               (unsigned int)halt_cnt,
               (unsigned int)wait_sdram_cnt,
               (unsigned int)first_fbseq,
               (unsigned int)last_fbseq,
               (unsigned int)fbseq_change_cnt);
    xil_printf("[HANG] irq_summary no_enabled_irq=%u masked_irq=%u can_irq=%u vblank_flag=%u vblank_ie=%u\r\n",
               (unsigned int)no_enabled_irq_cnt,
               (unsigned int)masked_irq_cnt,
               (unsigned int)can_irq_cnt,
               (unsigned int)vblank_flag_cnt,
               (unsigned int)vblank_ie_cnt);

    if ((bios_cnt == samples) && (pc_change_cnt == 0U)) {
        xil_printf("[HANG] verdict=CPU_STABLE_IN_BIOS hint=%s\r\n",
                   PsAppDiag_BiosPcHint(last_pc));
    } else if (halt_cnt == samples) {
        xil_printf("[HANG] verdict=CPU_HALTED irq_path=%s\r\n",
                   (can_irq_cnt != 0U) ? "wakeable" :
                   ((masked_irq_cnt != 0U) ? "masked" : "no_enabled_irq"));
    } else if (invalid_pc_cnt != 0U) {
        xil_printf("[HANG] verdict=PC_RUNAWAY_OR_TORN_CDC invalid_samples=%u\r\n",
                   (unsigned int)invalid_pc_cnt);
    } else if (fbseq_change_cnt == 0U) {
        xil_printf("[HANG] verdict=FRAME_CAPTURE_NOT_ADVANCING cpu_region=%s\r\n",
                   PsAppDiag_PcRegionName(last_pc));
    } else {
        xil_printf("[HANG] verdict=CPU_AND_CAPTURE_ADVANCING cpu_region=%s\r\n",
                   PsAppDiag_PcRegionName(last_pc));
    }

    PsAppDiag_PrintDdrLogSnapshot(ctx, "hang");
    PsAppDiag_PrintChainSnapshot(ctx, "hang");
    PsAppDiag_PrintVdmaSnapshot(ctx, "hang");
    {
        u32 latest_seq;
        u32 latest_buf;
        PsAppVideo_ReadCaptureStatus(ctx->video, &latest_seq, &latest_buf);
        (void)latest_seq;
        PsAppDiag_PrintPixcapSummary(ctx, latest_buf & 0x1U);
        PsAppDiag_PrintPixcapRow(ctx, latest_buf & 0x1U, 0U);
        PsAppDiag_PrintPixcapRow(ctx, latest_buf & 0x1U, PS_APP_GBA_FRAME_HEIGHT / 2U);
        PsAppDiag_PrintPixcapRow(ctx, latest_buf & 0x1U, PS_APP_GBA_FRAME_HEIGHT - 1U);
    }
    PsAppDiag_ScanFramebufferForAnomaly(ctx, "hang", 1U);
    if (rom_pc_count > 0U) {
        PsAppDiag_DisasmHangPcs(rom_pcs, rom_pc_count);
    }
    xil_printf("[HANG] end\r\n");
}

void PsAppDiag_PrintLaunchTrace(PsAppDiagContext *ctx, u32 samples, u32 interval_ms) {
    u32 idx;
    u32 pc_nonzero_cnt = 0U;
    u32 pc_change_cnt = 0U;
    u32 dbg_nonzero_cnt = 0U;
    u32 fbseq_change_cnt = 0U;
    u32 prev_pc = 0U;
    u32 prev_fbcap_seq = 0U;
    u8  has_prev = 0U;

    if (ctx == NULL) {
        return;
    }

    if (samples == 0U) {
        samples = PS_APP_LAUNCH_DIAG_SAMPLES;
    }
    if (interval_ms == 0U) {
        interval_ms = PS_APP_LAUNCH_DIAG_INTERVAL_MS;
    }

    xil_printf("[LAUNCHDIAG] begin samples=%u interval_ms=%u\r\n",
               (unsigned int)samples,
               (unsigned int)interval_ms);

    for (idx = 0U; idx < samples; ++idx) {
        u32 ctrl;
        u32 status0;
        u32 status1;
        u32 sw_reset;
        u32 rom_status;
        u32 err_latch;
        u32 irq_sts;
        u32 dbg_pc;
        u32 dbg_mix;
        u32 dbg_irq;
        u32 dbg_dma;
        u32 dbg_mem;
        u32 mem_state;
        u32 mem_eeprom_mode;
        u32 mem_dma_eepromcount;
        u32 fbcap_status;
        u32 fbcap_seq;
        u32 gpu_vcount;
        u32 gpu_disp_low;

        ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
        status0 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0);
        status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
        sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET) & 0x1U;
        rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS);
        err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
        irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS);
        dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
        dbg_mix = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_MIX);
        dbg_irq = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_IRQ);
        dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
        dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
        mem_state = dbg_mem & 0xFFU;
        mem_eeprom_mode = (dbg_mem >> 8) & 0x7U;
        mem_dma_eepromcount = (dbg_mem >> 11) & 0x1FFFFU;
        fbcap_status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
        fbcap_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
        gpu_vcount = (dbg_irq >> 17) & 0xFFU;
        gpu_disp_low = (dbg_irq >> 25) & 0x7FU;

        xil_printf("[LAUNCHDIAG] i=%u ctrl=0x%08x reset=%u rom=0x%08x status0=0x%08x status1=0x%08x frame=%u miss=%u vsync=%u irq_sts=0x%x err=0x%08x\r\n",
                   (unsigned int)idx,
                   (unsigned int)ctrl,
                   (unsigned int)sw_reset,
                   (unsigned int)rom_status,
                   (unsigned int)status0,
                   (unsigned int)status1,
                   (unsigned int)(status1 & 0x3U),
                   (unsigned int)((status1 >> 2) & 0x3FFFU),
                   (unsigned int)((status1 >> 16) & 0xFFFFU),
                   (unsigned int)(irq_sts & 0x3U),
                   (unsigned int)err_latch);
        xil_printf("[LAUNCHDIAG] i=%u pc=0x%08x mix=0x%08x irq=0x%08x dma=0x%08x mem=0x%08x vcount=%u disp_lo=0x%02x fbcap=0x%08x seq=%u\r\n",
                   (unsigned int)idx,
                   (unsigned int)dbg_pc,
                   (unsigned int)dbg_mix,
                   (unsigned int)dbg_irq,
                   (unsigned int)dbg_dma,
                   (unsigned int)dbg_mem,
                   (unsigned int)gpu_vcount,
                   (unsigned int)gpu_disp_low,
                   (unsigned int)fbcap_status,
                   (unsigned int)fbcap_seq);
        xil_printf("[LAUNCHDIAG] i=%u memdec state=0x%02x eepromMode=%u dma_eepromcount=%u\r\n",
                   (unsigned int)idx,
                   (unsigned int)mem_state,
                   (unsigned int)mem_eeprom_mode,
                   (unsigned int)mem_dma_eepromcount);

        if (dbg_pc != 0U) {
            ++pc_nonzero_cnt;
        }
        if ((dbg_mix | dbg_irq | dbg_dma | dbg_mem) != 0U) {
            ++dbg_nonzero_cnt;
        }
        if (has_prev != 0U) {
            if (dbg_pc != prev_pc) {
                ++pc_change_cnt;
            }
            if (fbcap_seq != prev_fbcap_seq) {
                ++fbseq_change_cnt;
            }
        }
        prev_pc = dbg_pc;
        prev_fbcap_seq = fbcap_seq;
        has_prev = 1U;

        if ((idx + 1U) < samples) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    xil_printf("[LAUNCHDIAG] summary pc_nonzero=%u pc_change=%u dbg_nonzero=%u fbseq_change=%u chain=0x%08x\r\n",
               (unsigned int)pc_nonzero_cnt,
               (unsigned int)pc_change_cnt,
               (unsigned int)dbg_nonzero_cnt,
               (unsigned int)fbseq_change_cnt,
               (unsigned int)PsGbaRegs_Read(ctx->regs, GBA_REG_DBG_CHAIN_FLAGS));

    PsAppDiag_PrintChainSnapshot(ctx, "launch");
    PsAppDiag_PrintVdmaSnapshot(ctx, "launch");
    xil_printf("[LAUNCHDIAG] end\r\n");
}

void PsAppDiag_PrintRuntimeSample(PsAppDiagContext *ctx, const char *tag) {
    u32 ctrl;
    u32 status0;
    u32 status1;
    u32 sw_reset;
    u32 rom_status;
    u32 err_latch;
    u32 irq_sts;
    u32 dbg_pc;
    u32 dbg_mix;
    u32 dbg_irq;
    u32 dbg_dma;
    u32 dbg_mem;
    u32 mem_state;
    u32 mem_vram_req;
    u32 mem_vram_we;
    u32 mem_vram_blk;
    u32 mem_sram_enable;
    u32 mem_vram_nz_seen;
    u32 fbcap_status;
    u32 fbcap_seq;
    u32 gpu_vcount;
    u32 gpu_disp_low;
    const char *print_tag = (tag != NULL) ? tag : "sample";

    if (ctx == NULL) {
        return;
    }

    ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
    status0 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0);
    status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
    sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET) & 0x1U;
    rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS);
    dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_mix = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_MIX);
    dbg_irq = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_IRQ);
    dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    mem_state = dbg_mem & 0xFFU;
    mem_vram_req = (dbg_mem >> 8) & 0xFFU;
    mem_vram_we = (dbg_mem >> 16) & 0xFFU;
    mem_vram_blk = (dbg_mem >> 24) & 0xFU;
    mem_sram_enable = (dbg_mem >> 30) & 0x1U;
    mem_vram_nz_seen = (dbg_mem >> 31) & 0x1U;
    fbcap_status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
    fbcap_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    gpu_vcount = (dbg_irq >> 17) & 0xFFU;
    gpu_disp_low = (dbg_irq >> 25) & 0x7FU;

    xil_printf("[RUNTIME] %s ctrl=0x%08x reset=%u rom=0x%08x status0=0x%08x status1=0x%08x frame=%u miss=%u vsync=%u irq_sts=0x%x err=0x%08x\r\n",
               print_tag,
               (unsigned int)ctrl,
               (unsigned int)sw_reset,
               (unsigned int)rom_status,
               (unsigned int)status0,
               (unsigned int)status1,
               (unsigned int)(status1 & 0x3U),
               (unsigned int)((status1 >> 2) & 0x3FFFU),
               (unsigned int)((status1 >> 16) & 0xFFFFU),
               (unsigned int)(irq_sts & 0x3U),
               (unsigned int)err_latch);
    xil_printf("[RUNTIME] %s pc=0x%08x mix=0x%08x irq=0x%08x dma=0x%08x mem=0x%08x vcount=%u disp_lo=0x%02x fbcap=0x%08x seq=%u\r\n",
               print_tag,
               (unsigned int)dbg_pc,
               (unsigned int)dbg_mix,
               (unsigned int)dbg_irq,
               (unsigned int)dbg_dma,
               (unsigned int)dbg_mem,
               (unsigned int)gpu_vcount,
               (unsigned int)gpu_disp_low,
               (unsigned int)fbcap_status,
               (unsigned int)fbcap_seq);
    xil_printf("[RUNTIME] %s memdec state=0x%02x vreq=%u vwe=%u vblk=%u vnz=%u sram=%u\r\n",
               print_tag,
               (unsigned int)mem_state,
               (unsigned int)mem_vram_req,
               (unsigned int)mem_vram_we,
               (unsigned int)mem_vram_blk,
               (unsigned int)mem_vram_nz_seen,
               (unsigned int)mem_sram_enable);
}

void PsAppDiag_PrintDdrLogSnapshot(PsAppDiagContext *ctx, const char *tag) {
    const char *label;
    u32 ctrl;
    u32 status1;
    u32 err_latch;
    u32 irq_sts;
    u32 dbg_pc;
    u32 dbg_mix;
    u32 dbg_dma;
    u32 dbg_mem;
    u32 mem_state;
    u32 mem_vram_req;
    u32 mem_vram_we;
    u32 mem_vram_blk;
    u32 mem_sram_enable;
    u32 mem_vram_nz_seen;
    u32 flags;
    u32 counts0;
    u32 counts1;
    u32 ch1_last_addr;
    u32 ch1_last_meta;
    u32 ddr_last_addr;
    u32 ddr_last_meta;
    u32 ar_last_addr;
    u32 ar_last_meta;
    u32 r_last_addr;
    u32 r_last_meta;
    u32 done_last_addr;
    u32 done_last_meta;
    const char *blocked_at;

    if (ctx == NULL) {
        return;
    }

    label = (tag != NULL) ? tag : "ddr";
    ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
    status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS);
    dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_mix = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_MIX);
    dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    mem_state = dbg_mem & 0xFFU;
    mem_vram_req = (dbg_mem >> 8) & 0xFFU;
    mem_vram_we = (dbg_mem >> 16) & 0xFFU;
    mem_vram_blk = (dbg_mem >> 24) & 0xFU;
    mem_sram_enable = (dbg_mem >> 30) & 0x1U;
    mem_vram_nz_seen = (dbg_mem >> 31) & 0x1U;

    PsAppDiag_ReadDdrChain(ctx,
                           &flags,
                           &counts0,
                           &counts1,
                           &ch1_last_addr,
                           &ch1_last_meta,
                           &ddr_last_addr,
                           &ddr_last_meta,
                           &ar_last_addr,
                           &ar_last_meta,
                           &r_last_addr,
                           &r_last_meta,
                           &done_last_addr,
                           &done_last_meta);
    blocked_at = PsAppDiag_InferDdrBlocker(mem_state, flags, counts0, counts1, err_latch);

    xil_printf("[DDRLOG] %s state=0x%02x(%s) blocked_at=%s pc=0x%08x dma=0x%08x mix=0x%08x frame=%u miss=%u irq=0x%x err=0x%08x\r\n",
               label,
               (unsigned int)mem_state,
               PsAppDiag_MemStateName(mem_state),
               blocked_at,
               (unsigned int)dbg_pc,
               (unsigned int)dbg_dma,
               (unsigned int)dbg_mix,
               (unsigned int)(status1 & 0x3U),
               (unsigned int)((status1 >> 2) & 0x3FFFU),
               (unsigned int)(irq_sts & 0x3U),
               (unsigned int)err_latch);
    xil_printf("[DDRLOG] %s mem=0x%08x ctrl=0x%08x romsafe=%u vreq=%u vwe=%u vblk=%u vnz=%u sram=%u flags=0x%08x counts ch1=%u ddr=%u ar=%u r=%u dout=%u done=%u errp=%u we=%u pix=%u\r\n",
               label,
               (unsigned int)dbg_mem,
               (unsigned int)ctrl,
               (unsigned int)((ctrl & GBA_CTRL_ROM_DDR_SAFE) != 0U),
               (unsigned int)mem_vram_req,
               (unsigned int)mem_vram_we,
               (unsigned int)mem_vram_blk,
               (unsigned int)mem_vram_nz_seen,
               (unsigned int)mem_sram_enable,
               (unsigned int)flags,
               (unsigned int)(counts0 & 0xFFU),
               (unsigned int)((counts0 >> 8) & 0xFFU),
               (unsigned int)((counts0 >> 16) & 0xFFU),
               (unsigned int)((counts0 >> 24) & 0xFFU),
               (unsigned int)(counts1 & 0xFFU),
               (unsigned int)((counts1 >> 8) & 0xFFU),
               (unsigned int)((counts1 >> 16) & 0xFFU),
               (unsigned int)((counts1 >> 24) & 0xFU),
               (unsigned int)((counts1 >> 28) & 0xFU));
    xil_printf("[DDRLOG] %s last ch1=0x%08x lane=%u busy=%u ddr=0x%08x burst=%u rd=%u we=%u busy=%u ar=0x%08x len=%u size=%u burst=%u\r\n",
               label,
               (unsigned int)ch1_last_addr,
               (unsigned int)((ch1_last_meta >> 8) & 0x3U),
               (unsigned int)((ch1_last_meta >> 10) & 0x1U),
               (unsigned int)ddr_last_addr,
               (unsigned int)((ddr_last_meta >> 8) & 0xFFU),
               (unsigned int)((ddr_last_meta >> 16) & 0x1U),
               (unsigned int)((ddr_last_meta >> 17) & 0x1U),
               (unsigned int)((ddr_last_meta >> 18) & 0x1U),
               (unsigned int)ar_last_addr,
               (unsigned int)((ar_last_meta >> 8) & 0xFFU),
               (unsigned int)((ar_last_meta >> 16) & 0x7U),
               (unsigned int)((ar_last_meta >> 19) & 0x3U));
    xil_printf("[DDRLOG] %s last r_ar=0x%08x rresp=%u rlast=%u done=0x%08x done_lane=%u done_rresp=%u done_rlast=%u metas ch1=0x%08x ddr=0x%08x ar=0x%08x r=0x%08x done=0x%08x\r\n",
               label,
               (unsigned int)r_last_addr,
               (unsigned int)((r_last_meta >> 8) & 0x3U),
               (unsigned int)((r_last_meta >> 10) & 0x1U),
               (unsigned int)done_last_addr,
               (unsigned int)((done_last_meta >> 8) & 0x3U),
               (unsigned int)((done_last_meta >> 12) & 0x3U),
               (unsigned int)((done_last_meta >> 14) & 0x1U),
               (unsigned int)ch1_last_meta,
               (unsigned int)ddr_last_meta,
               (unsigned int)ar_last_meta,
               (unsigned int)r_last_meta,
               (unsigned int)done_last_meta);
}

void PsAppDiag_ServiceDdrLog(PsAppDiagContext *ctx,
                             u32 status1,
                             u32 dbg_pc,
                             u32 dbg_mem,
                             u32 dbg_dma) {
    u32 flags;
    u32 counts0;
    u32 counts1;
    u32 dummy_addr;
    u32 dummy_meta;
    u32 err_latch;
    u32 signature_changed;
    u32 force_print;
    u32 interval;
    char tag[24];

    if ((ctx == NULL) || (ctx->diag == NULL) || (ctx->diag->log_ddr_enable == 0U)) {
        return;
    }

    PsAppDiag_ReadDdrChain(ctx,
                           &flags,
                           &counts0,
                           &counts1,
                           &dummy_addr,
                           &dummy_meta,
                           &dummy_addr,
                           &dummy_meta,
                           &dummy_addr,
                           &dummy_meta,
                           &dummy_addr,
                           &dummy_meta,
                           &dummy_addr,
                           &dummy_meta);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    signature_changed = ((ctx->diag->ddrlog_last_status1 != status1) ||
                         (ctx->diag->ddrlog_last_pc != dbg_pc) ||
                         (ctx->diag->ddrlog_last_mem != dbg_mem) ||
                         (ctx->diag->ddrlog_last_dma != dbg_dma) ||
                         (ctx->diag->ddrlog_last_err != err_latch) ||
                         (ctx->diag->ddrlog_last_counts0 != counts0) ||
                         (ctx->diag->ddrlog_last_counts1 != counts1)) ? 1U : 0U;

    if (signature_changed != 0U) {
        ctx->diag->ddrlog_same_sample_count = 0U;
    } else if (ctx->diag->ddrlog_same_sample_count < 0xFFFFFFFFU) {
        ctx->diag->ddrlog_same_sample_count++;
    }

    interval = ctx->diag->ddrlog_interval_ticks;
    if (interval < PS_APP_DDRLOG_MIN_INTERVAL_TICKS) {
        interval = PS_APP_DDRLOG_DEFAULT_INTERVAL_TICKS;
    }

    ctx->diag->ddrlog_tick++;
    force_print = 0U;
    if (signature_changed != 0U) {
        force_print = 1U;
    }
    if (((dbg_mem >> 28) & 0x1U) != 0U) {
        force_print = 1U;
    }
    if ((dbg_mem & 0xFFU) == PS_APP_MEM_STATE_WAIT_SDRAM) {
        if ((ctx->diag->ddrlog_same_sample_count == PS_APP_AUTO_STALL_AUDIT_SAMPLES) ||
            ((ctx->diag->ddrlog_same_sample_count > PS_APP_AUTO_STALL_AUDIT_SAMPLES) &&
             ((ctx->diag->ddrlog_same_sample_count % interval) == 0U))) {
            force_print = 1U;
        }
    }
    if ((ctx->diag->ddrlog_tick % interval) == 0U) {
        force_print = 1U;
    }

    if (force_print != 0U) {
        (void)snprintf(tag, sizeof(tag), "auto%u", (unsigned int)ctx->diag->ddrlog_tick);
        PsAppDiag_PrintDdrLogSnapshot(ctx, tag);
    }

    ctx->diag->ddrlog_last_status1 = status1;
    ctx->diag->ddrlog_last_pc = dbg_pc;
    ctx->diag->ddrlog_last_mem = dbg_mem;
    ctx->diag->ddrlog_last_dma = dbg_dma;
    ctx->diag->ddrlog_last_err = err_latch;
    ctx->diag->ddrlog_last_counts0 = counts0;
    ctx->diag->ddrlog_last_counts1 = counts1;
}

void PsAppDiag_PrintStatus(PsAppDiagContext *ctx) {
    u32 ctrl;
    u32 status1;
    u32 rom_status;
    u32 err_latch;
    u32 irq_sts;
    u32 fbcap_status;
    u32 fbcap_seq;
    u32 frame_idx;
    u32 cycles_missing;
    u32 cycles_vsync_speed;
    u32 parked;
    u32 vdma_status;
    u32 vdma_errs;
    u32 blit_avg_us;

    if (ctx == NULL) {
        return;
    }

    ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
    status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
    rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS);
    fbcap_status = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
    fbcap_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    parked = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);
    vdma_errs = vdma_status & XAXIVDMA_SR_ERR_ALL_MASK;

    frame_idx = (status1 & 0x3U);
    cycles_missing = (status1 >> 2) & 0x3FFFU;
    cycles_vsync_speed = (status1 >> 16) & 0xFFFFU;
    blit_avg_us = (ctx->video->state->blit_count != 0U) ?
                      (ctx->video->state->blit_total_us / ctx->video->state->blit_count) :
                      0U;

    xil_printf("[STAT] frame=%u miss=%u vsync=%u rom=%u busy=%u irq=0x%x err=0x%08x park=%u vdma_sr=0x%08x vdma_irq=%u vdma_err=%u dma_err=0x%03x cap_seq=%u cap_buf=%u\r\n",
               (unsigned int)frame_idx,
               (unsigned int)cycles_missing,
               (unsigned int)cycles_vsync_speed,
               (unsigned int)ctx->rom->loaded,
               (unsigned int)(rom_status & 0x1U),
               (unsigned int)irq_sts,
               (unsigned int)err_latch,
               (unsigned int)parked,
               (unsigned int)vdma_status,
               (unsigned int)ctx->video->state->vdma_irq_count,
               (unsigned int)ctx->video->state->vdma_err_count,
               (unsigned int)vdma_errs,
               (unsigned int)fbcap_seq,
               (unsigned int)(fbcap_status & 0x1U));
    xil_printf("[STAT] blit count=%u last_us=%u max_us=%u avg_us=%u seq_gap_max=%u seq_glitch_drop=%u log_input=%u log_fbscan=%u log_ddr=%u ddr_interval=%u guard=%u romsafe=%u active=%u cd=%u rec=%u\r\n",
               (unsigned int)ctx->video->state->blit_count,
               (unsigned int)ctx->video->state->blit_last_us,
               (unsigned int)ctx->video->state->blit_max_us,
               (unsigned int)blit_avg_us,
               (unsigned int)ctx->video->state->blit_seq_gap_max,
               (unsigned int)ctx->video->state->blit_seq_glitch_drop,
               (unsigned int)(ctx->diag->log_input_delta_enable != 0U),
               (unsigned int)(ctx->diag->log_fbscan_auto_enable != 0U),
               (unsigned int)(ctx->diag->log_ddr_enable != 0U),
               (unsigned int)ctx->diag->ddrlog_interval_ticks,
               (unsigned int)(ctx->diag->pc_guard_enable != 0U),
               (unsigned int)((ctrl & GBA_CTRL_ROM_DDR_SAFE) != 0U),
               (unsigned int)(ctx->diag->pc_guard_recovery_active != 0U),
               (unsigned int)ctx->diag->pc_guard_cooldown_ticks,
               (unsigned int)ctx->diag->pc_guard_recovery_count);

    if ((irq_sts & 0x3U) != 0U) {
        PsGbaRegs_ClearIrqStatus(ctx->regs, irq_sts & 0x3U);
    }

    PsAppVideo_AttemptRecover(ctx->video, vdma_status, vdma_errs);
}

void PsAppDiag_PrintDiag(PsAppDiagContext *ctx) {
    u32 ctrl;
    u32 keys;
    u32 max_pak;
    u32 cycle;
    u32 rtc;
    u32 irq_en;
    u32 sw_reset;
    u32 physical_keys;
    u32 dbg_pc;
    u32 dbg_mix;
    u32 dbg_irq;
    u32 dbg_dma;
    u32 dbg_mem;
    u32 mem_sdram_timeout;
    u32 mem_eeprom_cmd;
    u32 mem_sram_enable;
    u32 gpu_vcount;
    u32 gpu_disp_low;
    u32 blit_avg_us;

    if (ctx == NULL) {
        return;
    }

    ctrl = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
    keys = PsGbaRegs_Read(ctx->regs, GBA_REG_KEYS);
    max_pak = PsGbaRegs_Read(ctx->regs, GBA_REG_MAX_PAK_ADDR);
    cycle = PsGbaRegs_Read(ctx->regs, GBA_REG_CYCLE_PRECALC);
    rtc = PsGbaRegs_Read(ctx->regs, GBA_REG_RTC_TIMESTAMP);
    irq_en = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_EN);
    sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET);
    physical_keys = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0) & 0x3FFU;
    dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_mix = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_MIX);
    dbg_irq = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_IRQ);
    dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    mem_sdram_timeout = (dbg_mem >> 28) & 0x1U;
    mem_eeprom_cmd = (dbg_mem >> 29) & 0x1U;
    mem_sram_enable = (dbg_mem >> 30) & 0x1U;
    gpu_vcount = (dbg_irq >> 17) & 0xFFU;
    gpu_disp_low = (dbg_irq >> 25) & 0x7FU;
    blit_avg_us = (ctx->video->state->blit_count != 0U) ?
                      (ctx->video->state->blit_total_us / ctx->video->state->blit_count) :
                      0U;

    xil_printf("[DIAG] ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x irq_en=0x%x reset=%u\r\n",
               (unsigned int)ctrl,
               (unsigned int)(keys & 0x3FFU),
               (unsigned int)max_pak,
               (unsigned int)(cycle & 0xFFFFU),
               (unsigned int)rtc,
               (unsigned int)(irq_en & 0x3U),
               (unsigned int)(sw_reset & 0x1U));
    xil_printf("[DIAG] physical_keys=0x%03x\r\n", (unsigned int)physical_keys);
    xil_printf("[DIAG] dbg pc=0x%08x mix=0x%08x irq=0x%08x dma=0x%08x mem=0x%08x sdram_timeout=%u eeprom_cmd=%u sram=%u\r\n",
               (unsigned int)dbg_pc,
               (unsigned int)dbg_mix,
               (unsigned int)dbg_irq,
               (unsigned int)dbg_dma,
               (unsigned int)dbg_mem,
               (unsigned int)mem_sdram_timeout,
               (unsigned int)mem_eeprom_cmd,
               (unsigned int)mem_sram_enable);
    xil_printf("[DIAG] gpu vcount=%u disp_lo=0x%02x\r\n",
               (unsigned int)gpu_vcount,
               (unsigned int)gpu_disp_low);

    xil_printf("[DIAG] audio rate=%u bits=%u mute=%u vol=%u\r\n",
               (unsigned int)ctx->audio->sample_rate_hz,
               (unsigned int)ctx->audio->bits_per_sample,
               (unsigned int)ctx->audio->mute,
               (unsigned int)ctx->audio->volume);
    xil_printf("[DIAG] rom loaded=%u busy=%u bytes=%u aligned=%u maxpak=0x%08x\r\n",
               (unsigned int)ctx->rom->loaded,
               (unsigned int)(PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS) & 0x1U),
               (unsigned int)ctx->rom->size_bytes,
               (unsigned int)ctx->rom->size_aligned,
               (unsigned int)ctx->config->max_pak_addr);
    xil_printf("[DIAG] rom path=%s\r\n",
               ctx->rom->path[0] != '\0' ? ctx->rom->path : "(none)");

    xil_printf("[DIAG] hdmi wh=%ux%u bpp=%u line=%u frame_bytes=%u base=0x%08x\r\n",
               (unsigned int)ctx->vdma->width,
               (unsigned int)ctx->vdma->height,
               (unsigned int)ctx->vdma->bytes_per_pixel,
               (unsigned int)ctx->vdma->line_stride_bytes,
               (unsigned int)ctx->vdma->frame_size_bytes,
               (unsigned int)ctx->vdma->frame_addrs[0]);
    xil_printf("[DIAG] blit count=%u last_us=%u max_us=%u avg_us=%u seq_gap_max=%u seq_glitch_drop=%u\r\n",
               (unsigned int)ctx->video->state->blit_count,
               (unsigned int)ctx->video->state->blit_last_us,
               (unsigned int)ctx->video->state->blit_max_us,
               (unsigned int)blit_avg_us,
               (unsigned int)ctx->video->state->blit_seq_gap_max,
               (unsigned int)ctx->video->state->blit_seq_glitch_drop);
    xil_printf("[DIAG] log input=%u fbscan=%u ddr=%u ddr_interval=%u guard=%u romsafe=%u active=%u cooldown=%u hold=%u rec=%u\r\n",
               (unsigned int)(ctx->diag->log_input_delta_enable != 0U),
               (unsigned int)(ctx->diag->log_fbscan_auto_enable != 0U),
               (unsigned int)(ctx->diag->log_ddr_enable != 0U),
               (unsigned int)ctx->diag->ddrlog_interval_ticks,
               (unsigned int)(ctx->diag->pc_guard_enable != 0U),
               (unsigned int)((ctrl & GBA_CTRL_ROM_DDR_SAFE) != 0U),
               (unsigned int)(ctx->diag->pc_guard_recovery_active != 0U),
               (unsigned int)ctx->diag->pc_guard_cooldown_ticks,
               (unsigned int)ctx->diag->pc_guard_reset_hold_ticks,
               (unsigned int)ctx->diag->pc_guard_recovery_count);
    xil_printf("[DIAG] guard last_bad_pc=0x%08x status1=0x%08x mem=0x%08x dma=0x%08x trigger_tick=%u\r\n",
               (unsigned int)ctx->diag->pc_guard_last_bad_pc,
               (unsigned int)ctx->diag->pc_guard_last_bad_status1,
               (unsigned int)ctx->diag->pc_guard_last_bad_mem,
               (unsigned int)ctx->diag->pc_guard_last_bad_dma,
               (unsigned int)ctx->diag->pc_guard_last_trigger_tick);

    PsAppDiag_PrintVdmaSnapshot(ctx, "diag");
}

void PsAppDiag_PrintAutoBootAudit(PsAppDiagContext *ctx) {
    if ((ctx == NULL) || (ctx->diag->auto_boot_audit_printed != 0U)) {
        return;
    }

    xil_printf("[AUTO] boot audit begin\r\n");
    PsAppDiag_PrintConfigReadback(ctx, "boot");
    PsAppDiag_PrintDiag(ctx);
    PsAppDiag_PrintChainSnapshot(ctx, "boot");
    PsAppDiag_PrintProbe(ctx);
    xil_printf("[AUTO] boot audit end\r\n");

    ctx->diag->auto_boot_audit_printed = 1U;
}

void PsAppDiag_MaybePrintStallAudit(PsAppDiagContext *ctx,
                                    u32 status1,
                                    u32 dbg_pc,
                                    u32 dbg_mem,
                                    u32 dbg_dma) {
    u32 frame_idx;
    u32 same_state;
    u32 mem_sdram_timeout;
    u32 err_latch;

    if ((ctx == NULL) || (ctx->rom->loaded == 0U) || (ctx->rom->is_loading != 0U)) {
        return;
    }

    frame_idx = status1 & 0x3U;
    mem_sdram_timeout = (dbg_mem >> 28) & 0x1U;
    same_state = ((ctx->diag->stall_last_pc == dbg_pc) &&
                  (ctx->diag->stall_last_mem == dbg_mem) &&
                  (ctx->diag->stall_last_dma == dbg_dma) &&
                  (ctx->diag->stall_last_frame == frame_idx)) ? 1U : 0U;

    if (same_state != 0U) {
        if (ctx->diag->stall_same_sample_count < 0xFFFFFFFFU) {
            ctx->diag->stall_same_sample_count++;
        }
    } else {
        ctx->diag->stall_same_sample_count = 0U;
        ctx->diag->auto_stall_audit_printed = 0U;
    }

    ctx->diag->stall_last_pc = dbg_pc;
    ctx->diag->stall_last_mem = dbg_mem;
    ctx->diag->stall_last_dma = dbg_dma;
    ctx->diag->stall_last_frame = frame_idx;

    if ((ctx->diag->auto_stall_audit_printed == 0U) &&
        ((dbg_mem & 0xFFU) == PS_APP_MEM_STATE_WAIT_SDRAM) &&
        (ctx->diag->stall_same_sample_count >= PS_APP_AUTO_STALL_AUDIT_SAMPLES)) {
        err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
        xil_printf("[AUTO] stall audit begin samples=%u mem_state=0x%02x sdram_timeout=%u err=0x%08x\r\n",
                   (unsigned int)ctx->diag->stall_same_sample_count,
                   (unsigned int)(dbg_mem & 0xFFU),
                   (unsigned int)mem_sdram_timeout,
                   (unsigned int)err_latch);
        PsAppDiag_PrintConfigReadback(ctx, "stall");
        PsAppDiag_PrintDiag(ctx);
        PsAppDiag_PrintChainSnapshot(ctx, "stall");
        PsAppDiag_PrintProbe(ctx);
        xil_printf("[AUTO] stall audit end\r\n");
        ctx->diag->auto_stall_audit_printed = 1U;
    }
}
