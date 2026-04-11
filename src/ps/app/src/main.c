#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"

#include "sleep.h"
#include <stdlib.h>
#include <string.h>
#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xuartps.h"
#include "xaxivdma_hw.h"

#include "ps_audio_codec.h"
#include "ps_gba_regs.h"
#include "ps_hdmi_vdma.h"
#include "ps_rom_loader.h"

#define FB_REGION_BASE_ADDR      0x18000000U
#define FB_FRAME_STORE_BYTES     0x00200000U
#if defined(XPAR_ZYNQ_GBA_SYSTEM_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR)
#define FB_CAP_BRAM_BASE_ADDR    XPAR_ZYNQ_GBA_SYSTEM_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR
#elif defined(XPAR_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR)
#define FB_CAP_BRAM_BASE_ADDR    XPAR_FB_CAP_BRAM_CTRL_0_S_AXI_BASEADDR
#else
#define FB_CAP_BRAM_BASE_ADDR    0x44000000U
#endif
#define HDMI_WIDTH               640U
#define HDMI_HEIGHT              480U
#define HDMI_BPP                 4U
#define GBA_ROM_REGION_BASE_ADDR 0x100C0000U
#define GBA_ROM_REGION_MAX_BYTES (32U * 1024U * 1024U)
#define ROM_PATH_MAX_CHARS       128U
#define DEFAULT_ROM_SD_PATH      "0:/games/armwrestler.gba"

#define SYS_TASK_STACK_WORDS       (configMINIMAL_STACK_SIZE * 10U)
#define SYS_TASK_PRIORITY          (tskIDLE_PRIORITY + 3U)
#define CONSOLE_TASK_STACK_WORDS   (configMINIMAL_STACK_SIZE * 8U)
#define CONSOLE_TASK_PRIORITY      (tskIDLE_PRIORITY + 2U)
#define VALID_TASK_STACK_WORDS     (configMINIMAL_STACK_SIZE * 8U)
#define VALID_TASK_PRIORITY        (tskIDLE_PRIORITY + 2U)

#define UART_BAUDRATE              115200U
#define IRQ_MASK_VSYNC             0x1U
#define IRQ_MASK_ERROR             0x2U
#define IRQ_MASK_DEFAULT           (IRQ_MASK_VSYNC | IRQ_MASK_ERROR)
#define MONITOR_INTERVAL_MS        10U
#define HDMI_TEST_INTERVAL_MS      400U
#define TRACE_DEFAULT_SAMPLES      20U
#define TRACE_DEFAULT_INTERVAL_MS  100U
#define TRACE_MAX_SAMPLES          200U
#define FRAME_HASH_SAMPLES         64U
#define AUTO_BOOT_AUDIT_DELAY_MS   250U
#define AUTO_STALL_AUDIT_SAMPLES   50U
#define MEM_STATE_WAIT_SDRAM       0x0000000BU

/* 帧缓冲异常扫描参数 */
#define FBSCAN_INTERVAL_TICKS      100U  /* ~1s (100 * 10ms) */
#define FBSCAN_SAMPLE_LINES        16U
#define FBSCAN_SAMPLE_PIXELS       8U
#define FBSCAN_MAX_DUMPS           5U    /* 最多自动 dump 次数 */
#define FBSCAN_WHITE_PIXEL         0x00FFFFFFU
#define FBSCAN_BLACK_PIXEL         0x00000000U
#define FB_ACTIVE_X                80U
#define FB_ACTIVE_Y                80U
#define FB_ACTIVE_WIDTH            480U
#define FB_ACTIVE_HEIGHT           320U

#define GBA_CTRL_BOOT_REQUIRED (GBA_CTRL_LOCK_SPEED | GBA_CTRL_SRAM_FLASH_EN | GBA_CTRL_AUDIO_TONE)
#define GBA_FRAME_WIDTH            240U
#define GBA_FRAME_HEIGHT           160U
#define GBA_FB_CAPTURE_WORDS       ((240U * 160U) / 2U)
#define GBA_FB_CAPTURE_ROW_WORDS   (GBA_FB_CAPTURE_WORDS / GBA_FRAME_HEIGHT)
#define GBA_FB_CAPTURE_FRAME_BYTES (GBA_FB_CAPTURE_WORDS * sizeof(u32))

typedef struct {
    PsAudioCodec codec;
    PsHdmiVdma vdma;
    PsGbaRegs regs;
    XUartPs uart;

    u32 cfg_ctrl;
    u32 cfg_keys;
    u32 cfg_max_pak_addr;
    u32 cfg_cycle_precalc;
    u32 cfg_rtc_timestamp;
    u32 cfg_irq_enable;

    u32 audio_sample_rate_hz;
    u16 audio_bits_per_sample;
    u8 audio_mute;
    u8 audio_volume;
    u8 audio_tone_enable;

    u8 hdmi_test_enable;
    u8 hdmi_next_frame;
    u8 display_frame_idx;
    u32 hdmi_pattern_seed;
    u32 last_runtime_alert_sig;
    u32 last_runtime_irq_sts;
    u32 warn_print_count;
    u32 warn_suppress_ticks;
    u32 warn_last_err_latch;
    u32 warn_last_vdma_errs;
    u8 rom_loaded;
    u8 rom_is_loading;
    u32 rom_size_bytes;
    u32 rom_size_aligned;
    u32 last_physical_keys;
    u32 stall_last_pc;
    u32 stall_last_mem;
    u32 stall_last_dma;
    u32 stall_last_frame;
    u32 stall_same_sample_count;
    char rom_path[ROM_PATH_MAX_CHARS];
    u8 auto_boot_audit_printed;
    u8 auto_stall_audit_printed;
    u32 delayed_chain_tick;
    u8 delayed_chain_printed;

    u32 fbscan_tick;
    u32 fbscan_dump_count;
    u32 fbscan_last_anomaly_tick;
    u32 fbcap_last_frame_seq;
    u8 fbcap_last_buf_idx;
    u8 fbcap_last_frame_idx;

    volatile u32 vdma_irq_count;
    volatile u32 vdma_err_count;
    volatile u32 vdma_last_intr_mask;
    volatile u32 vdma_last_err_mask;
} ResearchAppCtx;

static ResearchAppCtx g_app;

static void print_vdma_snapshot(ResearchAppCtx *ctx, const char *tag);
static void sync_display_frame_idx(ResearchAppCtx *ctx);
static void print_config_readback(ResearchAppCtx *ctx, const char *tag);
static void print_chain_snapshot(ResearchAppCtx *ctx, const char *tag);
static void print_auto_boot_audit(ResearchAppCtx *ctx);
static u32 rgb565_to_xrgb8888(u16 pixel);
static void maybe_print_stall_audit(ResearchAppCtx *ctx,
                                    u32 status1,
                                    u32 dbg_pc,
                                    u32 dbg_mem,
                                    u32 dbg_dma);

static void vdma_general_cb(void *ref, u32 intr_mask) {
    ResearchAppCtx *ctx = (ResearchAppCtx *)ref;
    if (ctx == NULL) {
        return;
    }
    ctx->vdma_irq_count++;
    ctx->vdma_last_intr_mask = intr_mask;
}

static void vdma_error_cb(void *ref, u32 err_mask) {
    ResearchAppCtx *ctx = (ResearchAppCtx *)ref;
    if (ctx == NULL) {
        return;
    }
    ctx->vdma_err_count++;
    ctx->vdma_last_err_mask = err_mask;

    (void)XAxiVdma_ClearDmaChannelErrors(&ctx->vdma.vdma, XAXIVDMA_READ, err_mask);
}

static int parse_on_off(const char *s, u32 *value_out) {
    if ((s == NULL) || (value_out == NULL)) {
        return -1;
    }
    if ((strcmp(s, "on") == 0) || (strcmp(s, "1") == 0)) {
        *value_out = 1U;
        return 0;
    }
    if ((strcmp(s, "off") == 0) || (strcmp(s, "0") == 0)) {
        *value_out = 0U;
        return 0;
    }
    return -1;
}

static int parse_u32_value(const char *s, u32 *value_out) {
    char *end_ptr;
    unsigned long v;

    if ((s == NULL) || (value_out == NULL)) {
        return -1;
    }

    end_ptr = NULL;
    v = strtoul(s, &end_ptr, 0);
    if ((end_ptr == s) || ((end_ptr != NULL) && (*end_ptr != '\0'))) {
        return -1;
    }

    *value_out = (u32)v;
    return 0;
}

static int key_bit_from_name(const char *name) {
    if (name == NULL) return -1;
    if (strcmp(name, "a") == 0) return 0;
    if (strcmp(name, "b") == 0) return 1;
    if (strcmp(name, "select") == 0) return 2;
    if (strcmp(name, "start") == 0) return 3;
    if (strcmp(name, "right") == 0) return 4;
    if (strcmp(name, "left") == 0) return 5;
    if (strcmp(name, "up") == 0) return 6;
    if (strcmp(name, "down") == 0) return 7;
    if (strcmp(name, "r") == 0) return 8;
    if (strcmp(name, "l") == 0) return 9;
    return -1;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
    size_t src_len;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    src_len = strlen(src);
    if (src_len >= dst_size) {
        src_len = dst_size - 1U;
    }

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

static void sanitize_ascii_field(char *dst,
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

static u8 compute_gba_header_checksum(const u8 *rom) {
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

static u32 compute_sparse_hash(UINTPTR base_addr, u32 bytes, u32 samples) {
    volatile const u32 *ptr;
    u32 word_count;
    u32 step_words;
    u32 idx;
    u32 word_idx;
    u32 hash;

    if (bytes < 4U) {
        return 0U;
    }

    Xil_DCacheInvalidateRange((INTPTR)base_addr, bytes);

    ptr = (volatile const u32 *)base_addr;
    word_count = bytes >> 2;
    if (word_count == 0U) {
        return 0U;
    }

    if ((samples == 0U) || (samples > word_count)) {
        samples = word_count;
    }

    step_words = word_count / samples;
    if (step_words == 0U) {
        step_words = 1U;
    }

    hash = 2166136261U;
    word_idx = 0U;
    for (idx = 0U; idx < samples; ++idx) {
        hash ^= ptr[word_idx];
        hash *= 16777619U;

        if ((word_count - word_idx) <= step_words) {
            break;
        }
        word_idx += step_words;
    }

    return hash;
}

static u32 fnv1a_mix_u32(u32 hash, u32 value) {
    hash ^= value;
    hash *= 16777619U;
    return hash;
}

static UINTPTR fbcap_buf_base_addr(u32 buf_idx) {
    return (UINTPTR)(FB_CAP_BRAM_BASE_ADDR + ((buf_idx & 0x1U) * GBA_FB_CAPTURE_FRAME_BYTES));
}

static void read_fbcap_status_snapshot(ResearchAppCtx *ctx, u32 *seq_out, u32 *buf_out) {
    u32 seq0;
    u32 seq1;
    u32 status;

    if ((ctx == NULL) || (seq_out == NULL) || (buf_out == NULL)) {
        return;
    }

    seq0 = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_SEQ);
    status = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_STATUS);
    seq1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_SEQ);
    if (seq1 != seq0) {
        status = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_STATUS);
    }

    *seq_out = seq1;
    *buf_out = status & 0x1U;
}

static u32 default_fbcap_buf_idx(ResearchAppCtx *ctx) {
    u32 seq;
    u32 buf_idx;

    if (ctx == NULL) {
        return 0U;
    }

    if ((ctx->fbcap_last_frame_seq != 0U) && (ctx->fbcap_last_buf_idx < 2U)) {
        return ctx->fbcap_last_buf_idx;
    }

    read_fbcap_status_snapshot(ctx, &seq, &buf_idx);
    (void)seq;
    return buf_idx & 0x1U;
}

