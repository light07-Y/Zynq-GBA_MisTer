#include "App/Inc/ps_app_console.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"

#include "Diagnostics/Inc/ps_app_diag.h"
#include "App/Inc/ps_app_runtime.h"
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

static void PsAppConsole_PrintHelp(void) {
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
    xil_printf("  btn status           (PS-side BTN4/BTN5 on MIO50/51)\r\n");
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

    if ((strcmp(cmd, "core") == 0) || (strcmp(cmd, "turbo") == 0) ||
        (strcmp(cmd, "lock") == 0) || (strcmp(cmd, "remap") == 0)) {
        u32 bit;
        a1 = strtok(NULL, " \t");
        if (PsAppConsole_ParseOnOff(a1, &v) != 0) {
            xil_printf("[CMD] usage: %s on|off\r\n", cmd);
            return;
        }

        if (strcmp(cmd, "core") == 0) bit = 0U;
        else if (strcmp(cmd, "lock") == 0) bit = 1U;
        else if (strcmp(cmd, "turbo") == 0) bit = 2U;
        else bit = 5U;

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

    len = 0U;
    memset(line, 0, sizeof(line));

    xil_printf("[PS] Console ready, type 'help'\r\n> ");

    for (;;) {
        u8 ch;
        u32 n;

        n = XUartPs_Recv(ctx->uart, &ch, 1U);
        if (n == 1U) {
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
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
