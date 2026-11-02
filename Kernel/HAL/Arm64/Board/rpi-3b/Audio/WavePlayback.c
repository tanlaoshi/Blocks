/*
 * WavePlayback.c — 蜂鸣与 PCM 边界（audio-pi-1b：USEF FIFO 蜂鸣）
 *
 * PCM（`Samples != NULL`）如实返回 0：DMA 通路另刀；不谎报成功。
 */
#include "AudioPrivate.h"
#include "Hal.h"

int BoardAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                      UINT32 Channels, UINT32 Bits) {
    (void)Samples;
    (void)Bytes;
    (void)RateHz;
    (void)Channels;
    (void)Bits;
    return 0;
}

void BoardAudioStop(void) {
    PiPwmSilence();
}

void BoardAudioBeep(void) {
    UINT32 Index;

    (void)BoardAudioInitialize();
    for (Index = 0u; Index < PI_AUDIO_BEEPS; Index++) {
        (void)PiPwmFifoTone(PI_AUDIO_BEEP_HZ, PI_AUDIO_BEEP_MS);
        if (Index + 1u < PI_AUDIO_BEEPS) {
            HalDelayMilliseconds(PI_AUDIO_GAP_MS);
        }
    }
}
