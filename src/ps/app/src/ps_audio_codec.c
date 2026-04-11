#include "ps_audio_codec.h"

#include "xil_printf.h"

enum {
    SSM_R0_LEFT_LINE_IN    = 0x00,
    SSM_R1_RIGHT_LINE_IN   = 0x01,
    SSM_R2_LEFT_HP_OUT     = 0x02,
    SSM_R3_RIGHT_HP_OUT    = 0x03,
    SSM_R4_ANALOG_PATH     = 0x04,
    SSM_R5_DIGITAL_PATH    = 0x05,
    SSM_R6_POWER_MGMT      = 0x06,
    SSM_R7_DIGITAL_IF      = 0x07,
    SSM_R8_SAMPLE_RATE     = 0x08,
    SSM_R9_ACTIVE          = 0x09,
    SSM_R15_SOFT_RESET     = 0x0F
};

enum {
    SSM2603_SR_8KHZ_12M288   = 0x00CU,
    SSM2603_SR_12KHZ_12M288  = 0x010U,
    SSM2603_SR_16KHZ_12M288  = 0x014U,
    SSM2603_SR_24KHZ_12M288  = 0x038U,
    SSM2603_SR_32KHZ_12M288  = 0x018U,
    SSM2603_SR_48KHZ_12M288  = 0x000U,
    SSM2603_SR_96KHZ_12M288  = 0x01CU
};

enum {
    PS_AUDIO_I2C_IDLE_WAIT_LOOPS = 1000000U,
    PS_AUDIO_DELAY_LOOPS_PER_US = 200U
};

static void PsAudioCodec_DelayUs(u32 delay_us) {
    volatile u32 loops;
    while (delay_us != 0U) {
        loops = PS_AUDIO_DELAY_LOOPS_PER_US;
        while (loops != 0U) {
            loops--;
        }
        delay_us--;
    }
}

