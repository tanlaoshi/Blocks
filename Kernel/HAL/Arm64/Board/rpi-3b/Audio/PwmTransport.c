/*
 * PwmTransport.c — GPIO40/41 → PWM0/1，方波发声与静音
 *
 * BCM2835 §6.1：GPIO40 = bits[2:0] of GPFSEL4，GPIO41 = bits[5:3]；ALT0(=4) = PWM0/PWM1。
 * BCM2835 §9.5-9.6：RNG1=0x10 周期，DAT1=0x14 占空，CTL=0x00（PWEN1 bit0 / PWEN2 bit8）。
 *
 * 口径：MS 模式（MODE1=0），输出高电平宽度 = DAT1/RNG1 × 周期；
 *       RNG = 10MHz/freq ⇒ 1 kHz 时 RNG=10000、DAT=5000 ⇒ 50% 方波。
 */
#include "AudioPrivate.h"
#include "Hal.h"

#define PI_GPIO_BASE       0x3F200000ULL
#define PI_GPIO_GPFSEL4    0x10u
#define PI_GPIO_FSEL_MASK  7u
#define PI_GPIO_FSEL_ALT0  4u
#define PI_PWM_PIN40_SHIFT 0u
#define PI_PWM_PIN41_SHIFT 3u

static volatile UINT32 *PiPwmRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(PI_PWM_BASE + (UINT64)Offset);
}

static volatile UINT32 *PiAudioGpioRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(PI_GPIO_BASE + (UINT64)Offset);
}

static void PiPwmPinSelect(UINT32 Shift) {
    volatile UINT32 *Select = PiAudioGpioRegister(PI_GPIO_GPFSEL4);
    UINT32 Value = *Select;

    Value &= ~(PI_GPIO_FSEL_MASK << Shift);
    Value |= (PI_GPIO_FSEL_ALT0 & PI_GPIO_FSEL_MASK) << Shift;
    *Select = Value;
}

int PiPwmTransportInitialize(void) {
    PiPwmPinSelect(PI_PWM_PIN40_SHIFT);
    PiPwmPinSelect(PI_PWM_PIN41_SHIFT);
    /* 先全停：清零占空比并关两路输出 */
    *PiPwmRegister(PI_PWM_DAT1) = 0u;
    *PiPwmRegister(PI_PWM_DAT2) = 0u;
    *PiPwmRegister(PI_PWM_CTL) = 0u;
    return 0;
}

int PiPwmSetTone(UINT32 FrequencyHz, UINT32 DutyPercent) {
    UINT32 Period;

    if (FrequencyHz == 0u || FrequencyHz > PI_AUDIO_PWM_HZ / 2u ||
        DutyPercent == 0u || DutyPercent > 100u) {
        return -1;
    }
    Period = PI_AUDIO_PWM_HZ / FrequencyHz;
    if (Period < 2u) {
        return -1;
    }
    *PiPwmRegister(PI_PWM_RNG1) = Period;
    *PiPwmRegister(PI_PWM_RNG2) = Period;
    *PiPwmRegister(PI_PWM_DAT1) = (Period * DutyPercent) / 100u;
    *PiPwmRegister(PI_PWM_DAT2) = (Period * DutyPercent) / 100u;
    /*
     * 只开 PWEN1/2（MS 模式，RNG/DAT 直接决定占空比）。
     * MSEN=1 时 PWM 从 FIFO 取数，不喂 FIFO 时输出恒 0 ⇒ 默认 tone 路径**不能**带 MSEN。
     * （2026-11-02 09:40 更正：此前以为"加 MSEN 后咔哒消失"，用户澄清**从头到尾没有过任何声音**，
     *  所以 MSEN 不是差异源；这条只是代码正确性要求，MSEN 仍留在 audiomsen 作对照。）
     */
    *PiPwmRegister(PI_PWM_CTL) = PI_PWM_CTL_PWEN1 | PI_PWM_CTL_PWEN2;
    return 0;
}

void PiPwmSilence(void) {
    *PiPwmRegister(PI_PWM_DAT1) = 0u;
    *PiPwmRegister(PI_PWM_DAT2) = 0u;
    *PiPwmRegister(PI_PWM_CTL) = 0u;
}

/*
 * MSEN1/2 = CTL bit7 / bit15。
 * 评审 09:00 纠正：**MSEN 是 Mark-Space enable，不是"串行器"**；
 * FIFO 串行输出要靠 MODE/USEF 位，这里不要照抄 Linux 的名字乱解释。
 * 本函数只做"打开 MSEN 位"的对照，语义按 BCM2835 §9.6 字面（M/S enable）。
 */

int PiPwmSerialiserEnable(UINT32 Period) {
    if (Period < 2u) {
        return -1;
    }
    *PiPwmRegister(PI_PWM_RNG1) = Period;
    *PiPwmRegister(PI_PWM_RNG2) = Period;
    *PiPwmRegister(PI_PWM_CTL) = PI_PWM_CTL_PWEN1 | PI_PWM_CTL_PWEN2 |
                                 PI_PWM_CTL_MSEN1 | PI_PWM_CTL_MSEN2;
    return 0;
}

void PiPwmSerialiserPush(UINT32 Value) {
    *PiPwmRegister(PI_PWM_FIF1) = Value;
}

void PiPwmSerialiserDisable(void) {
    *PiPwmRegister(PI_PWM_CTL) = 0u;
    *PiPwmRegister(PI_PWM_DAT1) = 0u;
    *PiPwmRegister(PI_PWM_DAT2) = 0u;
}
