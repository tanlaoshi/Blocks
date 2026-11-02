/*
 * BringUp.c — Duo 音频上电序（audio-duo-1）
 *
 * 顺序：I2S3（含时钟门控）→ DAC 使能 → Probe（读 DAC CTRL0 的 TXDAC_EN）。
 * 判据：`HalAudioProbe()!=0`；`beep` 耳机有人耳声。
 */
#include "AudioPrivate.h"
#include "HalSerial.h"

static int sDuoAudioReady;

int DuoAudioInitialize(void) {
    if (DuoI2sInitialize() != 0) {
        HalSerialWrite("Boot: Audio I2S3 init fail\n");
        return -1;
    }
    (void)DuoDacInitialize();
    sDuoAudioReady = 1;
    HalSerialWrite("Boot: Audio I2S3->DAC ready\n");
    return 0;
}

int DuoAudioProbe(void) {
    if (sDuoAudioReady == 0) {
        if (DuoAudioInitialize() != 0) {
            return 0;
        }
    }
    return DuoDacProbe();
}
