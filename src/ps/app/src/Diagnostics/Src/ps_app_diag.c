#include "Diagnostics/Inc/ps_app_diag.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xaxivdma_hw.h"
#include "xil_cache.h"
#include "xil_printf.h"

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

static void PsAppDiag_PrintCtrlDecode(u32 ctrl) {
    xil_printf("[PROBE] ctrl core=%u lock=%u turbo=%u sram=%u remap=%u flash1m=%u gpio=%u tilt=%u rom_loading=%u unsafe=%u\r\n",
               (unsigned int)((ctrl >> 0) & 0x1U),
               (unsigned int)((ctrl >> 1) & 0x1U),
               (unsigned int)((ctrl >> 2) & 0x1U),
               (unsigned int)((ctrl >> 4) & 0x1U),
               (unsigned int)((ctrl >> 5) & 0x1U),
               (unsigned int)((ctrl >> 9) & 0x1U),
               (unsigned int)((ctrl >> 10) & 0x1U),
               (unsigned int)((ctrl >> 11) & 0x1U),
               (unsigned int)((ctrl >> 8) & 0x1U),
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

void PsAppDiag_PrintPixcapSummary(PsAppDiagContext *ctx, u32 buf_idx) {
    u32 latest_seq;
    u32 latest_buf;
    u32 raw_hash;
    u32 row0_hash;
    u32 row_mid_hash;
    u32 row_last_hash;
    u32 word0;
    u32 word_mid;
    u32 word_last;
    u32 frame_idx;
    u32 hdmi_hash;

    if (ctx == NULL) {
        return;
    }

    PsAppVideo_ReadCaptureStatus(ctx->video, &latest_seq, &latest_buf);
    raw_hash = PsAppVideo_ComputeCaptureSparseHash(buf_idx, PS_APP_FRAME_HASH_SAMPLES);
    row0_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, 0U);
    row_mid_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, PS_APP_GBA_FRAME_HEIGHT / 2U);
    row_last_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, PS_APP_GBA_FRAME_HEIGHT - 1U);
    word0 = PsAppVideo_ReadCaptureWord(buf_idx, 0U);
    word_mid = PsAppVideo_ReadCaptureWord(buf_idx, PS_APP_GBA_FB_CAPTURE_WORDS / 2U);
    word_last = PsAppVideo_ReadCaptureWord(buf_idx, PS_APP_GBA_FB_CAPTURE_WORDS - 1U);
    frame_idx = PsAppVideo_DefaultFrameIndex(ctx->video);
    hdmi_hash = 0U;
    if ((ctx->vdma->is_ready != 0U) && (frame_idx < ctx->vdma->frame_count)) {
        hdmi_hash = PsAppVideo_ComputeSparseHash(ctx->vdma->frame_addrs[frame_idx],
                                                 ctx->vdma->frame_size_bytes,
                                                 PS_APP_FRAME_HASH_SAMPLES);
    }

    xil_printf("[PIXCAP] summary latest_seq=%u latest_buf=%u use_buf=%u last_seq=%u last_buf=%u last_frame=%u raw_hash=0x%08x hdmi_frame=%u hdmi_hash=0x%08x\r\n",
               (unsigned int)latest_seq,
               (unsigned int)latest_buf,
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)ctx->video->state->fbcap_last_frame_seq,
               (unsigned int)ctx->video->state->fbcap_last_buf_idx,
               (unsigned int)ctx->video->state->fbcap_last_frame_idx,
               (unsigned int)raw_hash,
               (unsigned int)frame_idx,
               (unsigned int)hdmi_hash);
    xil_printf("[PIXCAP] rows r0=0x%08x rmid=0x%08x rlast=0x%08x w0=0x%08x wmid=0x%08x wlast=0x%08x\r\n",
               (unsigned int)row0_hash,
               (unsigned int)row_mid_hash,
               (unsigned int)row_last_hash,
               (unsigned int)word0,
               (unsigned int)word_mid,
               (unsigned int)word_last);
}

