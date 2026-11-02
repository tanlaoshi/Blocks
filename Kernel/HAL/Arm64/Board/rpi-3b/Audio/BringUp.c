/*
 * BringUp.c — Pi 3B 音频上电序（audio-pi-1）
 *
 * 顺序：PWM 时钟（10 MHz）→ GPIO40/41 ALT0 + PWM 通道停机 → Probe 可读回。
 * 判据：`HalAudioProbe()!=0`；`beep` 耳机有人耳声（由人手判）。
 */
#include "AudioPrivate.h"
#include "HalSerial.h"

static int sPiAudioReady;

int BoardAudioInitialize(void) {
    if (PiPwmClockEnable() != 0) {
        HalSerialWrite("Boot: Audio PWM clock fail\n");
        return -1;
    }
    (void)PiPwmTransportInitialize();
    sPiAudioReady = 1;
    HalSerialWrite("Boot: Audio PWM ready (10MHz, GPIO40/41 ALT0)\n");
    return 0;
}

int BoardAudioProbe(void) {
    volatile UINT32 *Range;
    UINT32 Saved;
    UINT32 ReadBack;

    if (sPiAudioReady == 0) {
        if (BoardAudioInitialize() != 0) {
            return 0;
        }
    }
    /* 真反映硬件：写一个周期值再读回，读回一致才认为 PWM 可用 */
    Range = (volatile UINT32 *)(UINTN)(PI_PWM_BASE + (UINT64)PI_PWM_RNG1);
    Saved = *Range;
    *Range = 1000u;
    ReadBack = *Range;
    *Range = Saved;
    return (ReadBack == 1000u) ? 1 : 0;
}
