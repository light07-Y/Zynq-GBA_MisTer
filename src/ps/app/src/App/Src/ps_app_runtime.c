#include "App/Inc/ps_app_runtime.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "sleep.h"
#include "xaxivdma_hw.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xuartps.h"
#include "xil_printf.h"

#include "Diagnostics/Inc/ps_app_diag.h"
#include "Input/Inc/ps_app_input.h"
#include "Save/Inc/ps_app_save.h"
#include "Video/Inc/ps_app_video.h"
#include "Rom/Inc/ps_rom_loader.h"
#include "UsbHost/Inc/ps_app_usbhost.h"

/*
 * 这里显式保留 MIO50/MIO51 的 SLCR 地址，是为了在应用启动阶段兜底覆盖
 * 旧 platform/ps7_init 残留的错误配置。实测错误配置会把 BTN4/BTN5 钉死，
 * 导致 raw50/raw51 始终不变，UART 侧也看不到按键边沿。
 */
#define PS_APP_SLCR_LOCK_ADDR          0xF8000004U
#define PS_APP_SLCR_UNLOCK_ADDR        0xF8000008U
#define PS_APP_SLCR_LOCK_CODE          0x0000767BU
#define PS_APP_SLCR_UNLOCK_CODE        0x0000DF0DU
#define PS_APP_MIO50_CFG_ADDR          0xF80007C8U
#define PS_APP_MIO51_CFG_ADDR          0xF80007CCU
#define PS_APP_MIO_GPIO_INPUT_CFG      0x00000200U
#define PS_APP_AUDIO_VOLUME_MAX        100U
#define PS_APP_AUDIO_VOLUME_STEP       5U
#define PS_APP_AUDIO_DEFAULT_VOLUME    95U
#define PS_APP_AUDIO_CODEC_MIN_VOLUME  0x5FU
#define PS_APP_AUDIO_CODEC_MAX_VOLUME  0x7FU