void PsAppDiag_PrintPixcapRow(PsAppDiagContext *ctx, u32 buf_idx, u32 y) {
    const u32 sample_x0 = 0U;
    const u32 sample_x1 = 60U;
    const u32 sample_x2 = 120U;
    const u32 sample_x3 = 239U;
    u32 row_hash;
    u32 word0;
    u32 word_mid;
    u32 word_last;
    u32 base_word_idx;

    if ((ctx == NULL) || (y >= PS_APP_GBA_FRAME_HEIGHT)) {
        return;
    }

    row_hash = PsAppVideo_ComputeCaptureRowHash(buf_idx, y);
    base_word_idx = y * PS_APP_GBA_FB_CAPTURE_ROW_WORDS;
    word0 = PsAppVideo_ReadCaptureWord(buf_idx, base_word_idx + 0U);
    word_mid = PsAppVideo_ReadCaptureWord(buf_idx,
                                          base_word_idx + (PS_APP_GBA_FB_CAPTURE_ROW_WORDS / 2U));
    word_last = PsAppVideo_ReadCaptureWord(buf_idx,
                                           base_word_idx + (PS_APP_GBA_FB_CAPTURE_ROW_WORDS - 1U));

    xil_printf("[PIXCAP] row buf=%u y=%u hash=0x%08x w0=0x%08x wmid=0x%08x wlast=0x%08x\r\n",
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)y,
               (unsigned int)row_hash,
               (unsigned int)word0,
               (unsigned int)word_mid,
               (unsigned int)word_last);
    xil_printf("[PIXCAP] row samples x0=0x%04x x60=0x%04x x120=0x%04x x239=0x%04x\r\n",
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x0, y),
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x1, y),
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x2, y),
               (unsigned int)PsAppVideo_ReadCapturePixelRgb565(buf_idx, sample_x3, y));
}

