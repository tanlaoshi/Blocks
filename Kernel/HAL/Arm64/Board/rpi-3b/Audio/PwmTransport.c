/*
 * PwmTransport.c — GPIO40/41 → PWM0/1；MS tone + USEF FIFO 蜂鸣
 *
 * BCM2835 §6.1：GPIO40/41 ALT0 = PWM0/1。
 * Circle pwmsoundbasedevice：CTL = PWEN|USEF（±MSEN）；FIFO 交织 L/R。
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

static void PiPwmFifoWriteBoth(UINT32 Sample) {
    UINT32 Guard = 0u;

    while ((*PiPwmRegister(PI_PWM_STA) & PI_PWM_STA_FULL1) != 0u) {
        if (++Guard > 1000000u) {
            return;
        }
    }
    *PiPwmRegister(PI_PWM_FIF1) = Sample; /* 左 */
    *PiPwmRegister(PI_PWM_FIF1) = Sample; /* 右（同值 → 双耳同相） */
}

int PiPwmTransportInitialize(void) {
    PiPwmPinSelect(PI_PWM_PIN40_SHIFT);
    PiPwmPinSelect(PI_PWM_PIN41_SHIFT);
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
    *PiPwmRegister(PI_PWM_CTL) = PI_PWM_CTL_PWEN1 | PI_PWM_CTL_PWEN2;
    return 0;
}

void PiPwmSilence(void) {
    *PiPwmRegister(PI_PWM_CTL) = 0u;
    *PiPwmRegister(PI_PWM_DAT1) = 0u;
    *PiPwmRegister(PI_PWM_DAT2) = 0u;
}

/*
 * USEF FIFO 蜂鸣：RNG = 250 MHz / 48 kHz；每样点写 L+R；
 * 1 kHz ⇒ 半周期 24 样点高低交替（占空 0 ↔ RNG/2）。
 */
int PiPwmFifoTone(UINT32 FrequencyHz, UINT32 Milliseconds) {
    UINT32 Range;
    UINT32 HalfHigh;
    UINT32 PeriodSamples;
    UINT32 TotalSamples;
    UINT32 Index;
    UINT32 Phase;

    if (FrequencyHz == 0u || Milliseconds == 0u) {
        return -1;
    }
    Range = (PI_AUDIO_PWM_HZ + PI_AUDIO_SAMPLE_HZ / 2u) / PI_AUDIO_SAMPLE_HZ;
    if (Range < 2u) {
        return -1;
    }
    PeriodSamples = PI_AUDIO_SAMPLE_HZ / FrequencyHz;
    if (PeriodSamples < 2u) {
        PeriodSamples = 2u;
    }
    HalfHigh = Range / 2u;
    TotalSamples = (PI_AUDIO_SAMPLE_HZ * Milliseconds) / 1000u;
    if (TotalSamples == 0u) {
        TotalSamples = PeriodSamples;
    }

    *PiPwmRegister(PI_PWM_CTL) = 0u;
    *PiPwmRegister(PI_PWM_RNG1) = Range;
    *PiPwmRegister(PI_PWM_RNG2) = Range;
    *PiPwmRegister(PI_PWM_CTL) = PI_PWM_CTL_CLRF1;
    HalDelayMilliseconds(1u);
    *PiPwmRegister(PI_PWM_CTL) = PI_PWM_CTL_PWEN1 | PI_PWM_CTL_USEF1 |
                                 PI_PWM_CTL_PWEN2 | PI_PWM_CTL_USEF2 |
                                 PI_PWM_CTL_MSEN1 | PI_PWM_CTL_MSEN2;
    HalDelayMilliseconds(2u);

    Phase = 0u;
    for (Index = 0u; Index < TotalSamples; Index++) {
        UINT32 Duty = ((Phase % PeriodSamples) < (PeriodSamples / 2u))
                          ? HalfHigh
                          : 0u;
        PiPwmFifoWriteBoth(Duty);
        Phase++;
    }
    PiPwmSilence();
    return 0;
}

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
    PiPwmSilence();
}