static void PsAppRuntime_CopyText(char *dst, size_t dst_size, const char *src) {
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

static u8 PsAppRuntime_Code3Eq(const char *code, const char *prefix3) {
    if ((code == NULL) || (prefix3 == NULL)) {
        return 0U;
    }
    return (memcmp(code, prefix3, 3U) == 0) ? 1U : 0U;
}

static u8 PsAppRuntime_Code4Eq(const char *code, const char *exact4) {
    if ((code == NULL) || (exact4 == NULL)) {
        return 0U;
    }
    return (memcmp(code, exact4, 4U) == 0) ? 1U : 0U;
}

static u8 PsAppRuntime_Code4InList(const char *code,
                                   const char *const *list,
                                   size_t count) {
    size_t idx;

    if ((code == NULL) || (list == NULL)) {
        return 0U;
    }

    for (idx = 0U; idx < count; ++idx) {
        if (PsAppRuntime_Code4Eq(code, list[idx]) != 0U) {
            return 1U;
        }
    }

    return 0U;
}

static void PsAppRuntime_ApplyRomDetection(PsAppRuntimeContext *ctx,
                                           const PsRomLoadResult *load_result) {
    static const char *const s_remap_codes[] = {
        "FBME", "FADE", "FDKE", "FDME", "FEBE", "FICE", "FMRE", "FP7E",
        "FSME", "FZLE", "FXVE", "FLBE", "FSRJ", "FGZJ", "FSDJ", "FADJ",
        "FM2J", "FGGJ", "FTWJ", "FMKJ", "FTBJ", "FDDJ", "FDMJ", "FWCJ",
        "FVFJ", "FCLJ", "FMBJ", "FSOJ", "FBMJ", "FMPJ", "FXVJ", "FZLJ",
        "FEBJ", "FICJ", "FDKJ", "FSMJ"
    };
    const char *game_code;
    u8 quirk_remap;
    u8 quirk_sram_disable;
    u8 quirk_gpio;
    u8 quirk_tilt;
    u8 quirk_solar;

    if ((ctx == NULL) || (load_result == NULL)) {
        return;
    }

    game_code = load_result->game_code;

    ctx->rom->sig_flash1m = (load_result->flash1m_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->sig_flash = (load_result->flash_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->sig_sram = (load_result->sram_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->sig_eeprom = (load_result->eeprom_offset != 0xFFFFFFFFU) ? 1U : 0U;
    ctx->rom->flash1m_offset = load_result->flash1m_offset;
    ctx->rom->flash_offset = load_result->flash_offset;
    ctx->rom->sram_offset = load_result->sram_offset;
    ctx->rom->eeprom_offset = load_result->eeprom_offset;
    PsAppRuntime_CopyText(ctx->rom->game_code, sizeof(ctx->rom->game_code), load_result->game_code);
    PsAppRuntime_CopyText(ctx->rom->maker_code, sizeof(ctx->rom->maker_code), load_result->maker_code);

    quirk_remap = PsAppRuntime_Code4InList(game_code,
                                           s_remap_codes,
                                           sizeof(s_remap_codes) / sizeof(s_remap_codes[0]));
    quirk_sram_disable =
        PsAppRuntime_Code3Eq(game_code, "AR8") || PsAppRuntime_Code3Eq(game_code, "ARO") ||
        PsAppRuntime_Code3Eq(game_code, "ALG") || PsAppRuntime_Code3Eq(game_code, "ALF") ||
        PsAppRuntime_Code3Eq(game_code, "BLF") || PsAppRuntime_Code3Eq(game_code, "BDB") ||
        PsAppRuntime_Code3Eq(game_code, "BG3") || PsAppRuntime_Code3Eq(game_code, "BDV") ||
        PsAppRuntime_Code3Eq(game_code, "A2Y") || PsAppRuntime_Code3Eq(game_code, "AI2") ||
        PsAppRuntime_Code3Eq(game_code, "BT4") || quirk_remap;
    quirk_gpio =
        PsAppRuntime_Code3Eq(game_code, "BPE") || PsAppRuntime_Code3Eq(game_code, "AXV") ||
        PsAppRuntime_Code3Eq(game_code, "AXP") || PsAppRuntime_Code3Eq(game_code, "RZW") ||
        PsAppRuntime_Code3Eq(game_code, "BKA") || PsAppRuntime_Code3Eq(game_code, "BR4") ||
        PsAppRuntime_Code3Eq(game_code, "V49") || PsAppRuntime_Code3Eq(game_code, "2GB") ||
        PsAppRuntime_Code3Eq(game_code, "U3I") || PsAppRuntime_Code3Eq(game_code, "U32") ||
        PsAppRuntime_Code3Eq(game_code, "U33");
    quirk_tilt =
        PsAppRuntime_Code3Eq(game_code, "KHP") || PsAppRuntime_Code3Eq(game_code, "KYG");
    quirk_solar =
        PsAppRuntime_Code3Eq(game_code, "U3I") ||
        PsAppRuntime_Code3Eq(game_code, "U32") ||
        PsAppRuntime_Code3Eq(game_code, "U33");

    ctx->rom->quirk_remap = quirk_remap;
    ctx->rom->quirk_sram_disable = quirk_sram_disable;
    ctx->rom->quirk_gpio = quirk_gpio;
    ctx->rom->quirk_tilt = quirk_tilt;
    ctx->rom->quirk_solar = quirk_solar;

    ctx->config->ctrl &= ~(GBA_CTRL_MEMORY_REMAP |
                           GBA_CTRL_FLASH_1M |
                           GBA_CTRL_SPECIAL_GPIO |
                           GBA_CTRL_TILT);
    ctx->config->ctrl |= GBA_CTRL_SRAM_FLASH_EN;

    if (quirk_remap != 0U) {
        ctx->config->ctrl |= GBA_CTRL_MEMORY_REMAP;
    }
    if (ctx->rom->sig_flash1m != 0U) {
        ctx->config->ctrl |= GBA_CTRL_FLASH_1M;
    }
    if (quirk_gpio != 0U) {
        ctx->config->ctrl |= GBA_CTRL_SPECIAL_GPIO;
    }
    if (quirk_tilt != 0U) {
        ctx->config->ctrl |= GBA_CTRL_TILT;
    }
    if (quirk_sram_disable != 0U) {
        ctx->config->ctrl &= ~GBA_CTRL_SRAM_FLASH_EN;
    }

    xil_printf("[ROMCFG] code=%s maker=%s sig flash1m=%u flash=%u sram=%u eeprom=%u\r\n",
               ctx->rom->game_code[0] != '\0' ? ctx->rom->game_code : "....",
               ctx->rom->maker_code[0] != '\0' ? ctx->rom->maker_code : "..",
               (unsigned int)ctx->rom->sig_flash1m,
               (unsigned int)ctx->rom->sig_flash,
               (unsigned int)ctx->rom->sig_sram,
               (unsigned int)ctx->rom->sig_eeprom);
    xil_printf("[ROMCFG] quirk remap=%u sram_dis=%u gpio=%u tilt=%u solar=%u ctrl=0x%08x\r\n",
               (unsigned int)ctx->rom->quirk_remap,
               (unsigned int)ctx->rom->quirk_sram_disable,
               (unsigned int)ctx->rom->quirk_gpio,
               (unsigned int)ctx->rom->quirk_tilt,
               (unsigned int)ctx->rom->quirk_solar,
               (unsigned int)ctx->config->ctrl);
}

static int PsAppRuntime_ResolveRomPath(const char *input,
                                       char *resolved_path,
                                       size_t resolved_path_size) {
    const char *effective_path;
    size_t input_len;

    if ((resolved_path == NULL) || (resolved_path_size == 0U)) {
        return -1;
    }

    effective_path = (input != NULL) ? input : PS_APP_DEFAULT_ROM_SD_PATH;
    input_len = strlen(effective_path);

    if (strstr(effective_path, ":/") != NULL) {
        if (input_len >= resolved_path_size) {
            return -1;
        }
        PsAppRuntime_CopyText(resolved_path, resolved_path_size, effective_path);
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

static void PsAppRuntime_PrintPhysicalKeys(u32 keys_mask) {
    static const char *kNames[10] = {
        "A", "B", "SELECT", "START", "RIGHT",
        "LEFT", "UP", "DOWN", "R", "L"
    };
    u32 idx;
    u32 any;

    xil_printf("[INPUT] phys=0x%03x", (unsigned int)(keys_mask & 0x3FFU));
    any = 0U;
    for (idx = 0U; idx < 10U; ++idx) {
        if ((keys_mask & (1U << idx)) != 0U) {
            xil_printf("%s%s", any != 0U ? "+" : " ", kNames[idx]);
            any = 1U;
        }
    }
    if (any == 0U) {
        xil_printf(" (idle)");
    }
    xil_printf("\r\n");
}

static const char *PsAppRuntime_GbaKeyNameByBit(u32 bit_index) {
    static const char *kNames[10] = {
        "A", "B", "SELECT", "START", "RIGHT",
        "LEFT", "UP", "DOWN", "R", "L"
    };

    if (bit_index < 10U) {
        return kNames[bit_index];
    }
    return NULL;
}

static const char *PsAppRuntime_PadKeyNameByBit(u32 bit_index) {
    switch (bit_index) {
        case 0U: return "DPAD_UP";
        case 1U: return "DPAD_DOWN";
        case 2U: return "DPAD_LEFT";
        case 3U: return "DPAD_RIGHT";
        case 4U: return "START";
        case 5U: return "BACK";
        case 6U: return "L3";
        case 7U: return "R3";
        case 8U: return "LB";
        case 9U: return "RB";
        case 10U: return "GUIDE";
        case 12U: return "A_BOTTOM";
        case 13U: return "B_RIGHT";
        case 14U: return "X_LEFT";
        case 15U: return "Y_TOP";
        case 16U: return "LT";
        case 17U: return "RT";
        case 18U: return "LS_RIGHT";
        case 19U: return "LS_LEFT";
        case 20U: return "LS_UP";
        case 21U: return "LS_DOWN";
        default: break;
    }
    return NULL;
}

static void PsAppRuntime_PrintMaskNamed(const char *tag,
                                        u32 mask,
                                        const char *(*name_of_bit)(u32),
                                        u32 max_bit) {
    u32 bit;
    u8 any;
    const char *name;

    xil_printf(" %s=", tag);
    any = 0U;
    for (bit = 0U; bit <= max_bit; ++bit) {
        if ((mask & (1UL << bit)) == 0U) {
            continue;
        }
        name = name_of_bit(bit);
        if (name == NULL) {
            continue;
        }
        xil_printf("%s%s", (any != 0U) ? "+" : "", name);
        any = 1U;
    }
    if (any == 0U) {
        xil_printf("-");
    }
}

static void PsAppRuntime_PrintInputDelta(PsAppRuntimeContext *ctx,
                                         u32 prev_pad_source_mask,
                                         u32 curr_pad_source_mask,
                                         u32 prev_gba_keys,
                                         u32 curr_gba_keys) {
    u32 pad_pressed;
    u32 pad_released;
    u32 gba_pressed;
    u32 gba_released;

    if ((ctx == NULL) || (ctx->input == NULL)) {
        return;
    }

    pad_pressed = curr_pad_source_mask & ~prev_pad_source_mask;
    pad_released = prev_pad_source_mask & ~curr_pad_source_mask;
    gba_pressed = curr_gba_keys & ~prev_gba_keys;
    gba_released = prev_gba_keys & ~curr_gba_keys;

    xil_printf("[INPUT] map ");
    PsAppRuntime_PrintMaskNamed("pad_press", pad_pressed, PsAppRuntime_PadKeyNameByBit, 21U);
    PsAppRuntime_PrintMaskNamed("pad_release", pad_released, PsAppRuntime_PadKeyNameByBit, 21U);
    PsAppRuntime_PrintMaskNamed("gba_press", gba_pressed, PsAppRuntime_GbaKeyNameByBit, 9U);
    PsAppRuntime_PrintMaskNamed("gba_release", gba_released, PsAppRuntime_GbaKeyNameByBit, 9U);
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    xil_printf(" ab_map=A<=B_RIGHT,B<=A_BOTTOM");
#else
    xil_printf(" ab_map=A<=A_BOTTOM,B<=B_RIGHT");
#endif
    xil_printf(" src=0x%08x gba=0x%03x\r\n",
               (unsigned int)curr_pad_source_mask,
               (unsigned int)(curr_gba_keys & 0x3FFU));
}

static void PsAppRuntime_PrintPsButtons(u32 ps_btn_mask) {
    xil_printf("[BTN] ps BTN4=%u BTN5=%u mask=0x%02x\r\n",
               (unsigned int)((ps_btn_mask & PS_APP_BTN4_MASK) != 0U),
               (unsigned int)((ps_btn_mask & PS_APP_BTN5_MASK) != 0U),
               (unsigned int)(ps_btn_mask & (PS_APP_BTN4_MASK | PS_APP_BTN5_MASK)));
}

static u8 PsAppRuntime_ClampAudioVolumePercent(u32 volume_percent) {
    if (volume_percent > PS_APP_AUDIO_VOLUME_MAX) {
        return (u8)PS_APP_AUDIO_VOLUME_MAX;
    }
    return (u8)volume_percent;
}

static u8 PsAppRuntime_EncodeCodecVolume(u8 volume_percent) {
    u32 codec_range;

    if (volume_percent == 0U) {
        return PS_APP_AUDIO_CODEC_MIN_VOLUME;
    }

    /*
     * 这个板子的 codec 实际可用听感主要集中在较高的耳机音量寄存器区间。
     * 如果把 0-100 线性映射到整个 0x00-0x7F，像 volume=45 这样的中档位
     * 会过早落到几乎听不见的衰减区。因此这里改为：
     * 1) volume=0 仍由上层静音逻辑处理；
     * 2) volume=1..100 只映射到更实用的可听区间 [0x5F, 0x7F]。
     */
    codec_range = (u32)PS_APP_AUDIO_CODEC_MAX_VOLUME - (u32)PS_APP_AUDIO_CODEC_MIN_VOLUME;
    return (u8)((u32)PS_APP_AUDIO_CODEC_MIN_VOLUME +
                (((u32)volume_percent * codec_range) + (PS_APP_AUDIO_VOLUME_MAX / 2U)) /
                    PS_APP_AUDIO_VOLUME_MAX);
}

/*
 * Zybo Z7-20 上这两个 PS 按键最终应工作在 0x00000200 配置下。
 * 如果启动后仍保留旧的 0x00001201，按钮输入会被内部上拉扰乱，
 * 所以这里在 XGpioPs 初始化完成后再强制写一次，确保应用层读到真实电平。
 */
static void PsAppRuntime_ForcePsButtonMioConfig(void) {
    Xil_Out32(PS_APP_SLCR_UNLOCK_ADDR, PS_APP_SLCR_UNLOCK_CODE);
    Xil_Out32(PS_APP_MIO50_CFG_ADDR, PS_APP_MIO_GPIO_INPUT_CFG);
    Xil_Out32(PS_APP_MIO51_CFG_ADDR, PS_APP_MIO_GPIO_INPUT_CFG);
    Xil_Out32(PS_APP_SLCR_LOCK_ADDR, PS_APP_SLCR_LOCK_CODE);
}

/*
 * 这个调试接口保留 raw50/raw51、bank1、dir1、outen1 和 mio50/mio51，
 * 是为了以后再次遇到“按键没反应”时，能够一步区分：
 * 1) 应用逻辑问题；
 * 2) GPIO 方向/输出使能问题；
 * 3) MIO/SLCR 配置残留问题。
 */
void PsAppRuntime_ReadPsButtonRawLevels(PsAppRuntimeContext *ctx,
                                        u32 *btn4_level,
                                        u32 *btn5_level,
                                        u32 *bank1_data,
                                        u32 *bank1_dir,
                                        u32 *bank1_outen,
                                        u32 *mio50_cfg,
                                        u32 *mio51_cfg) {
    if (btn4_level != NULL) {
        *btn4_level = 0U;
    }
    if (btn5_level != NULL) {
        *btn5_level = 0U;
    }
    if (bank1_data != NULL) {
        *bank1_data = 0U;
    }
    if (bank1_dir != NULL) {
        *bank1_dir = 0U;
    }
    if (bank1_outen != NULL) {
        *bank1_outen = 0U;
    }
    if (mio50_cfg != NULL) {
        *mio50_cfg = 0U;
    }
    if (mio51_cfg != NULL) {
        *mio51_cfg = 0U;
    }

    if ((ctx == NULL) || (ctx->ps_gpio == NULL) || (ctx->ps_gpio_ready == 0U)) {
        return;
    }

#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    if (btn4_level != NULL) {
        *btn4_level = XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN);
    }
    if (btn5_level != NULL) {
        *btn5_level = XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN);
    }
    if (bank1_data != NULL) {
        *bank1_data = XGpioPs_Read(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (bank1_dir != NULL) {
        *bank1_dir = XGpioPs_GetDirection(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (bank1_outen != NULL) {
        *bank1_outen = XGpioPs_GetOutputEnable(ctx->ps_gpio, XGPIOPS_BANK1);
    }
    if (mio50_cfg != NULL) {
        *mio50_cfg = Xil_In32(PS_APP_MIO50_CFG_ADDR);
    }
    if (mio51_cfg != NULL) {
        *mio51_cfg = Xil_In32(PS_APP_MIO51_CFG_ADDR);
    }
#endif
}

u32 PsAppRuntime_ReadPsButtonMask(PsAppRuntimeContext *ctx) {
    u32 ps_btn_mask;

    if ((ctx == NULL) || (ctx->ps_gpio == NULL) || (ctx->ps_gpio_ready == 0U)) {
        return 0U;
    }

    ps_btn_mask = 0U;
#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    /*
     * 当前板上修复后的实际行为是：空闲为低电平，按下为高电平。
     * 因此这里按 active-high 解释 BTN4/BTN5，和 raw50/raw51 的诊断输出保持一致。
     */
    if (XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN) != 0U) {
        ps_btn_mask |= PS_APP_BTN4_MASK;
    }
    if (XGpioPs_ReadPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN) != 0U) {
        ps_btn_mask |= PS_APP_BTN5_MASK;
    }
#endif
    return ps_btn_mask;
}

XStatus PsAppRuntime_ApplyAudioOutputState(PsAppRuntimeContext *ctx) {
    XStatus status;
    u8 codec_volume;
    u8 effective_mute;

    if ((ctx == NULL) || (ctx->audio == NULL)) {
        return XST_FAILURE;
    }

    ctx->audio->volume = PsAppRuntime_ClampAudioVolumePercent(ctx->audio->volume);
    if ((ctx->codec == NULL) || (ctx->codec->is_ready == 0U)) {
        return XST_SUCCESS;
    }

    codec_volume = PsAppRuntime_EncodeCodecVolume(ctx->audio->volume);
    status = PsAudioCodec_SetHeadphoneVolume(ctx->codec, codec_volume);
    if (status != XST_SUCCESS) {
        return status;
    }

    effective_mute = ((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)) ? 1U : 0U;
    return PsAudioCodec_SetMute(ctx->codec, effective_mute);
}

XStatus PsAppRuntime_SetAudioVolume(PsAppRuntimeContext *ctx, u32 volume_percent) {
    XStatus status;

    if ((ctx == NULL) || (ctx->audio == NULL)) {
        return XST_FAILURE;
    }

    ctx->audio->volume = PsAppRuntime_ClampAudioVolumePercent(volume_percent);
    status = PsAppRuntime_ApplyAudioOutputState(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XST_SUCCESS;
}

XStatus PsAppRuntime_InitPsGpio(PsAppRuntimeContext *ctx) {
    XStatus status;
#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
    XGpioPs_Config *cfg;
#endif

    if ((ctx == NULL) || (ctx->ps_gpio == NULL)) {
        return XST_FAILURE;
    }

    ctx->ps_gpio_ready = 0U;
    ctx->ps_btn_last_mask = 0U;

#if defined(XPAR_XGPIOPS_0_BASEADDR) || defined(XPAR_XGPIOPS_0_DEVICE_ID)
#if defined(SDT)
    cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_BASEADDR);
#else
    cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_DEVICE_ID);
#endif
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XGpioPs_CfgInitialize(ctx->ps_gpio, cfg, cfg->BaseAddr);
    if (status != XST_SUCCESS) {
        return status;
    }

    PsAppRuntime_ForcePsButtonMioConfig();

    XGpioPs_SetDirectionPin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN, 0U);
    XGpioPs_SetOutputEnablePin(ctx->ps_gpio, PS_APP_BTN4_MIO_PIN, 0U);
    XGpioPs_SetDirectionPin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN, 0U);
    XGpioPs_SetOutputEnablePin(ctx->ps_gpio, PS_APP_BTN5_MIO_PIN, 0U);

    ctx->ps_gpio_ready = 1U;
    ctx->ps_btn_last_mask = PsAppRuntime_ReadPsButtonMask(ctx);
    return XST_SUCCESS;
#else
    (void)status;
    xil_printf("[INIT] 8 warning: BSP has no XGPIOPS instance, regenerate platform from updated XSA\r\n");
    return XST_SUCCESS;
#endif
}

static void PsAppRuntime_InitDefaults(PsAppRuntimeContext *ctx) {
    ctx->config->ctrl =
        (PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL) | PS_APP_GBA_CTRL_BOOT_REQUIRED) &
        ~GBA_CTRL_CORE_ON;
    ctx->config->keys = 0U;
    ctx->config->max_pak_addr = 0U;
    ctx->config->cycle_precalc = 100U;
    ctx->config->rtc_timestamp = 0U;
    ctx->config->irq_enable = PS_APP_IRQ_MASK_DEFAULT;

    ctx->audio->sample_rate_hz = 48000U;
    ctx->audio->bits_per_sample = 16U;
    ctx->audio->mute = 0U;
    ctx->audio->volume = (u8)PS_APP_AUDIO_DEFAULT_VOLUME;

    ctx->video->display_frame_idx = 0xFFU;
    ctx->video->pending_frame_idx = 0xFFU;
    ctx->video->fbcap_last_frame_seq = 0U;
    ctx->video->fbcap_last_buf_idx = 0xFFU;
    ctx->video->fbcap_last_frame_idx = 0xFFU;
    ctx->video->blit_count = 0U;
    ctx->video->blit_last_us = 0U;
    ctx->video->blit_max_us = 0U;
    ctx->video->blit_total_us = 0U;
    ctx->video->blit_seq_gap_max = 0U;
    ctx->video->blit_seq_glitch_drop = 0U;

    ctx->rom->loaded = 0U;
    ctx->rom->is_loading = 0U;
    ctx->rom->sig_flash1m = 0U;
    ctx->rom->sig_flash = 0U;
    ctx->rom->sig_sram = 0U;
    ctx->rom->sig_eeprom = 0U;
    ctx->rom->quirk_remap = 0U;
    ctx->rom->quirk_sram_disable = 0U;
    ctx->rom->quirk_gpio = 0U;
    ctx->rom->quirk_tilt = 0U;
    ctx->rom->quirk_solar = 0U;
    ctx->rom->size_bytes = 0U;
    ctx->rom->size_aligned = 0U;
    ctx->rom->flash1m_offset = 0xFFFFFFFFU;
    ctx->rom->flash_offset = 0xFFFFFFFFU;
    ctx->rom->sram_offset = 0xFFFFFFFFU;
    ctx->rom->eeprom_offset = 0xFFFFFFFFU;
    ctx->rom->game_code[0] = '\0';
    ctx->rom->maker_code[0] = '\0';
    ctx->rom->path[0] = '\0';

    ctx->save->event_counters = 0U;
    ctx->save->flush_count = 0U;
    ctx->save->quiet_ticks = 0U;
    ctx->save->last_bytes = 0U;
    ctx->save->last_checksum = 0U;
    ctx->save->active_kind = PS_APP_SAVE_KIND_NONE;
    ctx->save->dirty = 0U;
    ctx->save->loaded_from_sd = 0U;
    ctx->save->path[0] = '\0';

    ctx->diag->last_runtime_irq_sts = 0U;
    ctx->diag->last_physical_keys = 0U;
    ctx->diag->warn_print_count = 0U;
    ctx->diag->warn_suppress_ticks = 0U;
    ctx->diag->warn_last_err_latch = 0U;
    ctx->diag->warn_last_vdma_errs = 0U;
    ctx->diag->stall_last_pc = 0U;
    ctx->diag->stall_last_mem = 0U;
    ctx->diag->stall_last_dma = 0U;
    ctx->diag->stall_last_frame = 0U;
    ctx->diag->stall_same_sample_count = 0U;
    ctx->diag->auto_boot_audit_printed = 0U;
    ctx->diag->auto_stall_audit_printed = 0U;
    ctx->diag->log_input_delta_enable = 0U;
    ctx->diag->log_fbscan_auto_enable = 0U;
    ctx->diag->delayed_chain_tick = 0U;
    ctx->diag->delayed_chain_printed = 0U;
    ctx->diag->fbscan_tick = 0U;
    ctx->diag->fbscan_dump_count = 0U;
    ctx->diag->fbscan_last_anomaly_tick = 0U;

    ctx->usb_host->enabled = (u8)((PS_APP_USBHOST_ENABLE_DEFAULT != 0U) ? 1U : 0U);
    ctx->usb_host->initialized = 0U;
    ctx->usb_host->irq_connected = 0U;
    ctx->usb_host->device_present = 0U;
    ctx->usb_host->xbox_interface_active = 0U;
    ctx->usb_host->bus_id = 0U;
    ctx->usb_host->interface_number = 0U;
    ctx->usb_host->port_speed = 0U;
    ctx->usb_host->vendor_id = 0U;
    ctx->usb_host->product_id = 0U;
    ctx->usb_host->interface_class = 0U;
    ctx->usb_host->interface_subclass = 0U;
    ctx->usb_host->interface_protocol = 0U;
    ctx->usb_host->ep_in_addr = 0U;
    ctx->usb_host->ep_out_addr = 0U;
    ctx->usb_host->irq_count = 0U;
    ctx->usb_host->event_count = 0U;
    ctx->usb_host->attach_count = 0U;
    ctx->usb_host->detach_count = 0U;
    ctx->usb_host->in_report_count = 0U;
    ctx->usb_host->in_report_error_count = 0U;
    ctx->usb_host->out_report_count = 0U;
    ctx->usb_host->out_report_error_count = 0U;

    ctx->input->enabled = (u8)((PS_APP_INPUT_ENABLE_DEFAULT != 0U) ? 1U : 0U);
    ctx->input->active = 0U;
    ctx->input->release_pending = 0U;
    ctx->input->report_valid = 0U;
    ctx->input->protocol_is_xinput = 0U;
    ctx->input->output_capable = 0U;
    ctx->input->vendor_id = 0U;
    ctx->input->product_id = 0U;
    ctx->input->interface_number = 0U;
    ctx->input->interface_class = 0U;
    ctx->input->interface_subclass = 0U;
    ctx->input->interface_protocol = 0U;
    ctx->input->ep_in_addr = 0U;
    ctx->input->ep_out_addr = 0U;
    ctx->input->ep_in_interval_ms = 0U;
    ctx->input->ep_out_interval_ms = 0U;
    ctx->input->buttons = 0U;
    ctx->input->lt = 0U;
    ctx->input->rt = 0U;
    ctx->input->lx = 0;
    ctx->input->ly = 0;
    ctx->input->rx = 0;
    ctx->input->ry = 0;
    ctx->input->mapped_keys = 0U;
    ctx->input->last_committed_keys = 0U;
    ctx->input->report_count = 0U;
    ctx->input->parse_error_count = 0U;
    ctx->input->unsupported_report_count = 0U;
    ctx->input->last_report_len = 0U;
    ctx->ps_gpio_ready = 0U;
    ctx->ps_btn_last_mask = 0U;
}

static XStatus PsAppRuntime_AutoloadDefaultRom(PsAppRuntimeContext *ctx) {
    XStatus status;

    xil_printf("[INIT] 9: default rom path=%s\r\n", PS_APP_DEFAULT_ROM_SD_PATH);
    status = PsAppRuntime_LoadRomFromSd(ctx, PS_APP_DEFAULT_ROM_SD_PATH);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 9 info: place a known-good ROM at %s\r\n",
                   PS_APP_DEFAULT_ROM_SD_PATH);
        xil_printf("[INIT] 9 info: or use 'rom load <path>' from the UART console\r\n");
        xil_printf("[INIT] 9 info: avoid relying on the previous hardcoded homebrew ROM for bring-up\r\n");
    }

    return status;
}