static u32 default_hdmi_frame_idx(ResearchAppCtx *ctx) {
    u32 frame_idx;

    if (ctx == NULL) {
        return 0U;
    }

    if ((ctx->vdma.frame_count != 0U) && (ctx->fbcap_last_frame_idx < ctx->vdma.frame_count)) {
        return ctx->fbcap_last_frame_idx;
    }

    if (ctx->vdma.is_ready == 0U) {
        return 0U;
    }

    frame_idx = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
    if (frame_idx >= ctx->vdma.frame_count) {
        frame_idx = 0U;
    }

    return frame_idx;
}

static u32 read_fbcap_word(u32 buf_idx, u32 word_idx) {
    if (word_idx >= GBA_FB_CAPTURE_WORDS) {
        return 0U;
    }

    return Xil_In32(fbcap_buf_base_addr(buf_idx) + (word_idx * sizeof(u32)));
}

static u16 read_fbcap_pixel_rgb565(u32 buf_idx, u32 x, u32 y) {
    u32 packed;
    u32 word_idx;

    if ((x >= GBA_FRAME_WIDTH) || (y >= GBA_FRAME_HEIGHT)) {
        return 0U;
    }

    word_idx = (y * GBA_FB_CAPTURE_ROW_WORDS) + (x >> 1);
    packed = read_fbcap_word(buf_idx, word_idx);
    if ((x & 0x1U) != 0U) {
        return (u16)((packed >> 16) & 0xFFFFU);
    }

    return (u16)(packed & 0xFFFFU);
}

static u32 compute_fbcap_sparse_hash(u32 buf_idx, u32 samples) {
    u32 word_count;
    u32 step_words;
    u32 idx;
    u32 word_idx;
    u32 hash;

    word_count = GBA_FB_CAPTURE_WORDS;
    if (word_count == 0U) {
        return 0U;
    }

    if ((samples == 0U) || (samples > word_count)) {
        samples = word_count;
    }

    step_words = word_count / samples;
    if (step_words == 0U) {
        step_words = 1U;
    }

    hash = 2166136261U;
    word_idx = 0U;
    for (idx = 0U; idx < samples; ++idx) {
        hash = fnv1a_mix_u32(hash, read_fbcap_word(buf_idx, word_idx));

        if ((word_count - word_idx) <= step_words) {
            break;
        }
        word_idx += step_words;
    }

    return hash;
}

static u32 compute_fbcap_row_hash(u32 buf_idx, u32 y) {
    u32 hash;
    u32 idx;
    u32 base_word_idx;

    if (y >= GBA_FRAME_HEIGHT) {
        return 0U;
    }

    hash = 2166136261U;
    base_word_idx = y * GBA_FB_CAPTURE_ROW_WORDS;
    for (idx = 0U; idx < GBA_FB_CAPTURE_ROW_WORDS; ++idx) {
        hash = fnv1a_mix_u32(hash, read_fbcap_word(buf_idx, base_word_idx + idx));
    }

    return hash;
}

static u32 read_hdmi_frame_pixel(ResearchAppCtx *ctx, u32 frame_idx, u32 x, u32 y) {
    UINTPTR addr;

    if ((ctx == NULL) || (ctx->vdma.is_ready == 0U) ||
        (frame_idx >= ctx->vdma.frame_count) ||
        (x >= ctx->vdma.width) ||
        (y >= ctx->vdma.height)) {
        return 0U;
    }

    Xil_DCacheInvalidateRange((INTPTR)ctx->vdma.frame_addrs[frame_idx], (INTPTR)ctx->vdma.frame_size_bytes);
    addr = ctx->vdma.frame_addrs[frame_idx]
         + (y * ctx->vdma.line_stride_bytes)
         + (x * sizeof(u32));
    return Xil_In32(addr);
}

static void print_pixcap_summary(ResearchAppCtx *ctx, u32 buf_idx) {
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

    read_fbcap_status_snapshot(ctx, &latest_seq, &latest_buf);
    raw_hash = compute_fbcap_sparse_hash(buf_idx, FRAME_HASH_SAMPLES);
    row0_hash = compute_fbcap_row_hash(buf_idx, 0U);
    row_mid_hash = compute_fbcap_row_hash(buf_idx, GBA_FRAME_HEIGHT / 2U);
    row_last_hash = compute_fbcap_row_hash(buf_idx, GBA_FRAME_HEIGHT - 1U);
    word0 = read_fbcap_word(buf_idx, 0U);
    word_mid = read_fbcap_word(buf_idx, GBA_FB_CAPTURE_WORDS / 2U);
    word_last = read_fbcap_word(buf_idx, GBA_FB_CAPTURE_WORDS - 1U);
    frame_idx = default_hdmi_frame_idx(ctx);
    hdmi_hash = 0U;
    if ((ctx->vdma.is_ready != 0U) && (frame_idx < ctx->vdma.frame_count)) {
        hdmi_hash = compute_sparse_hash(ctx->vdma.frame_addrs[frame_idx],
                                        ctx->vdma.frame_size_bytes,
                                        FRAME_HASH_SAMPLES);
    }

    xil_printf("[PIXCAP] summary latest_seq=%u latest_buf=%u use_buf=%u last_seq=%u last_buf=%u last_frame=%u raw_hash=0x%08x hdmi_frame=%u hdmi_hash=0x%08x\r\n",
               (unsigned int)latest_seq,
               (unsigned int)latest_buf,
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)ctx->fbcap_last_frame_seq,
               (unsigned int)ctx->fbcap_last_buf_idx,
               (unsigned int)ctx->fbcap_last_frame_idx,
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

static void print_pixcap_row(ResearchAppCtx *ctx, u32 buf_idx, u32 y) {
    const u32 sample_x0 = 0U;
    const u32 sample_x1 = 60U;
    const u32 sample_x2 = 120U;
    const u32 sample_x3 = 239U;
    u32 row_hash;
    u32 word0;
    u32 word_mid;
    u32 word_last;
    u32 base_word_idx;

    if ((ctx == NULL) || (y >= GBA_FRAME_HEIGHT)) {
        return;
    }

    row_hash = compute_fbcap_row_hash(buf_idx, y);
    base_word_idx = y * GBA_FB_CAPTURE_ROW_WORDS;
    word0 = read_fbcap_word(buf_idx, base_word_idx + 0U);
    word_mid = read_fbcap_word(buf_idx, base_word_idx + (GBA_FB_CAPTURE_ROW_WORDS / 2U));
    word_last = read_fbcap_word(buf_idx, base_word_idx + (GBA_FB_CAPTURE_ROW_WORDS - 1U));

    xil_printf("[PIXCAP] row buf=%u y=%u hash=0x%08x w0=0x%08x wmid=0x%08x wlast=0x%08x\r\n",
               (unsigned int)(buf_idx & 0x1U),
               (unsigned int)y,
               (unsigned int)row_hash,
               (unsigned int)word0,
               (unsigned int)word_mid,
               (unsigned int)word_last);
    xil_printf("[PIXCAP] row samples x0=0x%04x x60=0x%04x x120=0x%04x x239=0x%04x\r\n",
               (unsigned int)read_fbcap_pixel_rgb565(buf_idx, sample_x0, y),
               (unsigned int)read_fbcap_pixel_rgb565(buf_idx, sample_x1, y),
               (unsigned int)read_fbcap_pixel_rgb565(buf_idx, sample_x2, y),
               (unsigned int)read_fbcap_pixel_rgb565(buf_idx, sample_x3, y));
}

static void print_pixcap_compare(ResearchAppCtx *ctx, u32 buf_idx, u32 frame_idx, u32 x, u32 y) {
    u16 src565;
    u32 expected;
    u32 fb_x;
    u32 fb_y;
    u32 fb00;
    u32 fb10;
    u32 fb01;
    u32 fb11;
    u32 match;

    if ((ctx == NULL) || (ctx->vdma.is_ready == 0U) ||
        (x >= GBA_FRAME_WIDTH) || (y >= GBA_FRAME_HEIGHT) ||
        (frame_idx >= ctx->vdma.frame_count)) {
        return;
    }

    src565 = read_fbcap_pixel_rgb565(buf_idx, x, y);
    expected = rgb565_to_xrgb8888(src565);
    fb_x = FB_ACTIVE_X + (x * 2U);
    fb_y = FB_ACTIVE_Y + (y * 2U);
    fb00 = read_hdmi_frame_pixel(ctx, frame_idx, fb_x + 0U, fb_y + 0U);
    fb10 = read_hdmi_frame_pixel(ctx, frame_idx, fb_x + 1U, fb_y + 0U);
    fb01 = read_hdmi_frame_pixel(ctx, frame_idx, fb_x + 0U, fb_y + 1U);
    fb11 = read_hdmi_frame_pixel(ctx, frame_idx, fb_x + 1U, fb_y + 1U);
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

static void print_ctrl_decode(u32 ctrl) {
    xil_printf("[PROBE] ctrl core=%u lock=%u turbo=%u sram=%u remap=%u tone=%u rom_loading=%u\r\n",
               (unsigned int)((ctrl >> 0) & 0x1U),
               (unsigned int)((ctrl >> 1) & 0x1U),
               (unsigned int)((ctrl >> 2) & 0x1U),
               (unsigned int)((ctrl >> 4) & 0x1U),
               (unsigned int)((ctrl >> 5) & 0x1U),
               (unsigned int)((ctrl >> 6) & 0x1U),
               (unsigned int)((ctrl >> 8) & 0x1U));
}

static void print_rom_header(ResearchAppCtx *ctx) {
    const u8 *rom;
    char title[13];
    char game_code[5];
    char maker_code[3];
    u32 entry_word;
    u32 logo_hash;
    u32 idx;
    u8 checksum_calc;

    if ((ctx == NULL) || (ctx->rom_loaded == 0U)) {
        xil_printf("[ROMHDR] no rom loaded\r\n");
        return;
    }

    Xil_DCacheInvalidateRange((INTPTR)GBA_ROM_REGION_BASE_ADDR, 0xC0U);
    rom = (const u8 *)GBA_ROM_REGION_BASE_ADDR;

    sanitize_ascii_field(title, sizeof(title), &rom[0xA0U], 12U);
    sanitize_ascii_field(game_code, sizeof(game_code), &rom[0xACU], 4U);
    sanitize_ascii_field(maker_code, sizeof(maker_code), &rom[0xB0U], 2U);

    entry_word = ((u32)rom[0]) |
                 ((u32)rom[1] << 8) |
                 ((u32)rom[2] << 16) |
                 ((u32)rom[3] << 24);

    logo_hash = 2166136261U;
    for (idx = 0x04U; idx < 0xA0U; ++idx) {
        logo_hash ^= rom[idx];
        logo_hash *= 16777619U;
    }

    checksum_calc = compute_gba_header_checksum(rom);

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
               (unsigned int)Xil_In32(GBA_ROM_REGION_BASE_ADDR + 0x000U),
               (unsigned int)Xil_In32(GBA_ROM_REGION_BASE_ADDR + 0x004U),
               (unsigned int)Xil_In32(GBA_ROM_REGION_BASE_ADDR + 0x0A0U),
               (unsigned int)Xil_In32(GBA_ROM_REGION_BASE_ADDR + 0x0A4U));
}

static void print_framebuffer_probe(ResearchAppCtx *ctx) {
    u32 idx;
    u32 parked;

    if ((ctx == NULL) || (ctx->vdma.is_ready == 0U)) {
        return;
    }

    parked = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
    xil_printf("[PROBE] fb parked=%u frame_bytes=%u stride=%u\r\n",
               (unsigned int)parked,
               (unsigned int)ctx->vdma.frame_size_bytes,
               (unsigned int)ctx->vdma.line_stride_bytes);

    for (idx = 0U; idx < ctx->vdma.frame_count; ++idx) {
        UINTPTR base_addr;
        u32 hash;
        u32 word0;
        u32 word_mid;

        base_addr = ctx->vdma.frame_addrs[idx];
        hash = compute_sparse_hash(base_addr,
                                   ctx->vdma.frame_size_bytes,
                                   FRAME_HASH_SAMPLES);
        word0 = Xil_In32(base_addr + 0x000U);
        word_mid = Xil_In32(base_addr + ((ctx->vdma.frame_size_bytes / 2U) & ~0x3U));

        xil_printf("[PROBE] fb%u addr=0x%08x hash=0x%08x w0=0x%08x wmid=0x%08x\r\n",
                   (unsigned int)idx,
                   (unsigned int)base_addr,
                   (unsigned int)hash,
                   (unsigned int)word0,
                   (unsigned int)word_mid);
    }
}

static void print_probe(ResearchAppCtx *ctx) {
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

    ctrl = PsGbaRegs_Read(&ctx->regs, GBA_REG_CTRL);
    keys = PsGbaRegs_Read(&ctx->regs, GBA_REG_KEYS);
    max_pak = PsGbaRegs_Read(&ctx->regs, GBA_REG_MAX_PAK_ADDR);
    cycle = PsGbaRegs_Read(&ctx->regs, GBA_REG_CYCLE_PRECALC);
    rtc = PsGbaRegs_Read(&ctx->regs, GBA_REG_RTC_TIMESTAMP);
    status0 = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS0);
    status1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS1);
    sw_reset = PsGbaRegs_Read(&ctx->regs, GBA_REG_SW_RESET);
    rom_status = PsGbaRegs_Read(&ctx->regs, GBA_REG_ROM_STATUS);
    err_latch = PsGbaRegs_Read(&ctx->regs, GBA_REG_ERROR_LATCH);
    irq_en = PsGbaRegs_Read(&ctx->regs, GBA_REG_IRQ_EN);
    irq_sts = PsGbaRegs_Read(&ctx->regs, GBA_REG_IRQ_STS);
    dbg_pc = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_mix = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_CPU_MIX);
    dbg_irq = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_IRQ);
    dbg_dma = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_MEM);
    gpu_vcount = (dbg_irq >> 17) & 0xFFU;
    gpu_disp_low = (dbg_irq >> 25) & 0x7FU;

    xil_printf("[PROBE] shadow ctrl=0x%08x keys=0x%03x maxpak=0x%08x cycle=%u rtc=0x%08x irq_en=0x%x rom_loaded=%u rom_busy=%u path=%s\r\n",
               (unsigned int)ctx->cfg_ctrl,
               (unsigned int)(ctx->cfg_keys & 0x3FFU),
               (unsigned int)ctx->cfg_max_pak_addr,
               (unsigned int)ctx->cfg_cycle_precalc,
               (unsigned int)ctx->cfg_rtc_timestamp,
               (unsigned int)(ctx->cfg_irq_enable & 0x3U),
               (unsigned int)ctx->rom_loaded,
               (unsigned int)ctx->rom_is_loading,
               ctx->rom_path[0] != '\0' ? ctx->rom_path : "(none)");
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

    print_ctrl_decode(ctrl);
    print_rom_header(ctx);
    print_vdma_snapshot(ctx, "probe");
    print_framebuffer_probe(ctx);
}

