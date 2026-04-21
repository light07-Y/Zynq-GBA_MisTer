#include "App/Src/ps_app_console_internal.h"

#include <string.h>

#include "xil_printf.h"

#include "Audio/Inc/ps_audio_codec.h"
#include "Diagnostics/Inc/ps_app_diag.h"
#include "Gba/Inc/ps_gba_regs.h"
#include "Vdma/Inc/ps_hdmi_vdma.h"
#include "Video/Inc/ps_app_video.h"

void PsAppConsole_PrintMediaHelp(void) {
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

u8 PsAppConsole_HandleMediaCommands(PsAppConsoleContext *ctx, const char *cmd) {
    PsAppRuntimeContext *app;
    PsAppVideoContext *video;
    PsAppDiagContext *diag;
    char *arg1;
    char *arg2;
    char *arg3;
    u32 value;

    if ((ctx == NULL) || (ctx->runtime == NULL) || (cmd == NULL)) {
        return 0U;
    }

    app = ctx->runtime;
    video = ctx->video;
    diag = ctx->diag;

    if (strcmp(cmd, "audio") == 0) {
        arg1 = strtok(NULL, " \t");

        if ((arg1 != NULL) && (strcmp(arg1, "status") == 0)) {
            xil_printf("[CMD] audio rate=%u bits=%u mute=%u vol=%u\r\n",
                       (unsigned int)app->audio->sample_rate_hz,
                       (unsigned int)app->audio->bits_per_sample,
                       (unsigned int)((app->audio->mute != 0U) || (app->audio->volume == 0U)),
                       (unsigned int)app->audio->volume);
            return 1U;
        }

        if ((arg1 != NULL) && (strcmp(arg1, "reinit") == 0)) {
            if (PsAppRuntime_ProgramAudio(app) == XST_SUCCESS) {
                xil_printf("[CMD] audio reinit ok\r\n");
            } else {
                xil_printf("[CMD] audio reinit failed\r\n");
            }
            return 1U;
        }

        if ((arg1 != NULL) && (strcmp(arg1, "mute") == 0)) {
            arg2 = strtok(NULL, " \t");
            if (PsAppConsole_ParseOnOff(arg2, &value) == 0) {
                app->audio->mute = (u8)value;
                if (PsAppRuntime_ApplyAudioOutputState(app) == XST_SUCCESS) {
                    xil_printf("[CMD] audio mute=%u\r\n", (unsigned int)value);
                } else {
                    xil_printf("[CMD] audio mute failed\r\n");
                }
                return 1U;
            }
            xil_printf("[CMD] usage: audio mute on|off\r\n");
            return 1U;
        }

        if ((arg1 != NULL) && (strcmp(arg1, "vol") == 0)) {
            arg2 = strtok(NULL, " \t");
            if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &value) == 0)) {
                if (PsAppRuntime_SetAudioVolume(app, value) == XST_SUCCESS) {
                    xil_printf("[CMD] audio vol=%u\r\n", (unsigned int)app->audio->volume);
                } else {
                    xil_printf("[CMD] audio vol failed\r\n");
                }
                return 1U;
            }
            xil_printf("[CMD] usage: audio vol <0-100>\r\n");
            return 1U;
        }

        if ((arg1 != NULL) && (strcmp(arg1, "fmt") == 0)) {
            u32 bits_u32;
            u16 sample_rate_reg;
            u16 word_length_bits;
            u16 bits;

            arg2 = strtok(NULL, " \t");
            arg3 = strtok(NULL, " \t");
            if ((arg2 != NULL) && (arg3 != NULL) &&
                (PsAppConsole_ParseU32Value(arg2, &value) == 0) &&
                (PsAppConsole_ParseU32Value(arg3, &bits_u32) == 0)) {
                bits = (u16)bits_u32;
                if ((PsAudioCodec_ResolveSampleRateReg(value, &sample_rate_reg) == XST_SUCCESS) &&
                    (PsAudioCodec_ResolveWordLengthBits(bits, &word_length_bits) == XST_SUCCESS)) {
                    (void)sample_rate_reg;
                    (void)word_length_bits;
                    app->audio->sample_rate_hz = value;
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
                return 1U;
            }
            xil_printf("[CMD] usage: audio fmt <sample_rate_hz> <bits>\r\n");
            return 1U;
        }
    }

    if (strcmp(cmd, "rom") == 0) {
        arg1 = strtok(NULL, " \t");
        arg2 = strtok(NULL, " \t");

        if ((arg1 != NULL) && (strcmp(arg1, "status") == 0)) {
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
            return 1U;
        }

        if ((arg1 != NULL) && (strcmp(arg1, "probe") == 0)) {
            PsAppDiag_PrintRomProbe(diag);
            return 1U;
        }

        if ((arg1 != NULL) && (strcmp(arg1, "load") == 0)) {
            if (PsAppRuntime_LoadRomFromSd(app, arg2) == XST_SUCCESS) {
                xil_printf("[CMD] rom load ok\r\n");
            } else {
                xil_printf("[CMD] rom load failed\r\n");
            }
            return 1U;
        }

        xil_printf("[CMD] usage: rom status | rom probe | rom load [path]\r\n");
        return 1U;
    }

    if ((strcmp(cmd, "hdmi") == 0) || (strcmp(cmd, "vdma") == 0)) {
        arg1 = strtok(NULL, " \t");
        if ((arg1 != NULL) && (strcmp(arg1, "park") == 0)) {
            arg2 = strtok(NULL, " \t");
            if ((arg2 != NULL) && (PsAppConsole_ParseU32Value(arg2, &value) == 0)) {
                value %= app->vdma->frame_count;
                if (PsAppVideo_RequestFrame(video, value) == XST_SUCCESS) {
                    PsAppVideo_SyncDisplayFrame(video);
                    xil_printf("[CMD] %s park=%u\r\n", cmd, (unsigned int)value);
                } else {
                    xil_printf("[CMD] %s park failed\r\n", cmd);
                }
                return 1U;
            }
            xil_printf("[CMD] usage: %s park <0|1|2>\r\n", cmd);
            return 1U;
        }

        if ((strcmp(cmd, "hdmi") == 0) && (arg1 != NULL) && (strcmp(arg1, "fill") == 0)) {
            arg2 = strtok(NULL, " \t");
            arg3 = strtok(NULL, " \t");
            if ((arg2 != NULL) && (arg3 != NULL) && (PsAppConsole_ParseU32Value(arg3, &value) == 0)) {
                if (strcmp(arg2, "all") == 0) {
                    if (PsHdmiVdma_FillAllFrames(app->vdma, value) == XST_SUCCESS) {
                        xil_printf("[CMD] hdmi fill all color=0x%08x\r\n", (unsigned int)value);
                    } else {
                        xil_printf("[CMD] hdmi fill all failed\r\n");
                    }
                } else {
                    u32 idx;
                    if (PsAppConsole_ParseU32Value(arg2, &idx) == 0) {
                        idx %= app->vdma->frame_count;
                        if (PsHdmiVdma_FillFrame(app->vdma, idx, value) == XST_SUCCESS) {
                            xil_printf("[CMD] hdmi fill frame=%u color=0x%08x\r\n",
                                       (unsigned int)idx,
                                       (unsigned int)value);
                        } else {
                            xil_printf("[CMD] hdmi fill failed\r\n");
                        }
                    } else {
                        xil_printf("[CMD] usage: hdmi fill <idx|all> <hex>\r\n");
                    }
                }
                return 1U;
            }
            xil_printf("[CMD] usage: hdmi fill <idx|all> <hex>\r\n");
            return 1U;
        }

        xil_printf("[CMD] usage: %s park <0|1|2>\r\n", cmd);
        return 1U;
    }

    return 0U;
}
