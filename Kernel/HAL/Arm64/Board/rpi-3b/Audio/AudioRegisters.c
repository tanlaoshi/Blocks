/*
 * AudioRegisters.c — 音频寄存器取证（audio-pi-1 排障用，只读）
 *
 * 为什么需要它：`play: beep` 打了但耳机没声，而 Probe 只是"写 RNG1 再读回"——
 * 必须看 CM_PWMCTL 是否真 ENAB/BUSY、GPFSEL4 是否真 ALT0、PWM CTL 是否真使能。
 *
 * Out[0..7] = CM_PWMCTL, CM_PWMDIV, PWM_CTL, PWM_STA, PWM_RNG1, PWM_DAT1, GPFSEL4,
 *              clock-enable 返回值（0=ok, -1=fail）
 */
#include "AudioPrivate.h"

#define PI_GPIO_BASE      0x3F200000ULL
#define PI_GPIO_GPFSEL4   0x10u

int BoardAudioReadRegisters(UINT32 Out[8]) {
    volatile UINT32 *Pwm = (volatile UINT32 *)(UINTN)PI_PWM_BASE;
    volatile UINT32 *Clock = (volatile UINT32 *)(UINTN)(PI_CM_BASE + PI_CM_PWMCTL);
    volatile UINT32 *Divider = (volatile UINT32 *)(UINTN)(PI_CM_BASE + PI_CM_PWMDIV);
    volatile UINT32 *Gpio = (volatile UINT32 *)(UINTN)(PI_GPIO_BASE + PI_GPIO_GPFSEL4);
    int Rc = -1;

    if (Out == 0) {
        return -1;
    }
    Rc = PiPwmClockEnable();
    Out[0] = *Clock;
    Out[1] = *Divider;
    Out[2] = *Pwm;
    Out[3] = *(volatile UINT32 *)(UINTN)(PI_PWM_BASE + PI_PWM_STA);
    Out[4] = *(volatile UINT32 *)(UINTN)(PI_PWM_BASE + PI_PWM_RNG1);
    Out[5] = *(volatile UINT32 *)(UINTN)(PI_PWM_BASE + PI_PWM_DAT1);
    Out[6] = *Gpio;
    Out[7] = (UINT32)Rc;
    return 0;
}