static void print_trace(ResearchAppCtx *ctx, u32 samples, u32 interval_ms) {
    u32 idx;

    if (ctx == NULL) {
        return;
    }

    if (samples == 0U) {
        samples = TRACE_DEFAULT_SAMPLES;
    } else if (samples > TRACE_MAX_SAMPLES) {
        samples = TRACE_MAX_SAMPLES;
    }

    if (interval_ms == 0U) {
        interval_ms = TRACE_DEFAULT_INTERVAL_MS;
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
        u32 dbg_pc;
        u32 dbg_mem;
        u32 dbg_dma;

        status1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS1);
        irq_sts = PsGbaRegs_Read(&ctx->regs, GBA_REG_IRQ_STS) & 0x3U;
        sw_reset = PsGbaRegs_Read(&ctx->regs, GBA_REG_SW_RESET) & 0x1U;
        rom_status = PsGbaRegs_Read(&ctx->regs, GBA_REG_ROM_STATUS) & 0x1U;
        err_latch = PsGbaRegs_Read(&ctx->regs, GBA_REG_ERROR_LATCH);
        parked = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
        vdma_status = XAxiVdma_GetStatus(&ctx->vdma.vdma, XAXIVDMA_READ);
        dbg_pc = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_CPU_PC);
        dbg_mem = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_MEM);
        dbg_dma = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_DMA);

        xil_printf("[TRACE] i=%u frame=%u miss=%u vsync=%u reset=%u rombusy=%u irq=0x%x err=0x%08x park=%u vdma_sr=0x%08x pc=0x%08x mem=0x%08x dma=0x%08x\r\n",
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
                   (unsigned int)dbg_pc,
                   (unsigned int)dbg_mem,
                   (unsigned int)dbg_dma);

        if ((idx + 1U) < samples) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    xil_printf("[TRACE] end\r\n");
}

static int resolve_rom_path(const char *input, char *resolved_path, size_t resolved_path_size) {
    const char *effective_path;
    size_t input_len;

    if ((resolved_path == NULL) || (resolved_path_size == 0U)) {
        return -1;
    }

    effective_path = (input != NULL) ? input : DEFAULT_ROM_SD_PATH;
    input_len = strlen(effective_path);

    if (strstr(effective_path, ":/") != NULL) {
        if (input_len >= resolved_path_size) {
            return -1;
        }
        copy_text(resolved_path, resolved_path_size, effective_path);
        return 0;
    }

    if ((input_len + 3U) >= resolved_path_size) {
        return -1;
    }

    resolved_path[0] = '0';
    resolved_path[1] = ':';
    resolved_path[2] = '/';
    memcpy(&resolved_path[3], effective_path, input_len);
    resolved_path[input_len + 3U] = '\0';
    return 0;
}

static void apply_shadow_config(ResearchAppCtx *ctx) {
    PsGbaRegs_CommitConfig(&ctx->regs,
                           ctx->cfg_ctrl,
                           ctx->cfg_keys,
                           ctx->cfg_max_pak_addr,
                           ctx->cfg_cycle_precalc,
                           ctx->cfg_rtc_timestamp);
}

static u32 resolve_backbuffer_frame_idx(u32 display_frame_idx) {
    switch (display_frame_idx % 3U) {
        case 0U:
            return 2U;
        case 1U:
            return 0U;
        default:
            return 1U;
    }
}

static void present_next_backbuffer_frame(ResearchAppCtx *ctx) {
    u32 current_frame;
    u32 next_frame;

    if ((ctx == NULL) || (ctx->vdma.is_ready == 0U)) {
        return;
    }

    current_frame = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
    if (current_frame >= ctx->vdma.frame_count) {
        current_frame = 0U;
    }

    next_frame = resolve_backbuffer_frame_idx(current_frame);
    if (next_frame >= ctx->vdma.frame_count) {
        next_frame = 0U;
    }

    if (PsHdmiVdma_Park(&ctx->vdma, next_frame) == XST_SUCCESS) {
        ctx->display_frame_idx = (u8)next_frame;
        PsGbaRegs_SetDisplayFrameIdx(&ctx->regs, next_frame);
    }
}

static void sync_display_frame_idx(ResearchAppCtx *ctx) {
    u32 frame_idx;

    frame_idx = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
    if (frame_idx >= ctx->vdma.frame_count) {
        frame_idx = 0U;
    }

    if (ctx->display_frame_idx != (u8)frame_idx) {
        ctx->display_frame_idx = (u8)frame_idx;
        PsGbaRegs_SetDisplayFrameIdx(&ctx->regs, frame_idx);
    }
}

static void set_sw_reset(u32 asserted) {
    Xil_Out32(XPAR_ZYNQ_GBA_TOP_0_BASEADDR + GBA_REG_SW_RESET, asserted & 0x1U);
}

static XStatus load_rom_from_sd(ResearchAppCtx *ctx, const char *requested_path) {
    PsRomLoadResult load_result;
    char resolved_path[ROM_PATH_MAX_CHARS];
    XStatus status;

    if (resolve_rom_path(requested_path, resolved_path, sizeof(resolved_path)) != 0) {
        xil_printf("[ROM] invalid path\r\n");
        return XST_INVALID_PARAM;
    }

    ctx->rom_is_loading = 1U;
    ctx->rom_loaded = 0U;
    ctx->rom_size_bytes = 0U;
    ctx->rom_size_aligned = 0U;
    ctx->stall_last_pc = 0U;
    ctx->stall_last_mem = 0U;
    ctx->stall_last_dma = 0U;
    ctx->stall_last_frame = 0U;
    ctx->stall_same_sample_count = 0U;
    ctx->auto_stall_audit_printed = 0U;
    copy_text(ctx->rom_path, sizeof(ctx->rom_path), resolved_path);

    ctx->cfg_ctrl |= GBA_CTRL_BOOT_REQUIRED;
    ctx->cfg_ctrl &= ~GBA_CTRL_CORE_ON;
    ctx->cfg_ctrl |= GBA_CTRL_ROM_LOADING;
    ctx->cfg_max_pak_addr = 0U;
    apply_shadow_config(ctx);
    print_config_readback(ctx, "rom-preload");

    set_sw_reset(1U);
    usleep(2000U);
    print_config_readback(ctx, "reset-assert");

    xil_printf("[ROM] load %s\r\n", resolved_path);
    xil_printf("[ROM] target addr=0x%08x limit=%u\r\n",
               (unsigned int)GBA_ROM_REGION_BASE_ADDR,
               (unsigned int)GBA_ROM_REGION_MAX_BYTES);
    status = PsRomLoader_LoadSdFile(resolved_path,
                                    (UINTPTR)GBA_ROM_REGION_BASE_ADDR,
                                    GBA_ROM_REGION_MAX_BYTES,
                                    &load_result);
    if (status != XST_SUCCESS) {
        ctx->cfg_ctrl &= ~GBA_CTRL_ROM_LOADING;
        ctx->cfg_max_pak_addr = 0U;
        apply_shadow_config(ctx);
        set_sw_reset(1U);
        ctx->rom_is_loading = 0U;
        xil_printf("[ROM] load failed: %s\r\n",
                   PsRomLoader_StrError(load_result.fs_result));
        return status;
    }

    ctx->cfg_max_pak_addr = load_result.max_pak_addr & 0x1FFFFFFU;
    ctx->cfg_ctrl &= ~GBA_CTRL_ROM_LOADING;
    ctx->cfg_ctrl |= (GBA_CTRL_CORE_ON | GBA_CTRL_BOOT_REQUIRED);
    apply_shadow_config(ctx);
    (void)PsHdmiVdma_FillAllFrames(&ctx->vdma, 0x00000000U);
    (void)PsHdmiVdma_Park(&ctx->vdma, 0U);
    sync_display_frame_idx(ctx);
    ctx->fbcap_last_frame_seq = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_SEQ);
    print_config_readback(ctx, "rom-postload");

    usleep(2000U);
    set_sw_reset(0U);
    usleep(2000U);
    print_config_readback(ctx, "reset-release");
    apply_shadow_config(ctx);
    usleep(2000U);
    print_config_readback(ctx, "post-release-commit");

    ctx->rom_loaded = 1U;
    ctx->rom_is_loading = 0U;
    ctx->rom_size_bytes = load_result.bytes_loaded;
    ctx->rom_size_aligned = load_result.bytes_aligned;

    xil_printf("[ROM] ready bytes=%u aligned=%u maxpak=0x%08x addr=0x%08x\r\n",
               (unsigned int)ctx->rom_size_bytes,
               (unsigned int)ctx->rom_size_aligned,
               (unsigned int)ctx->cfg_max_pak_addr,
               (unsigned int)GBA_ROM_REGION_BASE_ADDR);
    print_rom_header(ctx);

    return XST_SUCCESS;
}

