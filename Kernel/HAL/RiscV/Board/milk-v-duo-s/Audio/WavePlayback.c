/*
 * WavePlayback.c — Duo 蜂鸣（方波）与 PCM 边界
 *
 * 蜂鸣：1 kHz 方波按 48 kHz 采样率写 I2S3（每周期 48 帧），幅度见 DUO_AUDIO_AMPLITUDE。
 * PCM ：`Samples != NULL` 时**如实返回 0**（DMA/PCM 通路属 audio-duo-1b，不谎报成功）。
 */
#include "AudioPrivate.h"
#include "Hal.h"

/*
 * 每毫秒写 DUO_AUDIO_FRAMES_PER_MS 帧：FIFO 深度 32，1 ms 被硬件取空，
 * 所以批量写 32 不溢出、也不需要 TX_STATUS（该寄存器没有"剩余空位"字段）。
 * 代价：有效采样率 = 32 kHz（48 kHz 的 2/3），音高按 2/3 折算 —— 先求"能响"。
 */
#define DUO_AUDIO_FRAMES_PER_MS 32u

static int DuoToneMilliseconds(UINT32 FrequencyHz, UINT32 Milliseconds) {
    UINT32 PeriodFrames;
    UINT32 Frame;
    UINT32 ElapsedMilliseconds = 0u;
    UINT32 Phase = 0u;

    if (FrequencyHz == 0u) {
        return 0;
    }
    PeriodFrames = DUO_AUDIO_RATE_HZ / FrequencyHz;
    if (PeriodFrames < 2u) {
        return 0;
    }
    while (ElapsedMilliseconds < Milliseconds) {
        for (Frame = 0u; Frame < DUO_AUDIO_FRAMES_PER_MS; Frame++) {
            INT16 Sample = ((Phase % PeriodFrames) < (PeriodFrames / 2u))
                               ? (INT16)DUO_AUDIO_AMPLITUDE
                               : (INT16)-DUO_AUDIO_AMPLITUDE;
            if (DuoI2sWriteFrame(Sample, Sample) != 0) {
                return 0;
            }
            Phase++;
        }
        HalDelayMilliseconds(1u);
        ElapsedMilliseconds++;
    }
    return 1;
}

int DuoAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits) {
    (void)Samples;
    (void)Bytes;
    (void)RateHz;
    (void)Channels;
    (void)Bits;
    /* audio-duo-1：PCM 未接（DMA 通路留 duo-1b）；不谎报成功 */
    return 0;
}

void DuoAudioStop(void) {
    DuoI2sDisable();
}

void DuoAudioBeep(void) {
    UINT32 Index;

    for (Index = 0u; Index < DUO_AUDIO_BEEPS; Index++) {
        (void)DuoToneMilliseconds(DUO_AUDIO_BEEP_HZ, DUO_AUDIO_BEEP_MS);
        if (Index + 1u < DUO_AUDIO_BEEPS) {
            HalDelayMilliseconds(DUO_AUDIO_GAP_MS);
        }
    }
    DuoI2sDisable();
}
