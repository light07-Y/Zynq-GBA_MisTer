#ifndef PS_AUDIO_CODEC_H
#define PS_AUDIO_CODEC_H

#include "xstatus.h"
#include "xil_types.h"
#include "xiicps.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    XIicPs i2c;
    u16 slave_addr;
    u8 is_ready;
    u8 is_muted;
    u8 hp_volume;
    u16 bits_per_sample;
    u32 sample_rate_hz;
} PsAudioCodec;

#define SSM2603_I2C_ADDR 0x1AU

XStatus PsAudioCodec_Init(PsAudioCodec *ctx, u32 i2c_base_addr, u16 slave_addr, u32 scl_hz);
XStatus PsAudioCodec_ResolveSampleRateReg(u32 sample_rate_hz, u16 *sample_rate_reg_out);
XStatus PsAudioCodec_ResolveWordLengthBits(u16 bits_per_sample, u16 *word_length_bits_out);
XStatus PsAudioCodec_ProgramPlayback(PsAudioCodec *ctx, u32 sample_rate_hz, u16 bits_per_sample);
XStatus PsAudioCodec_ProgramPlayback48k16b(PsAudioCodec *ctx);
XStatus PsAudioCodec_SetMute(PsAudioCodec *ctx, u8 mute);
XStatus PsAudioCodec_SetHeadphoneVolume(PsAudioCodec *ctx, u8 volume_7bit);

#ifdef __cplusplus
}
#endif

#endif