static XStatus autoload_default_rom(ResearchAppCtx *ctx) {
    XStatus status;

    xil_printf("[INIT] 9: default rom path=%s\r\n", DEFAULT_ROM_SD_PATH);
    status = load_rom_from_sd(ctx, DEFAULT_ROM_SD_PATH);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 9 info: place a known-good ROM at %s\r\n",
                   DEFAULT_ROM_SD_PATH);
        xil_printf("[INIT] 9 info: or use 'rom load <path>' from the UART console\r\n");
        xil_printf("[INIT] 9 info: avoid relying on the previous hardcoded homebrew ROM for bring-up\r\n");
    }

    return status;
}

static XStatus program_audio_runtime(ResearchAppCtx *ctx) {
    XStatus status;

    status = PsAudioCodec_ProgramPlayback(&ctx->codec,
                                          ctx->audio_sample_rate_hz,
                                          ctx->audio_bits_per_sample);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = PsAudioCodec_SetHeadphoneVolume(&ctx->codec, ctx->audio_volume);
    if (status != XST_SUCCESS) {
        return status;
    }

    return PsAudioCodec_SetMute(&ctx->codec, ctx->audio_mute);
}

static XStatus render_boot_patterns(ResearchAppCtx *ctx) {
    XStatus status;
    u32 idx;

    xil_printf("[INIT] 5.1: clear all frames @0x%08x stride=%u size=%u\r\n",
               (unsigned int)ctx->vdma.frame_addrs[0],
               (unsigned int)FB_FRAME_STORE_BYTES,
               (unsigned int)ctx->vdma.frame_size_bytes);
    status = PsHdmiVdma_FillAllFrames(&ctx->vdma, 0x00000000U);
    if (status != XST_SUCCESS) {
        return status;
    }
    xil_printf("[INIT] 5.1: clear done\r\n");

    for (idx = 0U; idx < ctx->vdma.frame_count; ++idx) {
        xil_printf("[INIT] 5.2: draw frame %u @0x%08x\r\n",
                   (unsigned int)idx,
                   (unsigned int)ctx->vdma.frame_addrs[idx]);
        status = PsHdmiVdma_DrawTestPattern(&ctx->vdma, idx, idx);
        if (status != XST_SUCCESS) {
            return status;
        }
    }
    xil_printf("[INIT] 5.2: draw done\r\n");

    status = PsHdmiVdma_Park(&ctx->vdma, 0U);
    if (status != XST_SUCCESS) {
        return status;
    }

    ctx->hdmi_next_frame = 1U;
    ctx->hdmi_pattern_seed = 3U;
    return XST_SUCCESS;
}

static u32 rgb565_to_xrgb8888(u16 pixel) {
    u32 r5;
    u32 g6;
    u32 b5;
    u32 r8;
    u32 g8;
    u32 b8;

    r5 = (pixel >> 11) & 0x1FU;
    g6 = (pixel >> 5) & 0x3FU;
    b5 = pixel & 0x1FU;

    r8 = (r5 << 3) | (r5 >> 2);
    g8 = (g6 << 2) | (g6 >> 4);
    b8 = (b5 << 3) | (b5 >> 2);

    return (b8 << 16) | (g8 << 8) | r8;
}

static XStatus blit_captured_frame_to_hdmi(ResearchAppCtx *ctx, u32 frame_seq, u32 buf_idx) {
    u32 current_frame;
    u32 target_frame;
    u32 *fb32;
    volatile const u32 *src_words;
    u32 y;
    u32 word_idx;

    if ((ctx == NULL) || (ctx->vdma.is_ready == 0U)) {
        return XST_FAILURE;
    }

    current_frame = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
    if (current_frame >= ctx->vdma.frame_count) {
        current_frame = 0U;
    }

    target_frame = resolve_backbuffer_frame_idx(current_frame);
    if (target_frame >= ctx->vdma.frame_count) {
        target_frame = 0U;
    }

    fb32 = (u32 *)ctx->vdma.frame_addrs[target_frame];
    src_words = (volatile const u32 *)(FB_CAP_BRAM_BASE_ADDR + (buf_idx * GBA_FB_CAPTURE_FRAME_BYTES));

    for (y = 0U; y < 160U; ++y) {
        u32 dst_row0;
        u32 dst_row1;

        dst_row0 = ((FB_ACTIVE_Y + (y * 2U)) * ctx->vdma.width) + FB_ACTIVE_X;
        dst_row1 = dst_row0 + ctx->vdma.width;

        for (word_idx = 0U; word_idx < GBA_FB_CAPTURE_ROW_WORDS; ++word_idx) {
            u32 packed;
            u32 color0;
            u32 color1;
            u32 dst_x;

            packed = src_words[(y * GBA_FB_CAPTURE_ROW_WORDS) + word_idx];
            color0 = rgb565_to_xrgb8888((u16)(packed & 0xFFFFU));
            color1 = rgb565_to_xrgb8888((u16)((packed >> 16) & 0xFFFFU));
            dst_x = word_idx * 4U;

            fb32[dst_row0 + dst_x + 0U] = color0;
            fb32[dst_row0 + dst_x + 1U] = color0;
            fb32[dst_row0 + dst_x + 2U] = color1;
            fb32[dst_row0 + dst_x + 3U] = color1;
            fb32[dst_row1 + dst_x + 0U] = color0;
            fb32[dst_row1 + dst_x + 1U] = color0;
            fb32[dst_row1 + dst_x + 2U] = color1;
            fb32[dst_row1 + dst_x + 3U] = color1;
        }
    }

    Xil_DCacheFlushRange((INTPTR)ctx->vdma.frame_addrs[target_frame], (INTPTR)ctx->vdma.frame_size_bytes);
    if (PsHdmiVdma_Park(&ctx->vdma, target_frame) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    ctx->display_frame_idx = (u8)target_frame;
    ctx->fbcap_last_frame_seq = frame_seq;
    ctx->fbcap_last_buf_idx = (u8)(buf_idx & 0x1U);
    ctx->fbcap_last_frame_idx = (u8)target_frame;
    PsGbaRegs_SetDisplayFrameIdx(&ctx->regs, target_frame);
    return XST_SUCCESS;
}

static void present_captured_frame_if_ready(ResearchAppCtx *ctx) {
    u32 seq0;
    u32 seq1;
    u32 status;

    if ((ctx == NULL) || (ctx->rom_loaded == 0U) || (ctx->hdmi_test_enable != 0U)) {
        return;
    }

    seq0 = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_SEQ);
    status = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_STATUS);
    seq1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_SEQ);
    if (seq1 != seq0) {
        status = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_STATUS);
    }

    if ((seq1 == 0U) || (seq1 == ctx->fbcap_last_frame_seq)) {
        return;
    }

    (void)blit_captured_frame_to_hdmi(ctx, seq1, status & 0x1U);
}

static void print_vdma_snapshot(ResearchAppCtx *ctx, const char *tag) {
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

    if ((ctx == NULL) || (ctx->vdma.is_ready == 0U)) {
        return;
    }

    chan_base = ctx->vdma.vdma.BaseAddr + XAXIVDMA_TX_OFFSET;
    cfg_base = ctx->vdma.vdma.BaseAddr + XAXIVDMA_MM2S_ADDR_OFFSET;

    cr = XAxiVdma_ReadReg(chan_base, XAXIVDMA_CR_OFFSET);
    sr = XAxiVdma_ReadReg(chan_base, XAXIVDMA_SR_OFFSET);
    vsize = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_VSIZE_OFFSET);
    hsize = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_HSIZE_OFFSET);
    strd_frmdly = XAxiVdma_ReadReg(cfg_base, XAXIVDMA_STRD_FRMDLY_OFFSET);
    parkptr = XAxiVdma_ReadReg(ctx->vdma.vdma.BaseAddr, XAXIVDMA_PARKPTR_OFFSET);
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

static void print_config_readback(ResearchAppCtx *ctx, const char *tag) {
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
    ctrl = PsGbaRegs_Read(&ctx->regs, GBA_REG_CTRL);
    keys = PsGbaRegs_Read(&ctx->regs, GBA_REG_KEYS);
    max_pak = PsGbaRegs_Read(&ctx->regs, GBA_REG_MAX_PAK_ADDR);
    cycle = PsGbaRegs_Read(&ctx->regs, GBA_REG_CYCLE_PRECALC);
    rtc = PsGbaRegs_Read(&ctx->regs, GBA_REG_RTC_TIMESTAMP);
    sw_reset = PsGbaRegs_Read(&ctx->regs, GBA_REG_SW_RESET);
    rom_status = PsGbaRegs_Read(&ctx->regs, GBA_REG_ROM_STATUS);
    irq_en = PsGbaRegs_Read(&ctx->regs, GBA_REG_IRQ_EN);
    display_frame = PsGbaRegs_Read(&ctx->regs, GBA_REG_DISPLAY_FRAME);

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

static void print_chain_snapshot(ResearchAppCtx *ctx, const char *tag) {
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
    flags = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_CHAIN_FLAGS);
    counts0 = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_CHAIN_COUNTS0);
    counts1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_CHAIN_COUNTS1);
    ch1_first_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_CH1_FIRST_ADDR);
    ch1_first_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_CH1_FIRST_META);
    ch1_last_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_CH1_LAST_ADDR);
    ch1_last_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_CH1_LAST_META);
    ddr_first_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DDR_FIRST_ADDR);
    ddr_first_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DDR_FIRST_META);
    ddr_last_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DDR_LAST_ADDR);
    ddr_last_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DDR_LAST_META);
    ar_first_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_AR_FIRST_ADDR);
    ar_first_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_AR_FIRST_META);
    ar_last_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_AR_LAST_ADDR);
    ar_last_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_AR_LAST_META);
    r_first_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_R_FIRST_ADDR);
    r_first_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_R_FIRST_META);
    r_last_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_R_LAST_ADDR);
    r_last_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_AXI_R_LAST_META);
    done_first_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DONE_FIRST_ADDR);
    done_first_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DONE_FIRST_META);
    done_last_addr = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DONE_LAST_ADDR);
    done_last_meta = PsGbaRegs_Read(&ctx->regs, GBA_REG_DBG_DONE_LAST_META);

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