void PsAppRuntime_ApplyShadowConfig(PsAppRuntimeContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    PsGbaRegs_CommitConfig(ctx->regs,
                           ctx->config->ctrl,
                           ctx->config->keys,
                           ctx->config->max_pak_addr,
                           ctx->config->cycle_precalc,
                           ctx->config->rtc_timestamp);
}

XStatus PsAppRuntime_InitUart(PsAppRuntimeContext *ctx) {
    XUartPs_Config *cfg;
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    cfg = XUartPs_LookupConfig(XPAR_XUARTPS_0_BASEADDR);
    if (cfg == NULL) {
        return XST_FAILURE;
    }

    status = XUartPs_CfgInitialize(ctx->uart, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    return XUartPs_SetBaudRate(ctx->uart, PS_APP_UART_BAUDRATE);
}

XStatus PsAppRuntime_ProgramAudio(PsAppRuntimeContext *ctx) {
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    status = PsAudioCodec_ProgramPlayback(ctx->codec,
                                          ctx->audio->sample_rate_hz,
                                          ctx->audio->bits_per_sample);
    if (status != XST_SUCCESS) {
        return status;
    }

    return PsAppRuntime_ApplyAudioOutputState(ctx);
}

XStatus PsAppRuntime_LoadRomFromSd(PsAppRuntimeContext *ctx, const char *requested_path) {
    PsRomLoadResult load_result;
    char resolved_path[PS_APP_ROM_PATH_MAX_CHARS];
    XStatus status;

    if ((ctx == NULL) ||
        (PsAppRuntime_ResolveRomPath(requested_path, resolved_path, sizeof(resolved_path)) != 0)) {
        xil_printf("[ROM] invalid path\r\n");
        return XST_INVALID_PARAM;
    }

    ctx->rom->is_loading = 1U;
    ctx->rom->loaded = 0U;
    ctx->rom->size_bytes = 0U;
    ctx->rom->size_aligned = 0U;
    ctx->rom->sig_flash1m = 0U;
    ctx->rom->sig_flash = 0U;
    ctx->rom->sig_sram = 0U;
    ctx->rom->sig_eeprom = 0U;
    ctx->rom->quirk_remap = 0U;
    ctx->rom->quirk_sram_disable = 0U;
    ctx->rom->quirk_gpio = 0U;
    ctx->rom->quirk_tilt = 0U;
    ctx->rom->quirk_solar = 0U;
    ctx->rom->flash1m_offset = 0xFFFFFFFFU;
    ctx->rom->flash_offset = 0xFFFFFFFFU;
    ctx->rom->sram_offset = 0xFFFFFFFFU;
    ctx->rom->eeprom_offset = 0xFFFFFFFFU;
    ctx->rom->game_code[0] = '\0';
    ctx->rom->maker_code[0] = '\0';
    ctx->video->fbcap_last_buf_idx = 0xFFU;
    ctx->video->fbcap_last_frame_idx = 0xFFU;
    ctx->video->blit_count = 0U;
    ctx->video->blit_last_us = 0U;
    ctx->video->blit_max_us = 0U;
    ctx->video->blit_total_us = 0U;
    ctx->video->blit_seq_gap_max = 0U;
    ctx->video->blit_seq_glitch_drop = 0U;
    ctx->diag->stall_last_pc = 0U;
    ctx->diag->stall_last_mem = 0U;
    ctx->diag->stall_last_dma = 0U;
    ctx->diag->stall_last_frame = 0U;
    ctx->diag->stall_same_sample_count = 0U;
    ctx->diag->auto_stall_audit_printed = 0U;
    PsAppRuntime_CopyText(ctx->rom->path, sizeof(ctx->rom->path), resolved_path);

    ctx->config->ctrl |= PS_APP_GBA_CTRL_BOOT_REQUIRED;
    ctx->config->ctrl &= ~GBA_CTRL_CORE_ON;
    ctx->config->ctrl |= GBA_CTRL_ROM_LOADING;
    ctx->config->max_pak_addr = 0U;
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "rom-preload");

    PsGbaRegs_SetSwReset(ctx->regs, 1U);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "reset-assert");

    xil_printf("[ROM] load %s\r\n", ctx->rom->path);
    xil_printf("[ROM] target addr=0x%08x limit=%u\r\n",
               (unsigned int)PS_APP_GBA_ROM_REGION_BASE_ADDR,
               (unsigned int)PS_APP_GBA_ROM_REGION_MAX_BYTES);
    status = PsRomLoader_LoadSdFile(ctx->rom->path,
                                    (UINTPTR)PS_APP_GBA_ROM_REGION_BASE_ADDR,
                                    PS_APP_GBA_ROM_REGION_MAX_BYTES,
                                    &load_result);
    if (status != XST_SUCCESS) {
        ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
        ctx->config->max_pak_addr = 0U;
        PsAppRuntime_ApplyShadowConfig(ctx);
        PsGbaRegs_SetSwReset(ctx->regs, 1U);
        ctx->rom->is_loading = 0U;
        xil_printf("[ROM] load failed: %s\r\n", PsRomLoader_StrError(load_result.fs_result));
        return status;
    }

    if (PsAppSave_PrepareForRom(ctx->save_ctx, ctx->rom->path) != XST_SUCCESS) {
        xil_printf("[SAVE] preload skipped\r\n");
    }

    ctx->config->max_pak_addr = load_result.max_pak_addr & 0x1FFFFFFU;
    ctx->config->ctrl &= ~GBA_CTRL_ROM_LOADING;
    ctx->config->ctrl |= (GBA_CTRL_CORE_ON | PS_APP_GBA_CTRL_BOOT_REQUIRED);
    PsAppRuntime_ApplyRomDetection(ctx, &load_result);
    PsAppRuntime_ApplyShadowConfig(ctx);
    (void)PsHdmiVdma_FillAllFrames(ctx->vdma, 0x00000000U);
    (void)PsAppVideo_RequestFrame(ctx->video_ctx, 0U);
    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
    ctx->video->fbcap_last_frame_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "rom-postload");

    usleep(2000U);
    PsGbaRegs_SetSwReset(ctx->regs, 0U);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "reset-release");
    PsAppRuntime_ApplyShadowConfig(ctx);
    usleep(2000U);
    PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "post-release-commit");

    ctx->rom->loaded = 1U;
    ctx->rom->is_loading = 0U;
    ctx->rom->size_bytes = load_result.bytes_loaded;
    ctx->rom->size_aligned = load_result.bytes_aligned;

    xil_printf("[ROM] ready bytes=%u aligned=%u maxpak=0x%08x addr=0x%08x\r\n",
               (unsigned int)ctx->rom->size_bytes,
               (unsigned int)ctx->rom->size_aligned,
               (unsigned int)ctx->config->max_pak_addr,
               (unsigned int)PS_APP_GBA_ROM_REGION_BASE_ADDR);
    PsAppDiag_PrintRomProbe(ctx->diag_ctx);

    return XST_SUCCESS;
}

