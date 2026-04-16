#include "App/Inc/ps_app_console.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xiltimer.h"
#include "xparameters.h"

#include "Diagnostics/Inc/ps_app_diag.h"
#include "App/Inc/ps_app_runtime.h"
#include "Input/Inc/ps_app_input.h"
#include "UsbHost/Inc/ps_app_usbhost.h"
#include "Video/Inc/ps_app_video.h"

static int PsAppConsole_ParseOnOff(const char *s, u32 *value_out) {
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
static int PsAppConsole_ParseU32Value(const char *s, u32 *value_out) {
    char *end_ptr;
    unsigned long value;

    if ((s == NULL) || (value_out == NULL)) {
        return -1;
    }

    value = strtoul(s, &end_ptr, 0);
    if ((end_ptr == s) || ((end_ptr != NULL) && (*end_ptr != '\0'))) {
        return -1;
    }

    *value_out = (u32)value;
    return 0;
}
static int PsAppConsole_KeyBitFromName(const char *name) {
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

static const char *PsAppConsole_BiosModeName(u8 bios_mode) {
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

static u32 PsAppConsole_TicksToMs(u64 ticks) {
#if defined(COUNTS_PER_SECOND) && (COUNTS_PER_SECOND != 0)
    u64 scaled = (ticks * 1000ULL) + ((u64)COUNTS_PER_SECOND / 2ULL);
    return (u32)(scaled / (u64)COUNTS_PER_SECOND);
#else
    return (u32)ticks;
#endif
}

static u32 PsAppConsole_DeltaMasked(u32 start, u32 end, u32 mask) {
    return (end - start) & mask;
}

static u32 PsAppConsole_ReadDisplayFrameIdx(PsAppRuntimeContext *app) {
    u32 frame_idx;

    if ((app == NULL) ||
        (app->vdma == NULL) ||
        (app->vdma->is_ready == 0U) ||
        (app->vdma->frame_count == 0U)) {
        return 0U;
    }

    frame_idx = XAxiVdma_CurrFrameStore(&app->vdma->vdma, XAXIVDMA_READ);
    if (frame_idx >= app->vdma->frame_count) {
        frame_idx = 0U;
    }
    return frame_idx;
}

static void PsAppConsole_RunFpsTest(PsAppConsoleContext *ctx, u32 duration_ms, u32 sample_ms) {
    PsAppRuntimeContext *app;
    u32 start_status1;
    u32 end_status1;
    u32 start_miss;
    u32 end_miss;
    u32 start_cap_seq;
    u32 end_cap_seq;
    u32 delta_cap_seq;
    u32 start_blit_count;
    u32 end_blit_count;
    u32 delta_blit_count;
    u32 start_blit_total_us;
    u32 end_blit_total_us;
    u32 delta_blit_total_us;
    u32 start_display_frame;
    u32 last_display_frame;
    u32 end_display_frame;
    u32 delta_display_flips;
    u32 elapsed_ms;
    u32 sample_ticks;
    u32 effective_sample_ms;
    u32 display_fps_x100;
    u32 cap_fps_x100;
    u32 blit_fps_x100;
    u32 present_ratio_x10;
    u32 window_blit_avg_us;
    XTime tick_begin;
    XTime tick_now;

    if ((ctx == NULL) || (ctx->runtime == NULL)) {
        return;
    }

    app = ctx->runtime;
    if (duration_ms == 0U) {
        duration_ms = 5000U;
    }
    if (sample_ms == 0U) {
        sample_ms = (u32)portTICK_PERIOD_MS;
    }
    if (sample_ms > duration_ms) {
        sample_ms = duration_ms;
    }
    if ((app->vdma == NULL) || (app->vdma->is_ready == 0U) || (app->vdma->frame_count == 0U)) {
        xil_printf("[FPS] hdmi vdma not ready\r\n");
        return;
    }

    start_status1 = PsGbaRegs_Read(app->regs, GBA_REG_STATUS1);
    start_miss = (start_status1 >> 2) & 0x3FFFU;
    start_cap_seq = PsGbaRegs_Read(app->regs, GBA_REG_FB_CAP_SEQ);
    start_blit_count = app->video->blit_count;
    start_blit_total_us = app->video->blit_total_us;
    start_display_frame = PsAppConsole_ReadDisplayFrameIdx(app);
    last_display_frame = start_display_frame;
    delta_display_flips = 0U;
    XTime_GetTime(&tick_begin);

    sample_ticks = pdMS_TO_TICKS(sample_ms);
    if (sample_ticks == 0U) {
        sample_ticks = 1U;
    }
    effective_sample_ms = sample_ticks * (u32)portTICK_PERIOD_MS;
    if (effective_sample_ms == 0U) {
        effective_sample_ms = sample_ms;
    }

    xil_printf("[FPS] test start mode=display window=%u ms sample_req=%u ms sample_eff=%u ms\r\n",
               (unsigned int)duration_ms,
               (unsigned int)sample_ms,
               (unsigned int)effective_sample_ms);

    do {
        u32 current_display_frame;

        vTaskDelay(sample_ticks);
        current_display_frame = PsAppConsole_ReadDisplayFrameIdx(app);
        if (current_display_frame != last_display_frame) {
            delta_display_flips++;
            last_display_frame = current_display_frame;
        }
        XTime_GetTime(&tick_now);
        elapsed_ms = PsAppConsole_TicksToMs((u64)(tick_now - tick_begin));
    } while (elapsed_ms < duration_ms);

    end_status1 = PsGbaRegs_Read(app->regs, GBA_REG_STATUS1);
    end_miss = (end_status1 >> 2) & 0x3FFFU;
    end_cap_seq = PsGbaRegs_Read(app->regs, GBA_REG_FB_CAP_SEQ);
    end_blit_count = app->video->blit_count;
    end_blit_total_us = app->video->blit_total_us;
    end_display_frame = last_display_frame;
    if (elapsed_ms == 0U) {
        elapsed_ms = 1U;
    }

    delta_cap_seq = end_cap_seq - start_cap_seq;
    delta_blit_count = end_blit_count - start_blit_count;
    delta_blit_total_us = end_blit_total_us - start_blit_total_us;

    display_fps_x100 = (u32)(((u64)delta_display_flips * 100000ULL + (u64)(elapsed_ms / 2U)) / (u64)elapsed_ms);
    cap_fps_x100 = (u32)(((u64)delta_cap_seq * 100000ULL + (u64)(elapsed_ms / 2U)) / (u64)elapsed_ms);
    blit_fps_x100 = (u32)(((u64)delta_blit_count * 100000ULL + (u64)(elapsed_ms / 2U)) / (u64)elapsed_ms);
    present_ratio_x10 = (delta_cap_seq != 0U) ?
                        (u32)(((u64)delta_blit_count * 1000ULL + (u64)(delta_cap_seq / 2U)) / (u64)delta_cap_seq) :
                        0U;
    window_blit_avg_us = (delta_blit_count != 0U) ? (delta_blit_total_us / delta_blit_count) : 0U;

    xil_printf("[FPS] elapsed=%u ms display_flip_delta=%u frame_begin=%u frame_end=%u cap_delta=%u blit_delta=%u miss_delta=%u\r\n",
               (unsigned int)elapsed_ms,
               (unsigned int)delta_display_flips,
               (unsigned int)start_display_frame,
               (unsigned int)end_display_frame,
               (unsigned int)delta_cap_seq,
               (unsigned int)delta_blit_count,
               (unsigned int)PsAppConsole_DeltaMasked(start_miss, end_miss, 0x3FFFU));
    xil_printf("[FPS] hdmi_fps=%u.%02u cap_fps=%u.%02u blit_fps=%u.%02u present=%u.%1u%% blit_avg_window_us=%u seq_gap_max=%u\r\n",
               (unsigned int)(display_fps_x100 / 100U),
               (unsigned int)(display_fps_x100 % 100U),
               (unsigned int)(cap_fps_x100 / 100U),
               (unsigned int)(cap_fps_x100 % 100U),
               (unsigned int)(blit_fps_x100 / 100U),
               (unsigned int)(blit_fps_x100 % 100U),
               (unsigned int)(present_ratio_x10 / 10U),
               (unsigned int)(present_ratio_x10 % 10U),
               (unsigned int)window_blit_avg_us,
               (unsigned int)app->video->blit_seq_gap_max);
}

static int PsAppConsole_HexNibble(char ch) {
    if ((ch >= '0') && (ch <= '9')) {
        return (int)(ch - '0');
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        return 10 + (int)(ch - 'a');
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        return 10 + (int)(ch - 'A');
    }
    return -1;
}

static int PsAppConsole_ParseHexBytes(const char *text,
                                      u8 *out_buf,
                                      u32 out_buf_size,
                                      u32 *out_len) {
    int high_nibble;
    int nibble;
    u32 count;
    char ch;

    if ((text == NULL) || (out_buf == NULL) || (out_len == NULL)) {
        return -1;
    }

    high_nibble = -1;
    count = 0U;
    while ((ch = *text++) != '\0') {
        if ((ch == ' ') || (ch == ':') || (ch == '-') || (ch == '_')) {
            continue;
        }

        nibble = PsAppConsole_HexNibble(ch);
        if (nibble < 0) {
            return -1;
        }

        if (high_nibble < 0) {
            high_nibble = nibble;
        } else {
            if (count >= out_buf_size) {
                return -1;
            }
            out_buf[count++] = (u8)(((u8)high_nibble << 4) | (u8)nibble);
            high_nibble = -1;
        }
    }

    if (high_nibble >= 0) {
        return -1;
    }
    if (count == 0U) {
        return -1;
    }

    *out_len = count;
    return 0;
}

static void PsAppConsole_PrintHelp(void) {
    xil_printf("Commands:\r\n");
    xil_printf("  help\r\n");
    xil_printf("  status\r\n");
    xil_printf("  diag\r\n");
    xil_printf("  probe\r\n");
    xil_printf("  chain\r\n");
    xil_printf("  audit\r\n");
    xil_printf("  fbscan\r\n");
    xil_printf("  log input on|off\r\n");
    xil_printf("  log fbscan on|off\r\n");
    xil_printf("  log status\r\n");
    xil_printf("  pixcap summary [buf]\r\n");
    xil_printf("  pixcap row <y> [buf]\r\n");
    xil_printf("  pixcap cmp <x> <y> [buf] [frame]\r\n");
    xil_printf("  trace [samples] [interval_ms]\r\n");
    xil_printf("  fps [seconds] [sample_ms]\r\n");
    xil_printf("  core on|off\r\n");
    xil_printf("  turbo on|off\r\n");
    xil_printf("  lock on|off\r\n");
    xil_printf("  remap on|off\r\n");
    xil_printf("  unsafe on|off\r\n");
    xil_printf("  key <name> on|off   (a/b/select/start/right/left/up/down/r/l)\r\n");
    xil_printf("  keymask <hex>\r\n");
    xil_printf("  btn status           (PS-side BTN4/BTN5 on MIO50/51)\r\n");
    xil_printf("  input status\r\n");
    xil_printf("  input inject <hex>   (xinput report hex, allow separators : - _ space)\r\n");
    xil_printf("  input detach\r\n");
    xil_printf("  usb status\r\n");
    xil_printf("  usb kick\r\n");
    xil_printf("  usb portreset\r\n");
    xil_printf("  usb ulpi\r\n");
    xil_printf("  usb vbus on|off\r\n");
    xil_printf("  usb rumble <large 0-255> <small 0-255>\r\n");
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
    xil_printf("  audio vol <0-100>\r\n");
    xil_printf("  audio fmt <sample_rate_hz> <bits>\r\n");
    xil_printf("  rom status\r\n");
    xil_printf("  rom probe\r\n");
    xil_printf("  rom load [path]\r\n");
    xil_printf("  hdmi park <0|1|2>\r\n");
    xil_printf("  hdmi fill <idx|all> <hex>\r\n");
}

static void PsAppConsole_ProcessLine(PsAppConsoleContext *ctx, char *line) {
    char *cmd;
    char *a1;
    char *a2;
    char *a3;
    char *a4;
    char *a5;
    PsAppRuntimeContext *app;
    PsAppVideoContext *video;
    PsAppDiagContext *diag;
    u32 v;

    if ((ctx == NULL) || (line == NULL)) {
        return;
    }

    app = ctx->runtime;
    video = ctx->video;
    diag = ctx->diag;

    cmd = strtok(line, " \t");
    if (cmd == NULL) {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        PsAppConsole_PrintHelp();
        return;
    }
    if (strcmp(cmd, "status") == 0) {
        PsAppDiag_PrintStatus(diag);
        return;
    }
    if (strcmp(cmd, "diag") == 0) {
        PsAppDiag_PrintDiag(diag);
        return;
    }
    if (strcmp(cmd, "probe") == 0) {
        PsAppDiag_PrintProbe(diag);
        return;
    }
    if (strcmp(cmd, "chain") == 0) {
        PsAppDiag_PrintChainSnapshot(diag, "cmd");
        return;
    }
    if (strcmp(cmd, "audit") == 0) {
        PsAppDiag_PrintConfigReadback(diag, "cmd");
        PsAppDiag_PrintDiag(diag);
        PsAppDiag_PrintChainSnapshot(diag, "cmd");
        PsAppDiag_PrintProbe(diag);
        return;
    }
    if (strcmp(cmd, "fbscan") == 0) {
        PsAppDiag_ScanFramebufferForAnomaly(diag, "cmd", 1U);
        return;
    }
    if (strcmp(cmd, "log") == 0) {
        a1 = strtok(NULL, " \t");
        a2 = strtok(NULL, " \t");

        if ((a1 == NULL) || (strcmp(a1, "status") == 0)) {
            xil_printf("[CMD] log input=%s fbscan=%s\r\n",
                       (app->diag->log_input_delta_enable != 0U) ? "on" : "off",
                       (app->diag->log_fbscan_auto_enable != 0U) ? "on" : "off");
            return;
        }

        if (strcmp(a1, "input") == 0) {
            if (PsAppConsole_ParseOnOff(a2, &v) != 0) {
                xil_printf("[CMD] usage: log input on|off\r\n");
                return;
            }
            app->diag->log_input_delta_enable = (u8)(v & 0x1U);
            xil_printf("[CMD] log input=%s\r\n",
                       (app->diag->log_input_delta_enable != 0U) ? "on" : "off");
            return;
        }

        if (strcmp(a1, "fbscan") == 0) {
            if (PsAppConsole_ParseOnOff(a2, &v) != 0) {
                xil_printf("[CMD] usage: log fbscan on|off\r\n");
                return;
            }
            app->diag->log_fbscan_auto_enable = (u8)(v & 0x1U);
            if (app->diag->log_fbscan_auto_enable == 0U) {
                app->diag->fbscan_tick = 0U;
            }
            xil_printf("[CMD] log fbscan=%s\r\n",
                       (app->diag->log_fbscan_auto_enable != 0U) ? "on" : "off");
            return;
        }

        xil_printf("[CMD] usage: log input on|off | log fbscan on|off | log status\r\n");
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
            buf_idx = PsAppVideo_DefaultCaptureBuffer(video);
            if ((a2 != NULL) && (PsAppConsole_ParseU32Value(a2, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap summary [buf]\r\n");
                return;
            }
            PsAppDiag_PrintPixcapSummary(diag, buf_idx & 0x1U);
            return;
        }

        if (strcmp(a1, "row") == 0) {
            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            if ((a2 == NULL) || (PsAppConsole_ParseU32Value(a2, &v) != 0) ||
                (v >= PS_APP_GBA_FRAME_HEIGHT)) {
                xil_printf("[CMD] usage: pixcap row <y> [buf]\r\n");
                return;
            }
            buf_idx = PsAppVideo_DefaultCaptureBuffer(video);
            if ((a3 != NULL) && (PsAppConsole_ParseU32Value(a3, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap row <y> [buf]\r\n");
                return;
            }
            PsAppDiag_PrintPixcapRow(diag, buf_idx & 0x1U, v);
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
                (PsAppConsole_ParseU32Value(a2, &x) != 0) ||
                (PsAppConsole_ParseU32Value(a3, &y) != 0) ||
                (x >= PS_APP_GBA_FRAME_WIDTH) || (y >= PS_APP_GBA_FRAME_HEIGHT)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return;
            }
            buf_idx = PsAppVideo_DefaultCaptureBuffer(video);
            if ((a4 != NULL) && (PsAppConsole_ParseU32Value(a4, &buf_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return;
            }
            frame_idx = PsAppVideo_DefaultFrameIndex(video);
            if ((a5 != NULL) && (PsAppConsole_ParseU32Value(a5, &frame_idx) != 0)) {
                xil_printf("[CMD] usage: pixcap cmp <x> <y> [buf] [frame]\r\n");
                return;
            }
            if ((app->vdma->is_ready == 0U) || (frame_idx >= app->vdma->frame_count)) {
                xil_printf("[CMD] pixcap cmp: invalid frame idx\r\n");
                return;
            }
            PsAppDiag_PrintPixcapCompare(diag, buf_idx & 0x1U, frame_idx, x, y);
            return;
        }

        xil_printf("[CMD] usage: pixcap summary [buf] | row <y> [buf] | cmp <x> <y> [buf] [frame]\r\n");
        return;
    }

    if (strcmp(cmd, "trace") == 0) {
        u32 samples = PS_APP_TRACE_DEFAULT_SAMPLES;
        u32 interval_ms = PS_APP_TRACE_DEFAULT_INTERVAL_MS;

        a1 = strtok(NULL, " \t");
        a2 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &samples) != 0)) {
            xil_printf("[CMD] usage: trace [samples] [interval_ms]\r\n");
            return;
        }
        if ((a2 != NULL) && (PsAppConsole_ParseU32Value(a2, &interval_ms) != 0)) {
            xil_printf("[CMD] usage: trace [samples] [interval_ms]\r\n");
            return;
        }
        PsAppDiag_PrintTrace(diag, samples, interval_ms);
        return;
    }

    if (strcmp(cmd, "fps") == 0) {
        u32 seconds = 5U;
        u32 sample_ms = (u32)portTICK_PERIOD_MS;

        a1 = strtok(NULL, " \t");
        a2 = strtok(NULL, " \t");
        if ((a1 != NULL) && (strcmp(a1, "test") == 0)) {
            a1 = a2;
            a2 = strtok(NULL, " \t");
        }

        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &seconds) != 0)) {
            xil_printf("[CMD] usage: fps [seconds] [sample_ms]\r\n");
            return;
        }
        if ((a2 != NULL) && (PsAppConsole_ParseU32Value(a2, &sample_ms) != 0)) {
            xil_printf("[CMD] usage: fps [seconds] [sample_ms]\r\n");
            return;
        }
        if ((seconds == 0U) || (seconds > 120U) || (sample_ms == 0U) || (sample_ms > 5000U)) {
            xil_printf("[CMD] range: seconds=1..120 sample_ms=1..5000\r\n");
            return;
        }

        PsAppConsole_RunFpsTest(ctx, seconds * 1000U, sample_ms);
        return;
    }

    if (strcmp(cmd, "commit") == 0) {
        PsAppRuntime_ApplyShadowConfig(app);
        xil_printf("[CMD] config committed\r\n");
        return;
    }

    if (strcmp(cmd, "keymask") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &v) == 0)) {
            app->config->keys = v & 0x3FFU;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] keys=0x%03x\r\n", (unsigned int)app->config->keys);
            return;
        }
        xil_printf("[CMD] usage: keymask <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "key") == 0) {
        int bit;
        a1 = strtok(NULL, " \t");
        a2 = strtok(NULL, " \t");
        bit = PsAppConsole_KeyBitFromName(a1);
        if ((bit >= 0) && (PsAppConsole_ParseOnOff(a2, &v) == 0)) {
            if (v != 0U) app->config->keys |= (1UL << (u32)bit);
            else app->config->keys &= ~(1UL << (u32)bit);
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] key %s=%u keys=0x%03x\r\n",
                       a1,
                       (unsigned int)v,
                       (unsigned int)app->config->keys);
            return;
        }
        xil_printf("[CMD] usage: key <name> on|off\r\n");
        return;
    }

    if ((strcmp(cmd, "btn") == 0) || (strcmp(cmd, "button") == 0)) {
        u32 ps_btn_mask;
        u32 btn4_raw;
        u32 btn5_raw;
        u32 bank1_data;
        u32 bank1_dir;
        u32 bank1_outen;
        u32 mio50_cfg;
        u32 mio51_cfg;
        a1 = strtok(NULL, " \t");

        if ((a1 == NULL) || (strcmp(a1, "status") == 0)) {
            ps_btn_mask = PsAppRuntime_ReadPsButtonMask(app);
            PsAppRuntime_ReadPsButtonRawLevels(app,
                                               &btn4_raw,
                                               &btn5_raw,
                                               &bank1_data,
                                               &bank1_dir,
                                               &bank1_outen,
                                               &mio50_cfg,
                                               &mio51_cfg);
            xil_printf("[CMD] ps_gpio_ready=%u BTN4=%u BTN5=%u mask=0x%02x raw50=%u raw51=%u bank1=0x%08x dir1=0x%08x outen1=0x%08x mio50=0x%08x mio51=0x%08x\r\n",
                       (unsigned int)app->ps_gpio_ready,
                       (unsigned int)((ps_btn_mask & PS_APP_BTN4_MASK) != 0U),
                       (unsigned int)((ps_btn_mask & PS_APP_BTN5_MASK) != 0U),
                       (unsigned int)(ps_btn_mask & (PS_APP_BTN4_MASK | PS_APP_BTN5_MASK)),
                       (unsigned int)btn4_raw,
                       (unsigned int)btn5_raw,
                       (unsigned int)bank1_data,
                       (unsigned int)bank1_dir,
                       (unsigned int)bank1_outen,
                       (unsigned int)mio50_cfg,
                       (unsigned int)mio51_cfg);
            return;
        }

        xil_printf("[CMD] usage: btn status\r\n");
        return;
    }

    if (strcmp(cmd, "input") == 0) {
        u8 report[PS_XINPUT_MAX_REPORT_BYTES];
        u32 report_len;

        a1 = strtok(NULL, " \t");
        if ((a1 == NULL) || (strcmp(a1, "status") == 0)) {
            PsAppInput_PrintStatus(app->input_ctx);
            return;
        }

        if (strcmp(a1, "detach") == 0) {
            PsAppInput_OnUsbDetached(app->input_ctx);
            xil_printf("[CMD] input detached\r\n");
            return;
        }

        if (strcmp(a1, "inject") == 0) {
            a2 = strtok(NULL, "\r\n");
            if ((a2 == NULL) ||
                (PsAppConsole_ParseHexBytes(a2,
                                            report,
                                            sizeof(report),
                                            &report_len) != 0)) {
                xil_printf("[CMD] usage: input inject <hex>\r\n");
                xil_printf("[CMD] eg: input inject 0014000000000000000000000000000000000000\r\n");
                return;
            }

            if (PsAppInput_InjectRawReport(app->input_ctx, report, report_len) == XST_SUCCESS) {
                xil_printf("[CMD] input inject ok, bytes=%u\r\n", (unsigned int)report_len);
            } else {
                xil_printf("[CMD] input inject failed\r\n");
            }
            return;
        }

        xil_printf("[CMD] usage: input status|inject <hex>|detach\r\n");
        return;
    }

    if (strcmp(cmd, "usb") == 0) {
        u32 on_off;

        a1 = strtok(NULL, " \t");

        if ((a1 == NULL) || (strcmp(a1, "status") == 0)) {
            PsAppUsbHost_PrintStatus(app->usb_host_ctx);
            return;
        }

        if (strcmp(a1, "kick") == 0) {
            PsAppUsbHost_ForceRootHubScan(app->usb_host_ctx);
            xil_printf("[CMD] usb kick requested\r\n");
            return;
        }

        if (strcmp(a1, "portreset") == 0) {
            if (PsAppUsbHost_PortReset(app->usb_host_ctx) == XST_SUCCESS) {
                xil_printf("[CMD] usb portreset ok\r\n");
            } else {
                xil_printf("[CMD] usb portreset failed\r\n");
            }
            return;
        }

        if (strcmp(a1, "ulpi") == 0) {
            if (PsAppUsbHost_DumpUlpi(app->usb_host_ctx) == XST_SUCCESS) {
                xil_printf("[CMD] usb ulpi ok\r\n");
            } else {
                xil_printf("[CMD] usb ulpi failed\r\n");
            }
            return;
        }

        if (strcmp(a1, "vbus") == 0) {
            a2 = strtok(NULL, " \t");
            if (PsAppConsole_ParseOnOff(a2, &on_off) != 0) {
                xil_printf("[CMD] usage: usb vbus on|off\r\n");
                return;
            }
            if (PsAppUsbHost_SetVbusDrive(app->usb_host_ctx, (u8)on_off) == XST_SUCCESS) {
                xil_printf("[CMD] usb vbus=%u\r\n", (unsigned int)on_off);
            } else {
                xil_printf("[CMD] usb vbus set failed\r\n");
            }
            return;
        }

        if (strcmp(a1, "rumble") == 0) {
            u32 large_motor;
            u32 small_motor;

            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            if ((a2 == NULL) || (a3 == NULL) ||
                (PsAppConsole_ParseU32Value(a2, &large_motor) != 0) ||
                (PsAppConsole_ParseU32Value(a3, &small_motor) != 0) ||
                (large_motor > 255U) || (small_motor > 255U)) {
                xil_printf("[CMD] usage: usb rumble <large 0-255> <small 0-255>\r\n");
                return;
            }

            if (PsAppUsbHost_SetRumble(app->usb_host_ctx, (u8)large_motor, (u8)small_motor) == XST_SUCCESS) {
                xil_printf("[CMD] usb rumble ok L=%u S=%u\r\n",
                           (unsigned int)large_motor,
                           (unsigned int)small_motor);
            } else {
                xil_printf("[CMD] usb rumble failed\r\n");
            }
            return;
        }

        xil_printf("[CMD] usage: usb status|kick|portreset|ulpi|vbus on|off|rumble <large 0-255> <small 0-255>\r\n");
        return;
    }

    if ((strcmp(cmd, "core") == 0) || (strcmp(cmd, "turbo") == 0) ||
        (strcmp(cmd, "lock") == 0) || (strcmp(cmd, "remap") == 0) ||
        (strcmp(cmd, "unsafe") == 0)) {
        u32 bit;
        a1 = strtok(NULL, " \t");
        if (PsAppConsole_ParseOnOff(a1, &v) != 0) {
            xil_printf("[CMD] usage: %s on|off\r\n", cmd);
            return;
        }

        if (strcmp(cmd, "core") == 0) bit = 0U;
        else if (strcmp(cmd, "lock") == 0) bit = 1U;
        else if (strcmp(cmd, "turbo") == 0) bit = 2U;
        else if (strcmp(cmd, "remap") == 0) bit = 5U;
        else bit = 13U;

        if (v != 0U) app->config->ctrl |= (1UL << bit);
        else app->config->ctrl &= ~(1UL << bit);
        PsAppRuntime_ApplyShadowConfig(app);
        xil_printf("[CMD] %s=%u ctrl=0x%08x\r\n",
                   cmd,
                   (unsigned int)v,
                   (unsigned int)app->config->ctrl);
        return;
    }

    if (strcmp(cmd, "rtc") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &v) == 0)) {
            app->config->rtc_timestamp = v;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] rtc=0x%08x\r\n", (unsigned int)app->config->rtc_timestamp);
            return;
        }
        xil_printf("[CMD] usage: rtc <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "cycle") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &v) == 0)) {
            app->config->cycle_precalc = v & 0xFFFFU;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] cycle=%u\r\n", (unsigned int)app->config->cycle_precalc);
            return;
        }
        xil_printf("[CMD] usage: cycle <dec>\r\n");
        return;
    }

    if (strcmp(cmd, "maxpak") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &v) == 0)) {
            app->config->max_pak_addr = v & 0x1FFFFFFU;
            PsAppRuntime_ApplyShadowConfig(app);
            xil_printf("[CMD] maxpak=0x%08x\r\n", (unsigned int)app->config->max_pak_addr);
            return;
        }
        xil_printf("[CMD] usage: maxpak <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "irqen") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &v) == 0)) {
            app->config->irq_enable = v & 0x3U;
            PsGbaRegs_SetIrqEnable(app->regs, app->config->irq_enable);
            PsGbaRegs_ClearIrqStatus(app->regs, PS_APP_IRQ_MASK_VSYNC | PS_APP_IRQ_MASK_ERROR);
            app->diag->last_runtime_irq_sts = 0U;
            xil_printf("[CMD] irqen=0x%x\r\n", (unsigned int)app->config->irq_enable);
            return;
        }
        xil_printf("[CMD] usage: irqen <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "irqclr") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &v) == 0)) {
            PsGbaRegs_ClearIrqStatus(app->regs, v & 0x3U);
            xil_printf("[CMD] irq status cleared: 0x%x\r\n", (unsigned int)(v & 0x3U));
            return;
        }
        xil_printf("[CMD] usage: irqclr <hex>\r\n");
        return;
    }

    if (strcmp(cmd, "errclr") == 0) {
        PsGbaRegs_ClearErrorLatch(app->regs);
        xil_printf("[CMD] error latch clear requested\r\n");
        return;
    }

    if (strcmp(cmd, "reset") == 0) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (PsAppConsole_ParseU32Value(a1, &v) == 0)) {
            PsGbaRegs_SetSwReset(app->regs, v & 0x1U);
            xil_printf("[CMD] sw_reset=%u\r\n", (unsigned int)(v & 0x1U));
            return;
        }
        xil_printf("[CMD] usage: reset <0|1>\r\n");
        return;
    }

    if (strcmp(cmd, "audio") == 0) {
        a1 = strtok(NULL, " \t");

        if ((a1 != NULL) && (strcmp(a1, "status") == 0)) {
            xil_printf("[CMD] audio rate=%u bits=%u mute=%u vol=%u\r\n",
                       (unsigned int)app->audio->sample_rate_hz,
                       (unsigned int)app->audio->bits_per_sample,
                       (unsigned int)((app->audio->mute != 0U) || (app->audio->volume == 0U)),
                       (unsigned int)app->audio->volume);
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "reinit") == 0)) {
            if (PsAppRuntime_ProgramAudio(app) == XST_SUCCESS) xil_printf("[CMD] audio reinit ok\r\n");
            else xil_printf("[CMD] audio reinit failed\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "mute") == 0)) {
            a2 = strtok(NULL, " \t");
            if (PsAppConsole_ParseOnOff(a2, &v) == 0) {
                app->audio->mute = (u8)v;
                if (PsAppRuntime_ApplyAudioOutputState(app) == XST_SUCCESS) {
                    xil_printf("[CMD] audio mute=%u\r\n", (unsigned int)v);
                } else {
                    xil_printf("[CMD] audio mute failed\r\n");
                }
                return;
            }
            xil_printf("[CMD] usage: audio mute on|off\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "vol") == 0)) {
            a2 = strtok(NULL, " \t");
            if ((a2 != NULL) && (PsAppConsole_ParseU32Value(a2, &v) == 0)) {
                if (PsAppRuntime_SetAudioVolume(app, v) == XST_SUCCESS) {
                    xil_printf("[CMD] audio vol=%u\r\n", (unsigned int)app->audio->volume);
                } else {
                    xil_printf("[CMD] audio vol failed\r\n");
                }
                return;
            }
            xil_printf("[CMD] usage: audio vol <0-100>\r\n");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "fmt") == 0)) {
            u32 bits_u32;
            u16 sample_rate_reg;
            u16 word_length_bits;
            u16 bits;

            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            if ((a2 != NULL) && (a3 != NULL) &&
                (PsAppConsole_ParseU32Value(a2, &v) == 0) &&
                (PsAppConsole_ParseU32Value(a3, &bits_u32) == 0)) {
                bits = (u16)bits_u32;
                if ((PsAudioCodec_ResolveSampleRateReg(v, &sample_rate_reg) == XST_SUCCESS) &&
                    (PsAudioCodec_ResolveWordLengthBits(bits, &word_length_bits) == XST_SUCCESS)) {
                    (void)sample_rate_reg;
                    (void)word_length_bits;
                    app->audio->sample_rate_hz = v;
                    app->audio->bits_per_sample = bits;
                    if (PsAppRuntime_ProgramAudio(app) == XST_SUCCESS) {
                        xil_printf("[CMD] audio fmt=%uHz/%u-bit\r\n",
                                   (unsigned int)app->audio->sample_rate_hz,
                                   (unsigned int)app->audio->bits_per_sample);
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
                       (unsigned int)app->rom->loaded,
                       (unsigned int)(PsGbaRegs_Read(app->regs, GBA_REG_ROM_STATUS) & 0x1U),
                       (unsigned int)app->rom->size_bytes,
                       (unsigned int)app->rom->size_aligned,
                       (unsigned int)app->config->max_pak_addr);
            xil_printf("[CMD] bios mode=%s ext=%u load_ok=%u bytes=%u\r\n",
                       PsAppConsole_BiosModeName(app->rom->bios_mode),
                       (unsigned int)app->rom->bios_external_present,
                       (unsigned int)app->rom->bios_load_ok,
                       (unsigned int)app->rom->bios_bytes_loaded);
            xil_printf("[CMD] rom path=%s\r\n",
                       app->rom->path[0] != '\0' ? app->rom->path : "(none)");
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "probe") == 0)) {
            PsAppDiag_PrintRomProbe(diag);
            return;
        }

        if ((a1 != NULL) && (strcmp(a1, "load") == 0)) {
            if (PsAppRuntime_LoadRomFromSd(app, a2) == XST_SUCCESS) xil_printf("[CMD] rom load ok\r\n");
            else xil_printf("[CMD] rom load failed\r\n");
            return;
        }

        xil_printf("[CMD] usage: rom status | rom probe | rom load [path]\r\n");
        return;
    }

    if ((strcmp(cmd, "hdmi") == 0) || (strcmp(cmd, "vdma") == 0)) {
        a1 = strtok(NULL, " \t");
        if ((a1 != NULL) && (strcmp(a1, "park") == 0)) {
            a2 = strtok(NULL, " \t");
            if ((a2 != NULL) && (PsAppConsole_ParseU32Value(a2, &v) == 0)) {
                v %= app->vdma->frame_count;
                if (PsAppVideo_RequestFrame(video, v) == XST_SUCCESS) {
                    PsAppVideo_SyncDisplayFrame(video);
                    xil_printf("[CMD] %s park=%u\r\n", cmd, (unsigned int)v);
                } else {
                    xil_printf("[CMD] %s park failed\r\n", cmd);
                }
                return;
            }
            xil_printf("[CMD] usage: %s park <0|1|2>\r\n", cmd);
            return;
        }

        if ((strcmp(cmd, "hdmi") == 0) && (a1 != NULL) && (strcmp(a1, "fill") == 0)) {
            a2 = strtok(NULL, " \t");
            a3 = strtok(NULL, " \t");
            if ((a2 != NULL) && (a3 != NULL) && (PsAppConsole_ParseU32Value(a3, &v) == 0)) {
                if (strcmp(a2, "all") == 0) {
                    if (PsHdmiVdma_FillAllFrames(app->vdma, v) == XST_SUCCESS) {
                        xil_printf("[CMD] hdmi fill all color=0x%08x\r\n", (unsigned int)v);
                    } else {
                        xil_printf("[CMD] hdmi fill all failed\r\n");
                    }
                } else {
                    u32 idx;
                    if (PsAppConsole_ParseU32Value(a2, &idx) == 0) {
                        idx %= app->vdma->frame_count;
                        if (PsHdmiVdma_FillFrame(app->vdma, idx, v) == XST_SUCCESS) {
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

        xil_printf("[CMD] usage: %s park <0|1|2>\r\n", cmd);
        return;
    }

    xil_printf("[CMD] unknown: %s\r\n", cmd);
}

void PsAppConsoleTask(void *arg) {
    PsAppConsoleContext *ctx = (PsAppConsoleContext *)arg;
    char line[192];
    u32 len;
    u32 idle_cycles;

    len = 0U;
    idle_cycles = 0U;
    memset(line, 0, sizeof(line));

    xil_printf("[PS] Console ready, type 'help'\r\n> ");

    for (;;) {
        u8 ch;
        u32 n;

        n = XUartPs_Recv(ctx->uart, &ch, 1U);
        if (n == 1U) {
            idle_cycles = 0U;
            if ((ch == '\r') || (ch == '\n')) {
                xil_printf("\r\n");
                line[len] = '\0';
                PsAppConsole_ProcessLine(ctx, line);
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
            idle_cycles++;
            if (idle_cycles >= 100U) {
                /* Defensive recovery: if RX/TX got disabled by unexpected side effects,
                 * periodically re-enable UART and reset RX timeout path. */
                XUartPs_EnableUart(ctx->uart);
                XUartPs_SetOptions(ctx->uart,
                                   (u16)(XUARTPS_OPTION_RESET_RX | XUARTPS_OPTION_RESET_TMOUT));
                idle_cycles = 0U;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