static void scan_fb_for_anomaly(ResearchAppCtx *ctx, const char *tag, u8 force_print) {
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

    if ((ctx == NULL) || (ctx->vdma.is_ready == 0U)) {
        return;
    }

    parked = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
    if (parked >= ctx->vdma.frame_count) {
        parked = 0U;
    }
    fb_base = ctx->vdma.frame_addrs[parked];
    if (ctx->vdma.line_stride_bytes == 0U) {
        return;
    }

    line_step = FB_ACTIVE_HEIGHT / FBSCAN_SAMPLE_LINES;
    if (line_step == 0U) line_step = 1U;
    pixel_step = FB_ACTIVE_WIDTH / FBSCAN_SAMPLE_PIXELS;
    if (pixel_step == 0U) pixel_step = 1U;

    Xil_DCacheInvalidateRange((INTPTR)fb_base, ctx->vdma.frame_size_bytes);

    white_lines = 0U;
    black_lines = 0U;
    total_sampled = 0U;
    anomaly = 0U;

    for (ly = 0U; ly < FB_ACTIVE_HEIGHT; ly += line_step) {
        UINTPTR line_base = fb_base
                          + ((FB_ACTIVE_Y + ly) * ctx->vdma.line_stride_bytes)
                          + (FB_ACTIVE_X * sizeof(u32));
        volatile const u32 *line_ptr = (volatile const u32 *)line_base;
        u32 white_count = 0U;
        u32 black_count = 0U;

        for (px = 0U; px < FB_ACTIVE_WIDTH; px += pixel_step) {
            u32 pixel = line_ptr[px];
            if ((pixel & 0x00FFFFFFU) == FBSCAN_WHITE_PIXEL) {
                white_count++;
            } else if ((pixel & 0x00FFFFFFU) == FBSCAN_BLACK_PIXEL) {
                black_count++;
            }
        }

        total_sampled++;

        if (white_count >= (FBSCAN_SAMPLE_PIXELS / 2U)) {
            white_lines++;
        }
        if (black_count >= FBSCAN_SAMPLE_PIXELS) {
            black_lines++;
        }
    }

    /* 异常判定：超过 1/4 采样行全白，或超过 3/4 行全黑（ROM 运行后不应全黑） */
    if (white_lines >= (total_sampled / 4U)) {
        anomaly = 1U;
    }
    if ((ctx->rom_loaded != 0U) && (ctx->delayed_chain_tick > 300U) &&
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

        /* 直接打印活动窗口关键采样点，避免被 frame base / frame midpoint 的无效区域误导 */
        {
            u32 fi;
            const u32 active_base_off =
                (FB_ACTIVE_Y * ctx->vdma.line_stride_bytes) + (FB_ACTIVE_X * sizeof(u32));
            const u32 active_mid_off =
                ((FB_ACTIVE_Y + (FB_ACTIVE_HEIGHT / 2U)) * ctx->vdma.line_stride_bytes) +
                ((FB_ACTIVE_X + (FB_ACTIVE_WIDTH / 2U)) * sizeof(u32));
            const u32 active_last_off =
                ((FB_ACTIVE_Y + FB_ACTIVE_HEIGHT - 1U) * ctx->vdma.line_stride_bytes) +
                ((FB_ACTIVE_X + FB_ACTIVE_WIDTH - 4U) * sizeof(u32));
            for (fi = 0U; fi < ctx->vdma.frame_count; fi++) {
                UINTPTR addr = ctx->vdma.frame_addrs[fi];
                Xil_DCacheInvalidateRange((INTPTR)addr, ctx->vdma.frame_size_bytes);
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
        print_chain_snapshot(ctx, "fbscan");
        print_vdma_snapshot(ctx, "fbscan");
        print_config_readback(ctx, "fbscan");
        {
            u32 status1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS1);
            u32 err_latch = PsGbaRegs_Read(&ctx->regs, GBA_REG_ERROR_LATCH);
            u32 dbg_pc = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_CPU_PC);
            u32 dbg_mem = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_MEM);
            xil_printf("[FBSCAN] status1=0x%08x err=0x%08x pc=0x%08x mem=0x%08x\r\n",
                       (unsigned int)status1,
                       (unsigned int)err_latch,
                       (unsigned int)dbg_pc,
                       (unsigned int)dbg_mem);
        }
        ctx->fbscan_last_anomaly_tick = ctx->fbscan_tick;
        ctx->fbscan_dump_count++;
    }
}

static void print_physical_keys(u32 keys_mask) {
    static const char *names[10] = {
        "A", "B", "SELECT", "START", "RIGHT",
        "LEFT", "UP", "DOWN", "R", "L"
    };
    u32 idx;
    u32 any;

    xil_printf("[INPUT] phys=0x%03x", (unsigned int)(keys_mask & 0x3FFU));
    any = 0U;
    for (idx = 0U; idx < 10U; ++idx) {
        if ((keys_mask & (1U << idx)) != 0U) {
            xil_printf("%s%s", any != 0U ? "+" : " ", names[idx]);
            any = 1U;
        }
    }
    if (any == 0U) {
        xil_printf(" (idle)");
    }
    xil_printf("\r\n");
}

static void attempt_vdma_recover(ResearchAppCtx *ctx, u32 vdma_status, u32 vdma_errs) {
    u32 is_halted;

    if (ctx == NULL) {
        return;
    }

    is_halted = (vdma_status & XAXIVDMA_SR_HALTED_MASK) ? 1U : 0U;
    if ((vdma_errs == 0U) && (is_halted == 0U)) {
        return;
    }

    xil_printf("[VDMA] recover: sr=0x%08x err=0x%03x halted=%u\r\n",
               (unsigned int)vdma_status,
               (unsigned int)vdma_errs,
               (unsigned int)is_halted);

    if (vdma_errs != 0U) {
        (void)XAxiVdma_ClearDmaChannelErrors(&ctx->vdma.vdma, XAXIVDMA_READ, vdma_errs);
    }

    if (is_halted != 0U) {
        (void)XAxiVdma_DmaStart(&ctx->vdma.vdma, XAXIVDMA_READ);
    }

    (void)PsHdmiVdma_Park(&ctx->vdma, 0U);
    print_vdma_snapshot(ctx, "post-recover");
}

static void print_status(ResearchAppCtx *ctx) {
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

    status1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS1);
    rom_status = PsGbaRegs_Read(&ctx->regs, GBA_REG_ROM_STATUS);
    err_latch = PsGbaRegs_Read(&ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(&ctx->regs, GBA_REG_IRQ_STS);
    fbcap_status = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_STATUS);
    fbcap_seq = PsGbaRegs_Read(&ctx->regs, GBA_REG_FB_CAP_SEQ);
    parked = XAxiVdma_CurrFrameStore(&ctx->vdma.vdma, XAXIVDMA_READ);
    vdma_status = XAxiVdma_GetStatus(&ctx->vdma.vdma, XAXIVDMA_READ);
    vdma_errs = vdma_status & XAXIVDMA_SR_ERR_ALL_MASK;

    frame_idx = (status1 & 0x3U);
    cycles_missing = (status1 >> 2) & 0x3FFFU;
    cycles_vsync_speed = (status1 >> 16) & 0xFFFFU;

    xil_printf("[STAT] frame=%u miss=%u vsync=%u rom=%u busy=%u irq=0x%x err=0x%08x park=%u vdma_sr=0x%08x vdma_irq=%u vdma_err=%u dma_err=0x%03x test=%u cap_seq=%u cap_buf=%u\r\n",
               (unsigned int)frame_idx,
               (unsigned int)cycles_missing,
               (unsigned int)cycles_vsync_speed,
               (unsigned int)ctx->rom_loaded,
               (unsigned int)(rom_status & 0x1U),
               (unsigned int)irq_sts,
               (unsigned int)err_latch,
               (unsigned int)parked,
               (unsigned int)vdma_status,
               (unsigned int)ctx->vdma_irq_count,
               (unsigned int)ctx->vdma_err_count,
               (unsigned int)vdma_errs,
               (unsigned int)ctx->hdmi_test_enable,
               (unsigned int)fbcap_seq,
               (unsigned int)(fbcap_status & 0x1U));

    if ((irq_sts & 0x3U) != 0U) {
        PsGbaRegs_ClearIrqStatus(&ctx->regs, irq_sts & 0x3U);
    }

    attempt_vdma_recover(ctx, vdma_status, vdma_errs);
}

static void service_runtime_status(ResearchAppCtx *ctx) {
    u32 status0;
    u32 status1;
    u32 err_latch;
    u32 irq_sts;
    u32 cycles_missing;
    u32 dbg_pc;
    u32 dbg_dma;
    u32 dbg_mem;
    u32 vdma_status;
    u32 vdma_errs;
    u32 physical_keys;

    status0 = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS0);
    status1 = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS1);
    err_latch = PsGbaRegs_Read(&ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(&ctx->regs, GBA_REG_IRQ_STS) & 0x3U;
    dbg_pc = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_dma = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_MEM);
    vdma_status = XAxiVdma_GetStatus(&ctx->vdma.vdma, XAXIVDMA_READ);
    vdma_errs = vdma_status & XAXIVDMA_SR_ERR_ALL_MASK;
    cycles_missing = (status1 >> 2) & 0x3FFFU;
    physical_keys = status0 & 0x3FFU;

    if (ctx->last_physical_keys != physical_keys) {
        print_physical_keys(physical_keys);
        ctx->last_physical_keys = physical_keys;
    }

    if (ctx->rom_is_loading != 0U) {
        attempt_vdma_recover(ctx, vdma_status, vdma_errs);
        return;
    }

    if ((cycles_missing != 0U) || (err_latch != 0U) || (vdma_errs != 0U)) {
        u32 new_error = 0U;

        /* 新错误类型出现时立即打印 */
        if ((err_latch != ctx->warn_last_err_latch) ||
            (vdma_errs != ctx->warn_last_vdma_errs)) {
            new_error = 1U;
        }
        /* miss 首次非零时也立即打印 */
        if ((ctx->warn_print_count == 0U) && (cycles_missing != 0U)) {
            new_error = 1U;
        }

        if (new_error != 0U) {
            xil_printf("[WARN] miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x\r\n",
                       (unsigned int)cycles_missing,
                       (unsigned int)err_latch,
                       (unsigned int)vdma_status,
                       (unsigned int)vdma_errs);
            ctx->warn_print_count++;
            ctx->warn_suppress_ticks = 0U;
            /* 下次汇总间隔: 300 ticks (~3s) */
        } else {
            ctx->warn_suppress_ticks++;
            /* 指数退避汇总: 300, 3000, 30000 ticks ... */
            {
                u32 threshold = 300U;
                u32 i;
                for (i = 1U; i < ctx->warn_print_count && i < 4U; i++) {
                    threshold *= 10U;
                }
                if (ctx->warn_suppress_ticks >= threshold) {
                    xil_printf("[WARN] summary miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x (suppressed %u ticks)\r\n",
                               (unsigned int)cycles_missing,
                               (unsigned int)err_latch,
                               (unsigned int)vdma_status,
                               (unsigned int)vdma_errs,
                               (unsigned int)ctx->warn_suppress_ticks);
                    ctx->warn_print_count++;
                    ctx->warn_suppress_ticks = 0U;
                }
            }
        }

        ctx->warn_last_err_latch = err_latch;
        ctx->warn_last_vdma_errs = vdma_errs;
    } else {
        /* 恢复正常时重置所有节流状态 */
        if (ctx->warn_print_count != 0U) {
            xil_printf("[WARN] cleared (was miss=%u)\r\n",
                       (unsigned int)ctx->warn_print_count);
        }
        ctx->warn_print_count = 0U;
        ctx->warn_suppress_ticks = 0U;
        ctx->warn_last_err_latch = 0U;
        ctx->warn_last_vdma_errs = 0U;
    }

    if (irq_sts != 0U) {
        if ((irq_sts & IRQ_MASK_VSYNC) != 0U) {
            if (ctx->hdmi_test_enable == 0U) {
                present_captured_frame_if_ready(ctx);
            }
        }

        if ((irq_sts & ~IRQ_MASK_VSYNC) != 0U) {
            if (ctx->last_runtime_irq_sts != irq_sts) {
                xil_printf("[IRQ] sts=0x%x\r\n", (unsigned int)irq_sts);
                ctx->last_runtime_irq_sts = irq_sts;
            }
        } else {
            ctx->last_runtime_irq_sts = 0U;
        }
        PsGbaRegs_ClearIrqStatus(&ctx->regs, irq_sts);
    } else {
        ctx->last_runtime_irq_sts = 0U;
    }

    sync_display_frame_idx(ctx);
    maybe_print_stall_audit(ctx, status1, dbg_pc, dbg_mem, dbg_dma);
    attempt_vdma_recover(ctx, vdma_status, vdma_errs);

    /* 延迟 CHAIN 快照: 启动后 ~5s 和 ~15s 各打印一次，确认 ch1 是否在 BIOS 跳转后触发 */
    if (ctx->rom_loaded && ctx->delayed_chain_printed < 2U) {
        ctx->delayed_chain_tick++;
        if ((ctx->delayed_chain_printed == 0U && ctx->delayed_chain_tick >= 500U) ||
            (ctx->delayed_chain_printed == 1U && ctx->delayed_chain_tick >= 1500U)) {
            xil_printf("[AUTO] delayed chain snapshot @tick=%u\r\n",
                       (unsigned int)ctx->delayed_chain_tick);
            print_chain_snapshot(ctx, "delayed");
            print_config_readback(ctx, "delayed");
            ctx->delayed_chain_printed++;
        }
    }

    /* 帧缓冲异常扫描: 每 ~1s 扫描一次，最多自动 dump FBSCAN_MAX_DUMPS 次 */
    if (ctx->rom_loaded && ctx->delayed_chain_tick > 200U) {
        ctx->fbscan_tick++;
        if ((ctx->fbscan_tick % FBSCAN_INTERVAL_TICKS) == 0U) {
            if (ctx->fbscan_dump_count < FBSCAN_MAX_DUMPS) {
                scan_fb_for_anomaly(ctx, "auto", 0U);
            }
        }
    }
}

