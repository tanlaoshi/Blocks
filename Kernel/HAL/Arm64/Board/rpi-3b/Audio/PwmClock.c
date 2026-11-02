/*
 * PwmClock.c — Pi 3B PWM 时钟（CM_PWMCTL/DIV）
 *
 * audio-pi-1b 对表 Circle `CGPIOClock::StartRate(250000000)`：
 *   SRC=PLLD(6) ≈500 MHz，DIVI=2 → **250 MHz** 载波。
 * （10 MHz/OSC 是 Linux pwm-bcm2835 dts 口径；裸机模拟孔走 Circle。）
 */
#include "AudioPrivate.h"
#include "Hal.h"

static volatile UINT32 *PiClockRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(PI_CM_BASE + (UINT64)Offset);
}

int PiPwmClockEnable(void) {
    volatile UINT32 *Control = PiClockRegister(PI_CM_PWMCTL);
    volatile UINT32 *Divider = PiClockRegister(PI_CM_PWMDIV);
    UINT32 Status;
    UINT32 Attempt;

    Status = *Control;
    *Control = PI_CM_PASSWORD | (Status & ~PI_CM_ENABLE);
    for (Attempt = 0u; Attempt < 1000u; Attempt++) {
        if ((*Control & PI_CM_BUSY) == 0u) {
            break;
        }
        HalDelayMilliseconds(1u);
    }
    *Divider = PI_CM_PASSWORD | ((PI_CM_PWM_DIVI & 0xFFFu) << 12) |
               (PI_CM_PWM_DIVF & 0xFFFu);
    *Control = PI_CM_PASSWORD | PI_CM_ENABLE | PI_CM_SRC_PLLD;
    for (Attempt = 0u; Attempt < 1000u; Attempt++) {
        if ((*Control & PI_CM_BUSY) != 0u) {
            HalDelayMilliseconds(2u); /* Circle：开钟后再稳 2 ms */
            return 0;
        }
        HalDelayMilliseconds(1u);
    }
    return -1;
}
