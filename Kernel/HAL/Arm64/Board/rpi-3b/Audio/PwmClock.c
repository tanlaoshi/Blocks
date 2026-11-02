/*
 * PwmClock.c — Pi 3B PWM 时钟（CM_PWMCTL/DIV），目标 10 MHz
 *
 * 对表 BCM2835 ARM Peripherals §6.3（时钟管理器：PASSWORD=0x5A000000，
 * ENAB=bit4，BUSY=bit7，SRC bit0..3）与 Linux clk-bcm2835.c（CM_PWMCTL=0xA0 / CM_PWMDIV=0xA4）。
 *
 * 别改：目标 10 MHz 是评审钉死的口径（Docs/29 §6.1#3 已纠回，勿再写 50 MHz）。
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
    *Divider = PI_CM_PASSWORD | ((PI_CM_PWM_DIVI & 0xFFFu) << 12) | (PI_CM_PWM_DIVF & 0xFFFu);
    *Control = PI_CM_PASSWORD | PI_CM_ENABLE | PI_CM_SRC_OSC;
    for (Attempt = 0u; Attempt < 1000u; Attempt++) {
        if ((*Control & PI_CM_BUSY) != 0u) {
            return 0;
        }
        HalDelayMilliseconds(1u);
    }
    return -1;
}