static void print_diag(ResearchAppCtx *ctx) {
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

    ctrl = PsGbaRegs_Read(&ctx->regs, GBA_REG_CTRL);
    keys = PsGbaRegs_Read(&ctx->regs, GBA_REG_KEYS);
    max_pak = PsGbaRegs_Read(&ctx->regs, GBA_REG_MAX_PAK_ADDR);
    cycle = PsGbaRegs_Read(&ctx->regs, GBA_REG_CYCLE_PRECALC);
    rtc = PsGbaRegs_Read(&ctx->regs, GBA_REG_RTC_TIMESTAMP);
    irq_en = PsGbaRegs_Read(&ctx->regs, GBA_REG_IRQ_EN);
    sw_reset = PsGbaRegs_Read(&ctx->regs, GBA_REG_SW_RESET);
    physical_keys = PsGbaRegs_Read(&ctx->regs, GBA_REG_STATUS0) & 0x3FFU;
    dbg_pc = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_mix = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_CPU_MIX);
    dbg_irq = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_IRQ);
    dbg_dma = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(&ctx->regs, GBA_REG_DEBUG_MEM);
    gpu_vcount = (dbg_irq >> 17) & 0xFFU;
    gpu_disp_low = (dbg_irq >> 25) & 0x7FU;

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
               (unsigned int)ctx->audio_sample_rate_hz,
               (unsigned int)ctx->audio_bits_per_sample,
               (unsigned int)ctx->audio_mute,
               (unsigned int)ctx->audio_volume);
    xil_printf("[DIAG] audio cue=%u\r\n",
               (unsigned int)ctx->audio_tone_enable);
    xil_printf("[DIAG] rom loaded=%u busy=%u bytes=%u aligned=%u maxpak=0x%08x\r\n",
               (unsigned int)ctx->rom_loaded,
               (unsigned int)(PsGbaRegs_Read(&ctx->regs, GBA_REG_ROM_STATUS) & 0x1U),
               (unsigned int)ctx->rom_size_bytes,
               (unsigned int)ctx->rom_size_aligned,
               (unsigned int)ctx->cfg_max_pak_addr);
    xil_printf("[DIAG] rom path=%s\r\n",
               ctx->rom_path[0] != '\0' ? ctx->rom_path : "(none)");

    xil_printf("[DIAG] hdmi wh=%ux%u bpp=%u line=%u frame_bytes=%u base=0x%08x\r\n",
               (unsigned int)ctx->vdma.width,
               (unsigned int)ctx->vdma.height,
               (unsigned int)ctx->vdma.bytes_per_pixel,
               (unsigned int)ctx->vdma.line_stride_bytes,
               (unsigned int)ctx->vdma.frame_size_bytes,
               (unsigned int)ctx->vdma.frame_addrs[0]);

    print_vdma_snapshot(ctx, "diag");
}

static void print_auto_boot_audit(ResearchAppCtx *ctx) {
    if ((ctx == NULL) || (ctx->auto_boot_audit_printed != 0U)) {
        return;
    }

    xil_printf("[AUTO] boot audit begin\r\n");
    print_config_readback(ctx, "boot");
    print_diag(ctx);
    print_chain_snapshot(ctx, "boot");
    print_probe(ctx);
    xil_printf("[AUTO] boot audit end\r\n");

    ctx->auto_boot_audit_printed = 1U;
}

static void maybe_print_stall_audit(ResearchAppCtx *ctx,
                                    u32 status1,
                                    u32 dbg_pc,
                                    u32 dbg_mem,
                                    u32 dbg_dma) {
    u32 frame_idx;
    u32 same_state;

    if ((ctx == NULL) || (ctx->rom_loaded == 0U) || (ctx->rom_is_loading != 0U)) {
        return;
    }

    frame_idx = status1 & 0x3U;
    same_state = ((ctx->stall_last_pc == dbg_pc) &&
                  (ctx->stall_last_mem == dbg_mem) &&
                  (ctx->stall_last_dma == dbg_dma) &&
                  (ctx->stall_last_frame == frame_idx)) ? 1U : 0U;

    if (same_state != 0U) {
        if (ctx->stall_same_sample_count < 0xFFFFFFFFU) {
            ctx->stall_same_sample_count++;
        }
    } else {
        ctx->stall_same_sample_count = 0U;
        ctx->auto_stall_audit_printed = 0U;
    }

    ctx->stall_last_pc = dbg_pc;
    ctx->stall_last_mem = dbg_mem;
    ctx->stall_last_dma = dbg_dma;
    ctx->stall_last_frame = frame_idx;

    if ((ctx->auto_stall_audit_printed == 0U) &&
        (dbg_mem == MEM_STATE_WAIT_SDRAM) &&
        (ctx->stall_same_sample_count >= AUTO_STALL_AUDIT_SAMPLES)) {
        xil_printf("[AUTO] stall audit begin samples=%u\r\n",
                   (unsigned int)ctx->stall_same_sample_count);
        print_config_readback(ctx, "stall");
        print_diag(ctx);
        print_chain_snapshot(ctx, "stall");
        print_probe(ctx);
        xil_printf("[AUTO] stall audit end\r\n");
        ctx->auto_stall_audit_printed = 1U;
    }
}

static void print_help(void) {
    xil_printf("Commands:\r\n");
    xil_printf("  help\r\n");
    xil_printf("  status\r\n");
    xil_printf("  diag\r\n");
    xil_printf("  probe\r\n");
    xil_printf("  chain\r\n");
    xil_printf("  audit\r\n");
    xil_printf("  fbscan\r\n");
    xil_printf("  pixcap summary [buf]\r\n");
    xil_printf("  pixcap row <y> [buf]\r\n");
    xil_printf("  pixcap cmp <x> <y> [buf] [frame]\r\n");
    xil_printf("  trace [samples] [interval_ms]\r\n");
    xil_printf("  core on|off\r\n");
    xil_printf("  turbo on|off\r\n");
    xil_printf("  lock on|off\r\n");
    xil_printf("  remap on|off\r\n");
    xil_printf("  key <name> on|off   (a/b/select/start/right/left/up/down/r/l)\r\n");
    xil_printf("  keymask <hex>\r\n");
    xil_printf("  rtc <hex>\r\n");
    xil_printf("  cycle <dec>\r\n");
    xil_printf("  maxpak <hex>\r\n");
    xil_printf("  commit\r\n");
    xil_printf("  irqen <hex>          (bit0=vsync, bit1=error)\r\n");
    xil_printf("  irqclr <hex>\r\n");
    xil_printf("  errclr\r\n");
    xil_printf("  reset <0|1>\r\n");
    xil_printf("  audio status\r\n");
    xil_printf("  audio reinit\r\n");
    xil_printf("  audio mute on|off\r\n");
    xil_printf("  audio tone on|off   (one-shot boot cue)\r\n");
    xil_printf("  audio vol <0-127>\r\n");
    xil_printf("  audio fmt <sample_rate_hz> <bits>\r\n");
    xil_printf("  rom status\r\n");
    xil_printf("  rom load [path]\r\n");
    xil_printf("  hdmi park <0|1|2>\r\n");
    xil_printf("  hdmi fill <idx|all> <hex>\r\n");
    xil_printf("  hdmi draw <idx>\r\n");
    xil_printf("  hdmi test on|off\r\n");
}