static XStatus PsAudioCodec_WaitBusIdle(PsAudioCodec *ctx) {
    u32 timeout;

    timeout = PS_AUDIO_I2C_IDLE_WAIT_LOOPS;
    while ((XIicPs_BusIsBusy(&ctx->i2c) == (s32)TRUE) && (timeout != 0U)) {
        timeout--;
    }

    if (timeout == 0U) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

static XStatus PsAudioCodec_WriteReg(PsAudioCodec *ctx, u8 reg, u16 data) {
    u8 tx[2];
    XStatus status;

    if ((ctx == 0) || (ctx->is_ready == 0U)) {
        return XST_FAILURE;
    }

    tx[0] = (u8)((reg << 1) | ((data >> 8) & 0x01U));
    tx[1] = (u8)(data & 0xFFU);

    status = PsAudioCodec_WaitBusIdle(ctx);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XIicPs_MasterSendPolled(&ctx->i2c, tx, 2, ctx->slave_addr);
    if (status != XST_SUCCESS) {
        return status;
    }

    return PsAudioCodec_WaitBusIdle(ctx);
}

static XStatus PsAudioCodec_WriteRegLogged(PsAudioCodec *ctx, u8 reg, u16 data, const char *tag) {
    XStatus status;

    status = PsAudioCodec_WriteReg(ctx, reg, data);
    if (status != XST_SUCCESS) {
        xil_printf("[AUDIO] %s failed reg=0x%02x data=0x%03x st=%d\r\n",
                   tag,
                   (unsigned int)reg,
                   (unsigned int)(data & 0x1FFU),
                   status);
    }

    return status;
}

XStatus PsAudioCodec_Init(PsAudioCodec *ctx, u32 i2c_base_addr, u16 slave_addr, u32 scl_hz) {
    XIicPs_Config *cfg;
    XStatus status;

    if (ctx == 0) {
        return XST_FAILURE;
    }

    cfg = XIicPs_LookupConfig(i2c_base_addr);
    if (cfg == 0) {
        return XST_FAILURE;
    }

    status = XIicPs_CfgInitialize(&ctx->i2c, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    if (scl_hz == 0U) {
        scl_hz = 100000U;
    }

    status = XIicPs_SetSClk(&ctx->i2c, scl_hz);
    if (status != XST_SUCCESS) {
        return status;
    }

    ctx->slave_addr = slave_addr;
    ctx->is_ready = 1U;
    ctx->is_muted = 0U;
    ctx->hp_volume = 0x79U;
    ctx->bits_per_sample = 16U;
    ctx->sample_rate_hz = 48000U;

    return XST_SUCCESS;
}

XStatus PsAudioCodec_ResolveSampleRateReg(u32 sample_rate_hz, u16 *sample_rate_reg_out) {
    if (sample_rate_reg_out == 0) {
        return XST_FAILURE;
    }

    switch (sample_rate_hz) {
        case 8000U:
            *sample_rate_reg_out = SSM2603_SR_8KHZ_12M288;
            return XST_SUCCESS;
        case 12000U:
            *sample_rate_reg_out = SSM2603_SR_12KHZ_12M288;
            return XST_SUCCESS;
        case 16000U:
            *sample_rate_reg_out = SSM2603_SR_16KHZ_12M288;
            return XST_SUCCESS;
        case 24000U:
            *sample_rate_reg_out = SSM2603_SR_24KHZ_12M288;
            return XST_SUCCESS;
        case 32000U:
            *sample_rate_reg_out = SSM2603_SR_32KHZ_12M288;
            return XST_SUCCESS;
        case 44100U:
        case 48000U:
            *sample_rate_reg_out = SSM2603_SR_48KHZ_12M288;
            return XST_SUCCESS;
        case 96000U:
            *sample_rate_reg_out = SSM2603_SR_96KHZ_12M288;
            return XST_SUCCESS;
        default:
            return XST_FAILURE;
    }
}

XStatus PsAudioCodec_ResolveWordLengthBits(u16 bits_per_sample, u16 *word_length_bits_out) {
    if (word_length_bits_out == 0) {
        return XST_FAILURE;
    }

    switch (bits_per_sample) {
        case 16U:
            *word_length_bits_out = 0x0U;
            return XST_SUCCESS;
        case 20U:
            *word_length_bits_out = 0x1U;
            return XST_SUCCESS;
        case 24U:
            *word_length_bits_out = 0x2U;
            return XST_SUCCESS;
        case 32U:
            *word_length_bits_out = 0x3U;
            return XST_SUCCESS;
        default:
            return XST_FAILURE;
    }
}

XStatus PsAudioCodec_ProgramPlayback(PsAudioCodec *ctx, u32 sample_rate_hz, u16 bits_per_sample) {
    XStatus status;
    u16 sample_rate_reg;
    u16 word_length_bits;
    u16 digital_if_reg;

    status = PsAudioCodec_ResolveSampleRateReg(sample_rate_hz, &sample_rate_reg);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = PsAudioCodec_ResolveWordLengthBits(bits_per_sample, &word_length_bits);
    if (status != XST_SUCCESS) {
        return status;
    }

    digital_if_reg = (u16)(0x0002U | (word_length_bits << 2));
    xil_printf("[AUDIO] program start sr=%u bits=%u\r\n",
               (unsigned int)sample_rate_hz,
               (unsigned int)bits_per_sample);

    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R15_SOFT_RESET, 0x000U, "R15_SOFT_RESET");
    if (status != XST_SUCCESS) return status;
    PsAudioCodec_DelayUs(1000U);

    /* Only keep DAC path active first to reduce bring-up pop noise. */
    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R6_POWER_MGMT, 0b000110111U, "R6_POWER_MGMT_INIT");
    if (status != XST_SUCCESS) return status;

    /* Mute floating line input paths. */
    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R0_LEFT_LINE_IN, 0b010010111U, "R0_LEFT_LINE_IN");
    if (status != XST_SUCCESS) return status;
    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R1_RIGHT_LINE_IN, 0b010010111U, "R1_RIGHT_LINE_IN");
    if (status != XST_SUCCESS) return status;

    /* Enable zero-cross mode to avoid clicks on volume changes. */
    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R2_LEFT_HP_OUT, 0b111111001U, "R2_LEFT_HP_OUT");
    if (status != XST_SUCCESS) return status;
    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R3_RIGHT_HP_OUT, 0b111111001U, "R3_RIGHT_HP_OUT");
    if (status != XST_SUCCESS) return status;

    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R4_ANALOG_PATH, 0b000010010U, "R4_ANALOG_PATH");
    if (status != XST_SUCCESS) return status;
    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R5_DIGITAL_PATH, 0b000000000U, "R5_DIGITAL_PATH");
    if (status != XST_SUCCESS) return status;

    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R7_DIGITAL_IF, digital_if_reg, "R7_DIGITAL_IF");
    if (status != XST_SUCCESS) return status;

    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R8_SAMPLE_RATE, sample_rate_reg, "R8_SAMPLE_RATE");
    if (status != XST_SUCCESS) return status;
    PsAudioCodec_DelayUs(1000U);

    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R9_ACTIVE, 0x001U, "R9_ACTIVE");
    if (status != XST_SUCCESS) return status;

    /* Wait long enough for codec analog reference to settle. */
    PsAudioCodec_DelayUs(75000U);

    status = PsAudioCodec_WriteRegLogged(ctx, SSM_R6_POWER_MGMT, 0b000100111U, "R6_POWER_MGMT_FINAL");
    if (status != XST_SUCCESS) return status;

    ctx->sample_rate_hz = sample_rate_hz;
    ctx->bits_per_sample = bits_per_sample;
    ctx->is_muted = 0U;
    ctx->hp_volume = 0x79U;
    xil_printf("[AUDIO] program done\r\n");

    return XST_SUCCESS;
}

XStatus PsAudioCodec_ProgramPlayback48k16b(PsAudioCodec *ctx) {
    return PsAudioCodec_ProgramPlayback(ctx, 48000U, 16U);
}

XStatus PsAudioCodec_SetMute(PsAudioCodec *ctx, u8 mute) {
    u16 analog_path;
    XStatus status;

    analog_path = (mute != 0U) ? 0b000011010U : 0b000010010U;
    status = PsAudioCodec_WriteReg(ctx, SSM_R4_ANALOG_PATH, analog_path);
    if (status == XST_SUCCESS) {
        ctx->is_muted = (mute != 0U) ? 1U : 0U;
    }

    return status;
}

XStatus PsAudioCodec_SetHeadphoneVolume(PsAudioCodec *ctx, u8 volume_7bit) {
    u16 reg;
    XStatus status;

    if (volume_7bit > 0x7FU) {
        volume_7bit = 0x7FU;
    }

    reg = (u16)(0x100U | 0x80U | volume_7bit);
    status = PsAudioCodec_WriteReg(ctx, SSM_R2_LEFT_HP_OUT, reg);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = PsAudioCodec_WriteReg(ctx, SSM_R3_RIGHT_HP_OUT, reg);
    if (status == XST_SUCCESS) {
        ctx->hp_volume = volume_7bit;
    }

    return status;
}