XStatus PsAppRuntime_InitSystem(PsAppRuntimeContext *ctx) {
    XStatus status;

    if (ctx == NULL) {
        return XST_FAILURE;
    }

    xil_printf("[INIT] 0: context cleared\r\n");
    xil_printf("[INIT] 1: gba regs bootstrap\r\n");
    PsGbaRegs_Init(ctx->regs, XPAR_ZYNQ_GBA_TOP_0_BASEADDR);
    PsAppRuntime_InitDefaults(ctx);
    PsAppSave_Reset(ctx->save_ctx);
    PsAppRuntime_ApplyShadowConfig(ctx);
    PsGbaRegs_SetIrqEnable(ctx->regs, ctx->config->irq_enable);
    PsGbaRegs_ClearIrqStatus(ctx->regs, PS_APP_IRQ_MASK_VSYNC | PS_APP_IRQ_MASK_ERROR);
    xil_printf("[INIT] 1: gba regs done\r\n");

    xil_printf("[INIT] 2: vdma init\r\n");
    status = PsHdmiVdma_Init(ctx->vdma,
                             XPAR_XAXIVDMA_0_BASEADDR,
                             (UINTPTR)PS_APP_FB_REGION_BASE_ADDR,
                             PS_APP_FB_FRAME_STORE_BYTES,
                             PS_APP_HDMI_WIDTH,
                             PS_APP_HDMI_HEIGHT,
                             PS_APP_HDMI_BPP);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 2 failed: %d\r\n", status);
        return status;
    }

    status = PsAppVideo_InitInterrupts(ctx->video_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 2.1 failed: %d\r\n", status);
        return status;
    }

    PsAppDiag_PrintVdmaSnapshot(ctx->diag_ctx, "post-init");
    xil_printf("[INIT] 2: vdma done\r\n");

    xil_printf("[INIT] 3: audio codec i2c init\r\n");
    status = PsAudioCodec_Init(ctx->codec,
                               XPAR_XIICPS_0_BASEADDR,
                               SSM2603_I2C_ADDR,
                               100000U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 3 warning: %d (audio disabled)\r\n", status);
        ctx->codec->is_ready = 0U;
    } else {
        xil_printf("[INIT] 3: audio i2c ready (defer program)\r\n");
    }

    xil_printf("[INIT] 5: clear boot framebuffers\r\n");
    status = PsAppVideo_RenderBootFrames(ctx->video_ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 failed: %d\r\n", status);
        return status;
    }
    xil_printf("[INIT] 5: framebuffers ready\r\n");
    xil_printf("[INIT] 5: trying park frame 0\r\n");
    status = PsAppVideo_RequestFrame(ctx->video_ctx, 0U);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 5 park failed: %d\r\n", status);
    } else {
        xil_printf("[INIT] 5 park ok\r\n");
    }
    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);

    if (ctx->codec->is_ready != 0U) {
        xil_printf("[INIT] 7: audio codec program\r\n");
        status = PsAppRuntime_ProgramAudio(ctx);
        if (status != XST_SUCCESS) {
            xil_printf("[INIT] 7 warning: %d (audio disabled)\r\n", status);
            ctx->codec->is_ready = 0U;
        } else {
            xil_printf("[INIT] 7: audio done\r\n");
        }
    }

    xil_printf("[INIT] 8: ps gpio BTN4/BTN5 init\r\n");
    status = PsAppRuntime_InitPsGpio(ctx);
    if (status != XST_SUCCESS) {
        xil_printf("[INIT] 8 failed: %d\r\n", status);
        return status;
    }
    if (ctx->ps_gpio_ready != 0U) {
        PsAppRuntime_PrintPsButtons(ctx->ps_btn_last_mask);
    }

    xil_printf("[INIT] 8.1: input map BTN3/2/1/0=left/up/down/right SW3/2/1/0=select(start-mod)/start/b/a SW3+BTN3=L SW3+BTN0=R\r\n");
    xil_printf("[INIT] 8.2: xinput parser init\r\n");
    PsAppInput_Init(ctx->input_ctx);