static void process_console_line(ResearchAppCtx *ctx, char *line) {
    char *cmd;
    char *a1;
    char *a2;
    char *a3;
    char *a4;
    char *a5;
    u32 v;

    cmd = strtok(line, " \t");
    if (cmd == NULL) {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        print_help();
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        print_status(ctx);
        return;
    }

    if (strcmp(cmd, "diag") == 0) {
        print_diag(ctx);
        return;
    }

    if (strcmp(cmd, "probe") == 0) {
        print_probe(ctx);
        return;
    }

    if (strcmp(cmd, "chain") == 0) {
        print_chain_snapshot(ctx, "cmd");
        return;
    }

    if (strcmp(cmd, "audit") == 0) {
        print_config_readback(ctx, "cmd");
        print_diag(ctx);
        print_chain_snapshot(ctx, "cmd");
        print_probe(ctx);
        return;
    }

    if (strcmp(cmd, "fbscan") == 0) {
        scan_fb_for_anomaly(ctx, "cmd", 1U);
        return;
    }

    if (strcmp(cmd, "pixcap") == 0) {
        u32 buf_idx;
        u32 frame_idx;

        a1 = strtok(NULL, " \t");
        if (a1 == NULL) {
            xil_printf("[CMD] usage: pixcap summary [buf] | row <y> [buf] | cmp <x> <y> [buf] [frame]\r\n");
            return;
        }

        if (strcmp(a1, "summary") == 0) {
            a2 = strtok(NULL, " \t");
            buf_idx = default_fbcap_buf_idx(ctx);
            if ((a2 != NULL) && (parse_u32_value(a2, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap summary [buf]\r\n");
                return;
            }
            print_pixcap_summary(ctx, buf_idx & 0x1U);
            return;
        }

        if (strcmp(a1, "row") == 0) {
            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            if ((a2 == NULL) || (parse_u32_value(a2, &v) != 0) || (v >= GBA_FRAME_HEIGHT)) {
                xil_printf("[CMD] usage: pixcap row <y> [buf]\r\n");
                return;
            }
            buf_idx = default_fbcap_buf_idx(ctx);
            if ((a3 != NULL) && (parse_u32_value(a3, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap row <y> [buf]\r\n");
                return;
            }
            print_pixcap_row(ctx, buf_idx & 0x1U, v);
            return;
        }

        if (strcmp(a1, "cmp") == 0) {
            u32 x;
            u32 y;

            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            a4 = strtok(NULL, " \t");
            a5 = strtok(NULL, " \t");
            if ((a2 == NULL) || (a3 == NULL) ||
                (parse_u32_value(a2, &x) != 0) ||
                (parse_u32_value(a3, &y) != 0) ||
                (x >= GBA_FRAME_WIDTH) || (y >= GBA_FRAME_HEIGHT)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return;
            }
            buf_idx = default_fbcap_buf_idx(ctx);
            if ((a4 != NULL) && (parse_u32_value(a4, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return;
            }
            frame_idx = default_hdmi_frame_idx(ctx);
            if ((a5 != NULL) && (parse_u32_value(a5, &frame_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return;
            }
            if ((ctx == NULL) || (ctx->vdma.is_ready == 0U) || (frame_idx >= ctx->vdma.frame_count)) {
                xil_printf("[CMD] pixcap cmp: invalid frame idx\r\n");
                return;
            }
            print_pixcap_compare(ctx, buf_idx & 0x1U, frame_idx, x, y);
            return;
        }

        xil_printf("[CMD] usage: pixcap summary [buf] | row <y> [buf] | cmp <x> <y> [buf] [frame]\r\n");
        return;
    }

    if (strcmp(cmd, "trace") == 0) {
        u32 samples;
        u32 interval_ms;

        a1 = strtok(NULL, " \t");
        a2 = strtok(NULL, " \t");

        samples = TRACE_DEFAULT_SAMPLES;
        interval_ms = TRACE_DEFAULT_INTERVAL_MS;

        if ((a1 != NULL) && (parse_u32_value(a1, &samples) != 0)) {
            xil_printf("[CMD] usage: trace [samples] [interval_ms]\r\n");
            return;
        }
        if ((a2 != NULL) && (parse_u32_value(a2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: trace [samples] [interval_ms]\r\n");
            return;
        }

        print_trace(ctx, samples, interval_ms);
        return;
    }

    if (strcmp(cmd, "commit") == 0) {
        apply_shadow_config(ctx);
        xil_printf("[CMD] config committed\r\n");
        return;
    }

    if (strcmp(cmd, "keymask") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (parse_u32_value(a1, &v) == 0)) {
            ctx->cfg_keys = v & 0x3FFU;
            apply_shadow_config(ctx);
            xil_printf("[CMD] keys=0x%03x\r\n", (unsigned int)ctx->cfg_keys);
            return;
        }
        xil_printf("[CMD] usage: keymask <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "key") == 0) {
        int bit;
        a1 = strtok(NULL, " \t");
        a2 = strtok(NULL, " \t");
        bit = key_bit_from_name(a1);
        if ((bit >= 0) && (parse_on_off(a2, &v) == 0)) {
            if (v != 0U) {
                ctx->cfg_keys |= (1UL << (u32)bit);
            } else {
                ctx->cfg_keys &= ~(1UL << (u32)bit);
            }
            apply_shadow_config(ctx);
            xil_printf("[CMD] key %s=%u keys=0x%03x\r\n",
                       a1,
                       (unsigned int)v,
                       (unsigned int)ctx->cfg_keys);
            return;
        }
        xil_printf("[CMD] usage: key <name> on|off\r\n");
        return;
    }

    if ((strcmp(cmd, "core") == 0) || (strcmp(cmd, "turbo") == 0) ||
        (strcmp(cmd, "lock") == 0) || (strcmp(cmd, "remap") == 0)) {
        u32 bit;
        a1 = strtok(NULL, " \t");
        if (parse_on_off(a1, &v) != 0) {
            xil_printf("[CMD] usage: %s on|off\r\n", cmd);
            return;
        }

        if (strcmp(cmd, "core") == 0) bit = 0U;
        else if (strcmp(cmd, "lock") == 0) bit = 1U;
        else if (strcmp(cmd, "turbo") == 0) bit = 2U;
        else bit = 5U;

        if (v != 0U) ctx->cfg_ctrl |= (1UL << bit);
        else ctx->cfg_ctrl &= ~(1UL << bit);
        apply_shadow_config(ctx);
        xil_printf("[CMD] %s=%u ctrl=0x%08x\r\n",
                   cmd,
                   (unsigned int)v,
                   (unsigned int)ctx->cfg_ctrl);
        return;
    }

    if (strcmp(cmd, "rtc") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (parse_u32_value(a1, &v) == 0)) {
            ctx->cfg_rtc_timestamp = v;
            apply_shadow_config(ctx);
            xil_printf("[CMD] rtc=0x%08x\r\n", (unsigned int)ctx->cfg_rtc_timestamp);
            return;
        }
        xil_printf("[CMD] usage: rtc <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "cycle") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (parse_u32_value(a1, &v) == 0)) {
            ctx->cfg_cycle_precalc = v & 0xFFFFU;
            apply_shadow_config(ctx);
            xil_printf("[CMD] cycle=%u\r\n", (unsigned int)ctx->cfg_cycle_precalc);
            return;
        }
        xil_printf("[CMD] usage: cycle <dec>\r\n");
        return;
    }

    if (strcmp(cmd, "maxpak") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (parse_u32_value(a1, &v) == 0)) {
            ctx->cfg_max_pak_addr = v & 0x1FFFFFFU;
            apply_shadow_config(ctx);
            xil_printf("[CMD] maxpak=0x%08x\r\n", (unsigned int)ctx->cfg_max_pak_addr);
            return;
        }
        xil_printf("[CMD] usage: maxpak <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "irqen") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (parse_u32_value(a1, &v) == 0)) {
            ctx->cfg_irq_enable = v & 0x3U;
            PsGbaRegs_SetIrqEnable(&ctx->regs, ctx->cfg_irq_enable);
            PsGbaRegs_ClearIrqStatus(&ctx->regs, IRQ_MASK_VSYNC | IRQ_MASK_ERROR);
            ctx->last_runtime_irq_sts = 0U;
            xil_printf("[CMD] irqen=0x%x\r\n", (unsigned int)ctx->cfg_irq_enable);
            return;
        }
        xil_printf("[CMD] usage: irqen <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "irqclr") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (parse_u32_value(a1, &v) == 0)) {
            PsGbaRegs_ClearIrqStatus(&ctx->regs, v & 0x3U);
            xil_printf("[CMD] irq status cleared: 0x%x\r\n", (unsigned int)(v & 0x3U));
            return;
        }
        xil_printf("[CMD] usage: irqclr <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "errclr") == 0) {
        Xil_Out32(XPAR_ZYNQ_GBA_TOP_0_BASEADDR + GBA_REG_ERROR_LATCH, 1U);
        xil_printf("[CMD] error latch clear requested\r\n");
        return;
    }

    if (strcmp(cmd, "reset") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (parse_u32_value(a1, &v) == 0)) {
            v &= 0x1U;
            Xil_Out32(XPAR_ZYNQ_GBA_TOP_0_BASEADDR + GBA_REG_SW_RESET, v);
            xil_printf("[CMD] sw_reset=%u\r\n", (unsigned int)v);
            return;
        }
        xil_printf("[CMD] usage: reset <0|1>\r\n");
        return;
    }

    if (strcmp(cmd, "audio") == 0) {
        a1 = strtok(NULL, " \t");

        if ((a1 != NULL) && (strcmp(a1, "status") == 0)) {
            xil_printf("[CMD] audio rate=%u bits=%u mute=%u vol=%u\r\n",
                       (unsigned int)ctx->audio_sample_rate_hz,
                       (unsigned int)ctx->audio_bits_per_sample,
                       (unsigned int)ctx->audio_mute,
                       (unsigned int)ctx->audio_volume);
            xil_printf("[CMD] audio cue=%u\r\n",
                       (unsigned int)ctx->audio_tone_enable);
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "reinit") == 0)) {
            if (program_audio_runtime(ctx) == XST_SUCCESS) {
                xil_printf("[CMD] audio reinit ok\r\n");
            } else {
                xil_printf("[CMD] audio reinit failed\r\n");
            }
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "mute") == 0)) {
            a2 = strtok(NULL, " \t");
            if (parse_on_off(a2, &v) == 0) {
                if (PsAudioCodec_SetMute(&ctx->codec, (u8)v) == XST_SUCCESS) {
                    ctx->audio_mute = (u8)v;
                    xil_printf("[CMD] audio mute=%u\r\n", (unsigned int)v);
                } else {
                    xil_printf("[CMD] audio mute failed\r\n");
                }
                return;
            }
            xil_printf("[CMD] usage: audio mute on|off\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "tone") == 0)) {
            a2 = strtok(NULL, " \t");
            if (parse_on_off(a2, &v) == 0) {
                ctx->audio_tone_enable = (u8)v;
                if (ctx->audio_tone_enable != 0U) {
                    ctx->cfg_ctrl |= GBA_CTRL_AUDIO_TONE;
                } else {
                    ctx->cfg_ctrl &= ~GBA_CTRL_AUDIO_TONE;
                }
                apply_shadow_config(ctx);
                xil_printf("[CMD] audio cue=%u\r\n", (unsigned int)ctx->audio_tone_enable);
                return;
            }
            xil_printf("[CMD] usage: audio tone on|off\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "vol") == 0)) {
            a2 = strtok(NULL, " \t");
            if ((a2 != NULL) && (parse_u32_value(a2, &v) == 0)) {
                if (PsAudioCodec_SetHeadphoneVolume(&ctx->codec, (u8)v) == XST_SUCCESS) {
                    ctx->audio_volume = (u8)(v & 0x7FU);
                    xil_printf("[CMD] audio vol=%u\r\n", (unsigned int)ctx->audio_volume);
                } else {
                    xil_printf("[CMD] audio vol failed\r\n");
                }
                return;
            }
            xil_printf("[CMD] usage: audio vol <0-127>\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "fmt") == 0)) {
            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            if ((a2 != NULL) && (a3 != NULL)) {
                u32 bits_u32;
                u16 sample_rate_reg;
                u16 word_length_bits;
                u16 bits;

                if ((parse_u32_value(a2, &v) != 0) || (parse_u32_value(a3, &bits_u32) != 0)) {
                    xil_printf("[CMD] usage: audio fmt <sample_rate_hz> <bits>\r\n");
                    return;
                }

                bits = (u16)bits_u32;
                if ((PsAudioCodec_ResolveSampleRateReg(v, &sample_rate_reg) == XST_SUCCESS) &&
                    (PsAudioCodec_ResolveWordLengthBits(bits, &word_length_bits) == XST_SUCCESS)) {
                    (void)sample_rate_reg;
                    (void)word_length_bits;
                    ctx->audio_sample_rate_hz = v;
                    ctx->audio_bits_per_sample = bits;
                    if (program_audio_runtime(ctx) == XST_SUCCESS) {
                        xil_printf("[CMD] audio fmt=%uHz/%u-bit\r\n",
                                   (unsigned int)ctx->audio_sample_rate_hz,
                                   (unsigned int)ctx->audio_bits_per_sample);
                    } else {
                        xil_printf("[CMD] audio fmt apply failed\r\n");
                    }
                } else {
                    xil_printf("[CMD] unsupported audio fmt\r\n");
                }
                return;
            }
            xil_printf("[CMD] usage: audio fmt <sample_rate_hz> <bits>\r\n");
            return;
        }
    }

    if (strcmp(cmd, "rom") == 0) {
        a1 = strtok(NULL, " \t");
        a2 = strtok(NULL, " \t");

        if ((a1 != NULL) && (strcmp(a1, "status") == 0)) {
            xil_printf("[CMD] rom loaded=%u busy=%u bytes=%u aligned=%u maxpak=0x%08x\r\n",
                       (unsigned int)ctx->rom_loaded,
                       (unsigned int)(PsGbaRegs_Read(&ctx->regs, GBA_REG_ROM_STATUS) & 0x1U),
                       (unsigned int)ctx->rom_size_bytes,
                       (unsigned int)ctx->rom_size_aligned,
                       (unsigned int)ctx->cfg_max_pak_addr);
            xil_printf("[CMD] rom path=%s\r\n",
                       ctx->rom_path[0] != '\0' ? ctx->rom_path : "(none)");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "load") == 0)) {
            if (load_rom_from_sd(ctx, a2) == XST_SUCCESS) {
                xil_printf("[CMD] rom load ok\r\n");
            } else {
                xil_printf("[CMD] rom load failed\r\n");
            }
            return;
        }

        xil_printf("[CMD] usage: rom status | rom load [path]\r\n");
        return;
    }

    if (strcmp(cmd, "hdmi") == 0) {
        a1 = strtok(NULL, " \t");

        if ((a1 != NULL) && (strcmp(a1, "park") == 0)) {
            a2 = strtok(NULL, " \t");
            if ((a2 != NULL) && (parse_u32_value(a2, &v) == 0)) {
                v %= ctx->vdma.frame_count;
                if (PsHdmiVdma_Park(&ctx->vdma, v) == XST_SUCCESS) {
                    sync_display_frame_idx(ctx);
                    xil_printf("[CMD] hdmi park=%u\r\n", (unsigned int)v);
                } else {
                    xil_printf("[CMD] hdmi park failed\r\n");
                }
                return;
            }
            xil_printf("[CMD] usage: hdmi park <0|1|2>\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "fill") == 0)) {
            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            if ((a2 != NULL) && (a3 != NULL) && (parse_u32_value(a3, &v) == 0)) {
                if (strcmp(a2, "all") == 0) {
                    if (PsHdmiVdma_FillAllFrames(&ctx->vdma, v) == XST_SUCCESS) {
                        xil_printf("[CMD] hdmi fill all color=0x%08x\r\n", (unsigned int)v);
                    } else {
                        xil_printf("[CMD] hdmi fill all failed\r\n");
                    }
                } else {
                    u32 idx;
                    if (parse_u32_value(a2, &idx) == 0) {
                        idx %= ctx->vdma.frame_count;
                        if (PsHdmiVdma_FillFrame(&ctx->vdma, idx, v) == XST_SUCCESS) {
                            xil_printf("[CMD] hdmi fill frame=%u color=0x%08x\r\n",
                                       (unsigned int)idx,
                                       (unsigned int)v);
                        } else {
                            xil_printf("[CMD] hdmi fill failed\r\n");
                        }
                    } else {
                        xil_printf("[CMD] usage: hdmi fill <idx|all> <hex>\r\n");
                    }
                }
                return;
            }
            xil_printf("[CMD] usage: hdmi fill <idx|all> <hex>\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "draw") == 0)) {
            a2 = strtok(NULL, " \t");
            if ((a2 != NULL) && (parse_u32_value(a2, &v) == 0)) {
                u32 idx;
                idx = v % ctx->vdma.frame_count;
                if (PsHdmiVdma_DrawTestPattern(&ctx->vdma, idx, ctx->hdmi_pattern_seed++) == XST_SUCCESS) {
                    (void)PsHdmiVdma_Park(&ctx->vdma, idx);
                    sync_display_frame_idx(ctx);
                    xil_printf("[CMD] hdmi draw frame=%u\r\n", (unsigned int)idx);
                } else {
                    xil_printf("[CMD] hdmi draw failed\r\n");
                }
                return;
            }
            xil_printf("[CMD] usage: hdmi draw <idx>\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "test") == 0)) {
            a2 = strtok(NULL, " \t");
            if (parse_on_off(a2, &v) == 0) {
                ctx->hdmi_test_enable = (u8)v;
                xil_printf("[CMD] hdmi test=%u\r\n", (unsigned int)ctx->hdmi_test_enable);
                return;
            }
            xil_printf("[CMD] usage: hdmi test on|off\r\n");
            return;
        }
    }

    if (strcmp(cmd, "vdma") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (strcmp(a1, "park") == 0)) {
            a2 = strtok(NULL, " \t");
            if ((a2 != NULL) && (parse_u32_value(a2, &v) == 0)) {
                v %= ctx->vdma.frame_count;
                if (PsHdmiVdma_Park(&ctx->vdma, v) == XST_SUCCESS) {
                    sync_display_frame_idx(ctx);
                    xil_printf("[CMD] vdma park=%u\r\n", (unsigned int)v);
                } else {
                    xil_printf("[CMD] vdma park failed\r\n");
                }
                return;
            }
        }
        xil_printf("[CMD] usage: vdma park <0|1|2>\r\n");
        return;
    }

    xil_printf("[CMD] unknown: %s\r\n", cmd);
}

static void console_task(void *arg) {
    ResearchAppCtx *ctx = (ResearchAppCtx *)arg;
    char line[192];
    u32 len;

    len = 0U;
    memset(line, 0, sizeof(line));

    xil_printf("[PS] Console ready, type 'help'\r\n> ");

    for (;;) {
        u8 ch;
        u32 n;

        n = XUartPs_Recv(&ctx->uart, &ch, 1U);
        if (n == 1U) {
            if ((ch == '\r') || (ch == '\n')) {
                xil_printf("\r\n");
                line[len] = '\0';
                process_console_line(ctx, line);
                len = 0U;
                memset(line, 0, sizeof(line));
                xil_printf("> ");
            } else if ((ch == 0x08U) || (ch == 0x7FU)) {
                if (len > 0U) {
                    len--;
                    line[len] = '\0';
                    xil_printf("\b \b");
                }
            } else if ((ch >= 32U) && (ch < 127U)) {
                if (len < (sizeof(line) - 1U)) {
                    line[len++] = (char)ch;
                    xil_printf("%c", ch);
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void monitor_task(void *arg) {
    ResearchAppCtx *ctx = (ResearchAppCtx *)arg;
    TickType_t delay_ticks;

    delay_ticks = pdMS_TO_TICKS(MONITOR_INTERVAL_MS);
    if (delay_ticks == 0U) {
        delay_ticks = 1U;
    }

    for (;;) {
        service_runtime_status(ctx);
        vTaskDelay(delay_ticks);
    }
}

static void validation_task(void *arg) {
    ResearchAppCtx *ctx = (ResearchAppCtx *)arg;

    for (;;) {
        if (ctx->hdmi_test_enable != 0U) {
            u32 frame_idx;

            frame_idx = ctx->hdmi_next_frame % ctx->vdma.frame_count;
            if (PsHdmiVdma_DrawTestPattern(&ctx->vdma, frame_idx, ctx->hdmi_pattern_seed) == XST_SUCCESS) {
                (void)PsHdmiVdma_Park(&ctx->vdma, frame_idx);
                sync_display_frame_idx(ctx);
                ctx->hdmi_next_frame = (u8)((frame_idx + 1U) % ctx->vdma.frame_count);
                ctx->hdmi_pattern_seed++;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(HDMI_TEST_INTERVAL_MS));
    }
}

static XStatus init_uart(ResearchAppCtx *ctx) {
    XUartPs_Config *cfg;
    XStatus status;

    cfg = XUartPs_LookupConfig(XPAR_XUARTPS_0_BASEADDR);
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XUartPs_CfgInitialize(&ctx->uart, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XUartPs_SetBaudRate(&ctx->uart, UART_BAUDRATE);
}

static XStatus init_system(ResearchAppCtx *ctx) {
    XStatus status;

    xil_printf("[INIT] 0: context cleared\r\n");

    xil_printf("[INIT] 1: gba regs bootstrap\r\n");
    PsGbaRegs_Init(&ctx->regs, XPAR_ZYNQ_GBA_TOP_0_BASEADDR);
    ctx->cfg_ctrl = (PsGbaRegs_Read(&ctx->regs, GBA_REG_CTRL) | GBA_CTRL_BOOT_REQUIRED) & ~GBA_CTRL_CORE_ON;
    ctx->cfg_keys = 0U;
    ctx->cfg_max_pak_addr = 0U;
    ctx->cfg_cycle_precalc = 100U;
    ctx->cfg_rtc_timestamp = 0U;
    ctx->cfg_irq_enable = IRQ_MASK_DEFAULT;

    ctx->audio_sample_rate_hz = 48000U;
    ctx->audio_bits_per_sample = 16U;
    ctx->audio_mute = 0U;
    ctx->audio_volume = 0x79U;
    ctx->audio_tone_enable = 1U;

    ctx->hdmi_test_enable = 0U;
    ctx->hdmi_next_frame = 0U;
    ctx->display_frame_idx = 0xFFU;
    ctx->hdmi_pattern_seed = 0U;
    ctx->fbcap_last_frame_seq = 0U;
    ctx->fbcap_last_buf_idx = 0xFFU;
    ctx->fbcap_last_frame_idx = 0xFFU;
    ctx->rom_loaded = 0U;
    ctx->rom_is_loading = 0U;
    ctx->rom_size_bytes = 0U;
    ctx->rom_size_aligned = 0U;
    ctx->last_physical_keys = 0U;
    ctx->stall_last_pc = 0U;
    ctx->stall_last_mem = 0U;
    ctx->stall_last_dma = 0U;
    ctx->stall_last_frame = 0U;
    ctx->stall_same_sample_count = 0U;
    ctx->rom_path[0] = '\0';
    ctx->auto_boot_audit_printed = 0U;
    ctx->auto_stall_audit_printed = 0U;

    apply_shadow_config(ctx);
    PsGbaRegs_SetIrqEnable(&ctx->regs, ctx->cfg_irq_enable);
    PsGbaRegs_ClearIrqStatus(&ctx->regs, IRQ_MASK_VSYNC | IRQ_MASK_ERROR);
    xil_printf("[INIT] 1: gba regs done\r\n");

    xil_printf("[INIT] 2: vdma init\r\n");
    status = PsHdmiVdma_Init(&ctx->vdma,
                             XPAR_XAXIVDMA_0_BASEADDR,
                             (UINTPTR)FB_REGION_BASE_ADDR,
                             FB_FRAME_STORE_BYTES,
                             HDMI_WIDTH,
                             HDMI_HEIGHT,
                             HDMI_BPP);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 2 failed: %d\r\n", status);
        return status;
    }

    XAxiVdma_IntrClear(&ctx->vdma.vdma, XAXIVDMA_IXR_ALL_MASK, XAXIVDMA_READ);
    XAxiVdma_IntrDisable(&ctx->vdma.vdma, XAXIVDMA_IXR_ALL_MASK, XAXIVDMA_READ);
    print_vdma_snapshot(ctx, "post-init");
    xil_printf("[INIT] 2: vdma done\r\n");

    xil_printf("[INIT] 3: audio codec i2c init\r\n");
    status = PsAudioCodec_Init(&ctx->codec,
                               XPAR_XIICPS_0_BASEADDR,
                               SSM2603_I2C_ADDR,
                               100000U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 3 warning: %d (audio disabled)\r\n", status);
        ctx->codec.is_ready = 0U;
    } else {
        xil_printf("[INIT] 3: audio i2c ready (defer program)\r\n");
    }

    xil_printf("[INIT] 5: render boot patterns\r\n");
    status = render_boot_patterns(ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 failed: %d\r\n", status);
        return status;
    }
    xil_printf("[INIT] 5: hdmi pattern done\r\n");
    xil_printf("[INIT] 5: trying park frame 0\r\n");
    status = PsHdmiVdma_Park(&ctx->vdma, 0U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 park failed: %d\r\n", status);
    } else {
        xil_printf("[INIT] 5 park ok\r\n");
    }
    sync_display_frame_idx(ctx);
    if (ctx->codec.is_ready != 0U) {
        xil_printf("[INIT] 7: audio codec program\r\n");
        status = program_audio_runtime(ctx);
        if (status != XST_SUCCESS) {
            xil_printf("[INIT] 7 warning: %d (audio disabled)\r\n", status);
            ctx->codec.is_ready = 0U;
        } else {
            xil_printf("[INIT] 7: audio done\r\n");
        }
    }

    xil_printf("[INIT] 8: boot audio cue armed=%u\r\n", (unsigned int)ctx->audio_tone_enable);
    xil_printf("[INIT] 8.1: input map btns=up/down/left/right sws=a/b/select/start\r\n");
    xil_printf("[INIT] 9: rom autoload\r\n");
    if (autoload_default_rom(ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 9 warning: ROM autoload skipped\r\n");
    }

    return XST_SUCCESS;
}

int main(void) {
    BaseType_t ok;
    XStatus status;

    Xil_DCacheEnable();
    Xil_ICacheEnable();

    memset(&g_app, 0, sizeof(g_app));
    status = init_uart(&g_app);
    if (status != XST_SUCCESS) {
        xil_printf("[PS] uart init failed: %d\r\n", status);
        return 1;
    }

    xil_printf("\r\n=== Zynq GBA PS Research Validation Runtime (Full) ===\r\n");

    status = init_system(&g_app);
    if (status != XST_SUCCESS) {
        xil_printf("[PS] init_system failed: %d\r\n", status);
        return 1;
    }

    usleep(AUTO_BOOT_AUDIT_DELAY_MS * 1000U);
    sync_display_frame_idx(&g_app);
    print_auto_boot_audit(&g_app);

    xil_printf("[PS] init done: VDMA+Audio+Regs+UART ready\r\n");
    xil_printf("[PS] boot selftest done: HDMI patterns loaded, audio playback configured\r\n");

    ok = xTaskCreate(monitor_task,
                     "mon",
                     SYS_TASK_STACK_WORDS,
                     &g_app,
                     SYS_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create monitor task\r\n");
        return 1;
    }

    ok = xTaskCreate(console_task,
                     "cons",
                     CONSOLE_TASK_STACK_WORDS,
                     &g_app,
                     CONSOLE_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create console task\r\n");
        return 1;
    }

    ok = xTaskCreate(validation_task,
                     "valid",
                     VALID_TASK_STACK_WORDS,
                     &g_app,
                     VALID_TASK_PRIORITY,
                     NULL);
    if (ok != pdPASS) {
        xil_printf("[PS] failed to create validation task\r\n");
        return 1;
    }

    vTaskStartScheduler();

    for (;;) {
        usleep(1000000U);
    }
}