void PsAppDiag_PrintPixcapCompare(PsAppDiagContext *ctx, u32 buf_idx, u32 frame_idx, u32 x, u32 y) {
    u16 src565;
    u32 expected;
    u32 active_x;
    u32 active_y;
    u32 active_w;
    u32 active_h;
    u32 fb_x;
    u32 fb_y;
    u32 fb_x1;
    u32 fb_y1;
    u32 fb00;
    u32 fb10;
    u32 fb01;
    u32 fb11;
    u32 match;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U) ||
        (x >= PS_APP_GBA_FRAME_WIDTH) || (y >= PS_APP_GBA_FRAME_HEIGHT) ||
        (frame_idx >= ctx->vdma->frame_count)) {
        return;
    }

    src565 = PsAppVideo_ReadCapturePixelRgb565(buf_idx, x, y);
    expected = PsAppVideo_ConvertRgb565ToXrgb8888(src565);
    PsAppVideo_ComputeDisplayWindow(ctx->video, &active_x, &active_y, &active_w, &active_h);
    if ((active_w == 0U) || (active_h == 0U)) {
        return;
    }

    fb_x = active_x + ((x * active_w) / PS_APP_GBA_FRAME_WIDTH);
    fb_y = active_y + ((y * active_h) / PS_APP_GBA_FRAME_HEIGHT);
    fb_x1 = active_x + (((x + 1U) * active_w) / PS_APP_GBA_FRAME_WIDTH);
    fb_y1 = active_y + (((y + 1U) * active_h) / PS_APP_GBA_FRAME_HEIGHT);
    if (fb_x1 > active_x) {
        fb_x1--;
    }
    if (fb_y1 > active_y) {
        fb_y1--;
    }
    if (fb_x1 < fb_x) {
        fb_x1 = fb_x;
    }
    if (fb_y1 < fb_y) {
        fb_y1 = fb_y;
    }

    fb00 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x + 0U, fb_y + 0U);
    fb10 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x1, fb_y + 0U);
    fb01 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x + 0U, fb_y1);
    fb11 = PsAppVideo_ReadHdmiFramePixel(ctx->video, frame_idx, fb_x1, fb_y1);
    match = (((fb00 & 0x00FFFFFFU) == expected) &&
             ((fb10 & 0x00FFFFFFU) == expected) &&
             ((fb01 & 0x00FFFFFFU) == expected) &&
             ((fb11 & 0x00FFFFFFU) == expected)) ? 1U : 0U;

    xil_printf("[PIXCAP] cmp buf=%u frame=%u x=%u y=%u src565=0x%04x exp=0x%08x match=%u\r\n",
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)frame_idx,
               (unsigned int)x,
               (unsigned int)y,
               (unsigned int)src565,
               (unsigned int)expected,
               (unsigned int)match);
    xil_printf("[PIXCAP] cmp fb00=0x%08x fb10=0x%08x fb01=0x%08x fb11=0x%08x\r\n",
               (unsigned int)fb00,
               (unsigned int)fb10,
               (unsigned int)fb01,
               (unsigned int)fb11);
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
    sw_reset = PsGbaRegs_Read(ctx->regs, GBA_REG_SW_RESET);
    rom_status = PsGbaRegs_Read(ctx->regs, GBA_REG_ROM_STATUS);
    irq_en = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_EN);
    display_frame = PsGbaRegs_Read(ctx->regs, GBA_REG_DISPLAY_FRAME);

    xil_printf("[CFG] %s ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x reset=%u rombusy=%u irq_en=0x%x disp=%u\r\n",
               label,
               (unsigned int)ctrl,
               (unsigned int)(keys & 0x3FFU),
               (unsigned int)max_pak,
               (unsigned int)(cycle & 0xFFFFU),
               (unsigned int)rtc,
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
    gpu_vcount = (dbg_irq >> 17) & 0xFFU;
    gpu_disp_low = (dbg_irq >> 25) & 0x7FU;

    xil_printf("[PROBE] shadow ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x irq_en=0x%x rom_loaded=%u rom_busy=%u path=%s\r\n",
               (unsigned int)ctx->config->ctrl,
               (unsigned int)(ctx->config->keys & 0x3FFU),
               (unsigned int)ctx->config->max_pak_addr,
               (unsigned int)ctx->config->cycle_precalc,
               (unsigned int)ctx->config->rtc_timestamp,
               (unsigned int)(ctx->config->irq_enable & 0x3U),
               (unsigned int)ctx->rom->loaded,
               (unsigned int)ctx->rom->is_loading,
               ctx->rom->path[0] != '\0' ? ctx->rom->path : "(none)");
    xil_printf("[PROBE] raw ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x reset=%u rom=0x%08x irq_en=0x%x irq_sts=0x%x\r\n",
               (unsigned int)ctrl,
               (unsigned int)(keys & 0x3FFU),
               (unsigned int)max_pak,
               (unsigned int)(cycle & 0xFFFFU),
               (unsigned int)rtc,
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
    xil_printf("[PROBE] dbg pc=0x%08x mix=0x%08x irq=0x%08x dma=0x%08x mem=0x%08x\r\n",
               (unsigned int)dbg_pc,
               (unsigned int)dbg_mix,
               (unsigned int)dbg_irq,
               (unsigned int)dbg_dma,
               (unsigned int)dbg_mem);
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

        xil_printf("[TRACE] i=%u frame=%u miss=%u vsync=%u reset=%u rombusy=%u irq=0x%x err=0x%08x park=%u vdma_sr=0x%08x vdma_irq=%u vdma_err=%u dma_err=0x%03x\r\n",
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

void PsAppDiag_PrintStatus(PsAppDiagContext *ctx) {
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
    xil_printf("[STAT] blit count=%u last_us=%u max_us=%u avg_us=%u seq_gap_max=%u seq_glitch_drop=%u log_input=%u log_fbscan=%u\r\n",
               (unsigned int)ctx->video->state->blit_count,
               (unsigned int)ctx->video->state->blit_last_us,
               (unsigned int)ctx->video->state->blit_max_us,
               (unsigned int)blit_avg_us,
               (unsigned int)ctx->video->state->blit_seq_gap_max,
               (unsigned int)ctx->video->state->blit_seq_glitch_drop,
               (unsigned int)(ctx->diag->log_input_delta_enable != 0U),
               (unsigned int)(ctx->diag->log_fbscan_auto_enable != 0U));

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
    xil_printf("[DIAG] dbg pc=0x%08x mix=0x%08x irq=0x%08x dma=0x%08x mem=0x%08x\r\n",
               (unsigned int)dbg_pc,
               (unsigned int)dbg_mix,
               (unsigned int)dbg_irq,
               (unsigned int)dbg_dma,
               (unsigned int)dbg_mem);
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
    xil_printf("[DIAG] log input=%u fbscan=%u\r\n",
               (unsigned int)(ctx->diag->log_input_delta_enable != 0U),
               (unsigned int)(ctx->diag->log_fbscan_auto_enable != 0U));

    PsAppDiag_PrintVdmaSnapshot(ctx, "diag");
}

void PsAppDiag_ScanFramebufferForAnomaly(PsAppDiagContext *ctx, const char *tag, u8 force_print) {
    u32 parked;
    UINTPTR fb_base;
    u32 line_step;
    u32 pixel_step;
    u32 white_lines;
    u32 black_lines;
    u32 total_sampled;
    u32 ly;
    u32 px;
    u32 anomaly;
    u32 active_x;
    u32 active_y;
    u32 active_w;
    u32 active_h;

    if ((ctx == NULL) || (ctx->vdma->is_ready == 0U)) {
        return;
    }

    parked = XAxiVdma_CurrFrameStore(&ctx->vdma->vdma, XAXIVDMA_READ);
    if (parked >= ctx->vdma->frame_count) {
        parked = 0U;
    }
    fb_base = ctx->vdma->frame_addrs[parked];
    if (ctx->vdma->line_stride_bytes == 0U) {
        return;
    }

    PsAppVideo_ComputeDisplayWindow(ctx->video, &active_x, &active_y, &active_w, &active_h);
    if ((active_w == 0U) || (active_h == 0U)) {
        return;
    }

    line_step = active_h / PS_APP_FBSCAN_SAMPLE_LINES;
    if (line_step == 0U) {
        line_step = 1U;
    }
    pixel_step = active_w / PS_APP_FBSCAN_SAMPLE_PIXELS;
    if (pixel_step == 0U) {
        pixel_step = 1U;
    }

    Xil_DCacheInvalidateRange((INTPTR)fb_base, ctx->vdma->frame_size_bytes);

    white_lines = 0U;
    black_lines = 0U;
    total_sampled = 0U;
    anomaly = 0U;

    for (ly = 0U; ly < active_h; ly += line_step) {
        UINTPTR line_base = fb_base +
                            ((active_y + ly) * ctx->vdma->line_stride_bytes) +
                            (active_x * sizeof(u32));
        volatile const u32 *line_ptr = (volatile const u32 *)line_base;
        u32 white_count = 0U;
        u32 black_count = 0U;

        for (px = 0U; px < active_w; px += pixel_step) {
            u32 pixel = line_ptr[px];
            if ((pixel & 0x00FFFFFFU) == PS_APP_FBSCAN_WHITE_PIXEL) {
                white_count++;
            } else if ((pixel & 0x00FFFFFFU) == PS_APP_FBSCAN_BLACK_PIXEL) {
                black_count++;
            }
        }

        total_sampled++;

        if (white_count >= (PS_APP_FBSCAN_SAMPLE_PIXELS / 2U)) {
            white_lines++;
        }
        if (black_count >= PS_APP_FBSCAN_SAMPLE_PIXELS) {
            black_lines++;
        }
    }

    if (white_lines >= (total_sampled / 4U)) {
        anomaly = 1U;
    }
    if ((ctx->rom->loaded != 0U) && (ctx->diag->delayed_chain_tick > 300U) &&
        (black_lines >= ((total_sampled * 3U) / 4U))) {
        anomaly = 2U;
    }

    if ((anomaly != 0U) || (force_print != 0U)) {
        xil_printf("[FBSCAN] %s park=%u fb=0x%08x white_lines=%u black_lines=%u total=%u anomaly=%u\r\n",
                   (tag != NULL) ? tag : "scan",
                   (unsigned int)parked,
                   (unsigned int)fb_base,
                   (unsigned int)white_lines,
                   (unsigned int)black_lines,
                   (unsigned int)total_sampled,
                   (unsigned int)anomaly);

        {
            u32 fi;
            const u32 active_base_off =
                (active_y * ctx->vdma->line_stride_bytes) + (active_x * sizeof(u32));
            const u32 active_mid_off =
                ((active_y + (active_h / 2U)) * ctx->vdma->line_stride_bytes) +
                ((active_x + (active_w / 2U)) * sizeof(u32));
            const u32 active_last_off =
                ((active_y + active_h - 1U) * ctx->vdma->line_stride_bytes) +
                ((active_x + active_w - 4U) * sizeof(u32));

            for (fi = 0U; fi < ctx->vdma->frame_count; fi++) {
                UINTPTR addr = ctx->vdma->frame_addrs[fi];
                Xil_DCacheInvalidateRange((INTPTR)addr, ctx->vdma->frame_size_bytes);
                xil_printf("[FBSCAN] fb%u act0=0x%08x act1=0x%08x act2=0x%08x act3=0x%08x mid0=0x%08x mid1=0x%08x tail0=0x%08x tail1=0x%08x\r\n",
                           (unsigned int)fi,
                           (unsigned int)Xil_In32(addr + active_base_off),
                           (unsigned int)Xil_In32(addr + active_base_off + 4U),
                           (unsigned int)Xil_In32(addr + active_base_off + 8U),
                           (unsigned int)Xil_In32(addr + active_base_off + 12U),
                           (unsigned int)Xil_In32(addr + active_mid_off),
                           (unsigned int)Xil_In32(addr + active_mid_off + 4U),
                           (unsigned int)Xil_In32(addr + active_last_off),
                           (unsigned int)Xil_In32(addr + active_last_off + 4U));
            }
        }
    }

    if (anomaly != 0U) {
        xil_printf("[FBSCAN] *** ANOMALY DETECTED type=%u *** dumping full state\r\n",
                   (unsigned int)anomaly);
        PsAppDiag_PrintChainSnapshot(ctx, "fbscan");
        PsAppDiag_PrintVdmaSnapshot(ctx, "fbscan");
        PsAppDiag_PrintConfigReadback(ctx, "fbscan");
        {
            u32 status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
            u32 err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
            u32 dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
            u32 dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
            xil_printf("[FBSCAN] status1=0x%08x err=0x%08x pc=0x%08x mem=0x%08x\r\n",
                       (unsigned int)status1,
                       (unsigned int)err_latch,
                       (unsigned int)dbg_pc,
                       (unsigned int)dbg_mem);
        }
        ctx->diag->fbscan_last_anomaly_tick = ctx->diag->fbscan_tick;
        ctx->diag->fbscan_dump_count++;
    }
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

    if ((ctx == NULL) || (ctx->rom->loaded == 0U) || (ctx->rom->is_loading != 0U)) {
        return;
    }

    frame_idx = status1 & 0x3U;
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
        (dbg_mem == PS_APP_MEM_STATE_WAIT_SDRAM) &&
        (ctx->diag->stall_same_sample_count >= PS_APP_AUTO_STALL_AUDIT_SAMPLES)) {
        xil_printf("[AUTO] stall audit begin samples=%u\r\n",
                   (unsigned int)ctx->diag->stall_same_sample_count);
        PsAppDiag_PrintConfigReadback(ctx, "stall");
        PsAppDiag_PrintDiag(ctx);
        PsAppDiag_PrintChainSnapshot(ctx, "stall");
        PsAppDiag_PrintProbe(ctx);
        xil_printf("[AUTO] stall audit end\r\n");
        ctx->diag->auto_stall_audit_printed = 1U;
    }
}