#if (PS_APP_INPUT_MAP_AB_BY_POSITION != 0U)
    xil_printf("[INIT] 8.2: gba map A<=B(right) B<=A(bottom) deadzone=%d trig=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#else
    xil_printf("[INIT] 8.2: gba map A<=A(bottom) B<=B(right) deadzone=%d trig=%u\r\n",
               (int)PS_APP_INPUT_LSTICK_DEADZONE,
               (unsigned int)PS_APP_INPUT_TRIGGER_THRESHOLD);
#endif
    xil_printf("[INIT] 8.3: usb host init\r\n");
    if (PsAppUsbHost_Init(ctx->usb_host_ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 8.3 warning: usb host unavailable\r\n");
    } else {
        PsAppUsbHost_PrintStatus(ctx->usb_host_ctx);
    }

    xil_printf("[INIT] 9: rom autoload\r\n");
    if (PsAppRuntime_AutoloadDefaultRom(ctx) != XST_SUCCESS) {
        xil_printf("[INIT] 9 warning: ROM autoload skipped\r\n");
    }

    return XST_SUCCESS;
}

static void PsAppRuntime_ServiceInputFast(PsAppRuntimeContext *ctx) {
    u32 prev_gba_keys;
    u32 mapped_keys;
    u32 prev_pad_source_mask;
    u32 curr_pad_source_mask;
    u32 frame_token;
    u8 log_needed;
    u8 input_override;

    if (ctx == NULL) {
        return;
    }

    PsAppUsbHost_Service(ctx->usb_host_ctx);
    PsAppInput_Service(ctx->input_ctx);
    frame_token = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
    PsAppInput_PublishFrame(ctx->input_ctx, frame_token);
    input_override = PsAppInput_ShouldOverrideKeys(ctx->input_ctx);
    if (input_override != 0U) {
        prev_gba_keys = ctx->input->last_committed_keys & 0x3FFU;
        mapped_keys = PsAppInput_GetMappedKeys(ctx->input_ctx) & 0x3FFU;
        prev_pad_source_mask = ctx->input->last_logged_source_mask;
        curr_pad_source_mask = ctx->input->source_snapshot_mask;
        log_needed = (u8)(((prev_gba_keys != mapped_keys) ||
                           (prev_pad_source_mask != curr_pad_source_mask)) ? 1U : 0U);
        if ((ctx->config->keys != mapped_keys) ||
            (PsAppInput_IsReleasePending(ctx->input_ctx) != 0U)) {
            ctx->config->keys = mapped_keys;
            PsAppRuntime_ApplyShadowConfig(ctx);
        }
        if ((log_needed != 0U) && (ctx->diag->log_input_delta_enable != 0U)) {
            PsAppRuntime_PrintInputDelta(ctx,
                                         prev_pad_source_mask,
                                         curr_pad_source_mask,
                                         prev_gba_keys,
                                         mapped_keys);
        }
        ctx->input->last_committed_keys = mapped_keys;
        ctx->input->last_logged_source_mask = curr_pad_source_mask;
        PsAppInput_ClearReleasePending(ctx->input_ctx);
    }
}

static void PsAppRuntime_ServiceSlow(PsAppRuntimeContext *ctx) {
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
    u32 ps_btn_mask;
    u32 pressed_ps_btns;

    if (ctx == NULL) {
        return;
    }

    status0 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS0);
    status1 = PsGbaRegs_Read(ctx->regs, GBA_REG_STATUS1);
    err_latch = PsGbaRegs_Read(ctx->regs, GBA_REG_ERROR_LATCH);
    irq_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_IRQ_STS) & 0x3U;
    dbg_pc = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_CPU_PC);
    dbg_dma = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_DMA);
    dbg_mem = PsGbaRegs_Read(ctx->regs, GBA_REG_DEBUG_MEM);
    vdma_status = XAxiVdma_GetStatus(&ctx->vdma->vdma, XAXIVDMA_READ);
    vdma_errs = vdma_status & XAXIVDMA_SR_ERR_ALL_MASK;
    cycles_missing = (status1 >> 2) & 0x3FFFU;
    physical_keys = status0 & 0x3FFU;

    if (ctx->diag->last_physical_keys != physical_keys) {
        PsAppRuntime_PrintPhysicalKeys(physical_keys);
        ctx->diag->last_physical_keys = physical_keys;
    }
    ps_btn_mask = PsAppRuntime_ReadPsButtonMask(ctx);
    if ((ctx->ps_gpio_ready != 0U) && (ctx->ps_btn_last_mask != ps_btn_mask)) {
        pressed_ps_btns = ps_btn_mask & ~ctx->ps_btn_last_mask;
        if ((pressed_ps_btns & PS_APP_BTN4_MASK) != 0U) {
            u32 next_volume = (u32)ctx->audio->volume + PS_APP_AUDIO_VOLUME_STEP;
            if (PsAppRuntime_SetAudioVolume(ctx, next_volume) == XST_SUCCESS) {
                xil_printf("[AUDIO] volume=%u mute=%u\r\n",
                           (unsigned int)ctx->audio->volume,
                           (unsigned int)((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)));
            }
        }
        if ((pressed_ps_btns & PS_APP_BTN5_MASK) != 0U) {
            u32 next_volume = (ctx->audio->volume > PS_APP_AUDIO_VOLUME_STEP) ?
                              ((u32)ctx->audio->volume - PS_APP_AUDIO_VOLUME_STEP) : 0U;
            if (PsAppRuntime_SetAudioVolume(ctx, next_volume) == XST_SUCCESS) {
                xil_printf("[AUDIO] volume=%u mute=%u\r\n",
                           (unsigned int)ctx->audio->volume,
                           (unsigned int)((ctx->audio->mute != 0U) || (ctx->audio->volume == 0U)));
            }
        }
        PsAppRuntime_PrintPsButtons(ps_btn_mask);
        ctx->ps_btn_last_mask = ps_btn_mask;
    }

    if (ctx->rom->is_loading != 0U) {
        PsAppVideo_AttemptRecover(ctx->video_ctx, vdma_status, vdma_errs);
        return;
    }

    if ((cycles_missing != 0U) || (err_latch != 0U) || (vdma_errs != 0U)) {
        u32 new_error = 0U;

        if ((err_latch != ctx->diag->warn_last_err_latch) ||
            (vdma_errs != ctx->diag->warn_last_vdma_errs)) {
            new_error = 1U;
        }
        if ((ctx->diag->warn_print_count == 0U) && (cycles_missing != 0U)) {
            new_error = 1U;
        }

        if (new_error != 0U) {
            xil_printf("[WARN] miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x\r\n",
                       (unsigned int)cycles_missing,
                       (unsigned int)err_latch,
                       (unsigned int)vdma_status,
                       (unsigned int)vdma_errs);
            ctx->diag->warn_print_count++;
            ctx->diag->warn_suppress_ticks = 0U;
        } else {
            ctx->diag->warn_suppress_ticks++;
            {
                u32 threshold = 300U;
                u32 i;
                for (i = 1U; i < ctx->diag->warn_print_count && i < 4U; i++) {
                    threshold *= 10U;
                }
                if (ctx->diag->warn_suppress_ticks >= threshold) {
                    xil_printf("[WARN] summary miss=%u err=0x%08x vdma_sr=0x%08x dma_err=0x%03x (suppressed %u ticks)\r\n",
                               (unsigned int)cycles_missing,
                               (unsigned int)err_latch,
                               (unsigned int)vdma_status,
                               (unsigned int)vdma_errs,
                               (unsigned int)ctx->diag->warn_suppress_ticks);
                    ctx->diag->warn_print_count++;
                    ctx->diag->warn_suppress_ticks = 0U;
                }
            }
        }

        ctx->diag->warn_last_err_latch = err_latch;
        ctx->diag->warn_last_vdma_errs = vdma_errs;
    } else {
        if (ctx->diag->warn_print_count != 0U) {
            xil_printf("[WARN] cleared (was miss=%u)\r\n",
                       (unsigned int)ctx->diag->warn_print_count);
        }
        ctx->diag->warn_print_count = 0U;
        ctx->diag->warn_suppress_ticks = 0U;
        ctx->diag->warn_last_err_latch = 0U;
        ctx->diag->warn_last_vdma_errs = 0U;
    }

    if (irq_sts != 0U) {
        if ((irq_sts & ~PS_APP_IRQ_MASK_VSYNC) != 0U) {
            if (ctx->diag->last_runtime_irq_sts != irq_sts) {
                xil_printf("[IRQ] sts=0x%x\r\n", (unsigned int)irq_sts);
                ctx->diag->last_runtime_irq_sts = irq_sts;
            }
        } else {
            ctx->diag->last_runtime_irq_sts = 0U;
        }

        PsGbaRegs_ClearIrqStatus(ctx->regs, irq_sts);
    } else {
        ctx->diag->last_runtime_irq_sts = 0U;
    }

    PsAppVideo_SyncDisplayFrame(ctx->video_ctx);
    PsAppDiag_MaybePrintStallAudit(ctx->diag_ctx, status1, dbg_pc, dbg_mem, dbg_dma);
    PsAppVideo_AttemptRecover(ctx->video_ctx, vdma_status, vdma_errs);
    PsAppSave_Service(ctx->save_ctx);

    /* 早期 BRAM 捕获诊断：ROM 加载后约 1 秒触发一次 */
    if (ctx->rom->loaded && ctx->diag->delayed_chain_printed == 0U &&
        ctx->diag->delayed_chain_tick >= 80U && ctx->diag->delayed_chain_tick <= 82U) {
        u32 cap_seq = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_SEQ);
        u32 cap_sts = PsGbaRegs_Read(ctx->regs, GBA_REG_FB_CAP_STATUS);
        u32 bram_w0 = PsAppVideo_ReadCaptureWord(cap_sts & 0x1U, 0U);
        u32 bram_w1 = PsAppVideo_ReadCaptureWord(cap_sts & 0x1U, 1U);
        u32 ctrl_rb = PsGbaRegs_Read(ctx->regs, GBA_REG_CTRL);
        xil_printf("[CAPDIAG] seq=%u buf=%u w0=0x%08x w1=0x%08x ctrl=0x%08x last_seq=%u vdma_sr=0x%08x\r\n",
                   (unsigned int)cap_seq,
                   (unsigned int)(cap_sts & 0x1U),
                   (unsigned int)bram_w0,
                   (unsigned int)bram_w1,
                   (unsigned int)ctrl_rb,
                   (unsigned int)ctx->video->fbcap_last_frame_seq,
                   (unsigned int)vdma_status);
    }

    if (ctx->rom->loaded && ctx->diag->delayed_chain_printed < 2U) {
        ctx->diag->delayed_chain_tick++;
        if ((ctx->diag->delayed_chain_printed == 0U && ctx->diag->delayed_chain_tick >= 500U) ||
            (ctx->diag->delayed_chain_printed == 1U && ctx->diag->delayed_chain_tick >= 1500U)) {
            xil_printf("[AUTO] delayed chain snapshot @tick=%u\r\n",
                       (unsigned int)ctx->diag->delayed_chain_tick);
            PsAppDiag_PrintChainSnapshot(ctx->diag_ctx, "delayed");
            PsAppDiag_PrintConfigReadback(ctx->diag_ctx, "delayed");
            ctx->diag->delayed_chain_printed++;
        }
    }

    if (ctx->rom->loaded &&
        (ctx->diag->log_fbscan_auto_enable != 0U) &&
        (ctx->diag->delayed_chain_tick > 200U)) {
        ctx->diag->fbscan_tick++;
        if ((ctx->diag->fbscan_tick % PS_APP_FBSCAN_INTERVAL_TICKS) == 0U) {
            if (ctx->diag->fbscan_dump_count < PS_APP_FBSCAN_MAX_DUMPS) {
                PsAppDiag_ScanFramebufferForAnomaly(ctx->diag_ctx, "auto", 0U);
            }
        }
    }
}

