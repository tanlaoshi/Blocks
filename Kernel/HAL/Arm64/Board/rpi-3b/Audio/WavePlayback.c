/*
 * WavePlayback.c — 蜂鸣与"最小播放"的边界（audio-pi-1）
 *
 * 本刀只保证：`HalAudioBeep()` → 耳机孔听得见。
 * PCM（`Samples != NULL`）**如实返回 0**：PWM+MASH+DMA 的 PCM 通路是 pi-1b，
 * 不允许"返回成功却无声"（Docs/29 §0 禁止项）。
 */
#include "AudioPrivate.h"
#include "Hal.h"

static int PiAudioToneMs(UINT32 FrequencyHz, UINT32 Milliseconds) {
    if (PiPwmSetTone(FrequencyHz, 50u) != 0) {
        return 0;
    }
    HalDelayMilliseconds(Milliseconds);
    PiPwmSilence();
    return 1;
}

int BoardAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                      UINT32 Channels, UINT32 Bits) {
    (void)Samples;
    (void)Bytes;
    (void)RateHz;
    (void)Channels;
    (void)Bits;
    /* pi-1：PCM 未接（pi-1b 才做 PWM MASH/DMA）；不谎报成功 */
    return 0;
}

void BoardAudioStop(void) {
    PiPwmSilence();
}

void BoardAudioBeep(void) {
    UINT32 Index;

    for (Index = 0u; Index < PI_AUDIO_BEEPS; Index++) {
        (void)PiAudioToneMs(PI_AUDIO_BEEP_HZ, PI_AUDIO_BEEP_MS);
        if (Index + 1u < PI_AUDIO_BEEPS) {
            HalDelayMilliseconds(PI_AUDIO_GAP_MS);
        }
    }
}