void PsAppRuntime_Service(PsAppRuntimeContext *ctx) {
    PsAppRuntime_ServiceInputFast(ctx);
    PsAppRuntime_ServiceSlow(ctx);
}

void PsAppMonitorTask(void *arg) {
    PsAppRuntimeContext *ctx = (PsAppRuntimeContext *)arg;
    TickType_t fast_delay_ticks;
    u32 slow_divider;
    u32 slow_tick_accum;

    fast_delay_ticks = pdMS_TO_TICKS(PS_APP_INPUT_SERVICE_INTERVAL_MS);
    if (fast_delay_ticks == 0U) {
        fast_delay_ticks = 1U;
    }
    slow_divider = PS_APP_MONITOR_INTERVAL_MS / PS_APP_INPUT_SERVICE_INTERVAL_MS;
    if (slow_divider == 0U) {
        slow_divider = 1U;
    }
    slow_tick_accum = slow_divider;

    for (;;) {
        /* 输入快路径每 1ms 跑一次，尽量贴近 USB IN 报告节拍；
         * 其余监控/诊断仍按原来的 10ms 慢节拍执行，避免打乱既有超时基准。 */
        PsAppRuntime_ServiceInputFast(ctx);
        if (slow_tick_accum >= slow_divider) {
            PsAppRuntime_ServiceSlow(ctx);
            slow_tick_accum = 0U;
        }
        slow_tick_accum++;
        vTaskDelay(fast_delay_ticks);
    }
}

void PsAppVideoPresentTask(void *arg) {
    PsAppRuntimeContext *ctx = (PsAppRuntimeContext *)arg;
    TickType_t present_delay_ticks;

    present_delay_ticks = pdMS_TO_TICKS(PS_APP_VIDEO_PRESENT_INTERVAL_MS);
    if (present_delay_ticks == 0U) {
        present_delay_ticks = 1U;
    }

    for (;;) {
        if ((ctx != NULL) && (ctx->rom != NULL) && (ctx->rom->loaded != 0U)) {
            PsAppVideo_PresentCapturedFrameIfReady(ctx->video_ctx);
        }
        vTaskDelay(present_delay_ticks);
    }
}
