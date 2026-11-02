/*
 * DigitalToAnalog.c — 片内 DAC（dac@0300A000）使能与探针
 *
 * 对表厂商 cv181xdac.c：
 *   on()       ：CTRL0 |= TXDAC_EN | I2S_RX_EN
 *   hw_params  ：48 kHz → CIC_DS_64 + AFE0 tick 0x19；立体声清 ANA2 mute 位
 */
#include "AudioPrivate.h"

static volatile UINT32 *DuoDacRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DUO_DAC_BASE + (UINT64)Offset);
}

int DuoDacInitialize(void) {
    UINT32 Value = *DuoDacRegister(DUO_DAC_CTRL0);
    UINT32 Control1;
    UINT32 Analog2;
    UINT32 Tick;

    Value |= DUO_DAC_TXDAC_EN | DUO_DAC_I2S_RX_EN;
    *DuoDacRegister(DUO_DAC_CTRL0) = Value;

    Control1 = *DuoDacRegister(DUO_DAC_CTRL1);
    Control1 = (Control1 & ~DUO_DAC_CIC_MASK) | DUO_DAC_CIC_DS_64;
    *DuoDacRegister(DUO_DAC_CTRL1) = Control1;

    Tick = *DuoDacRegister(DUO_DAC_AFE0);
    Tick = (Tick & ~DUO_DAC_AFE0_TICK_MASK) | DUO_DAC_AFE0_TICK_48K;
    *DuoDacRegister(DUO_DAC_AFE0) = Tick;

    Analog2 = *DuoDacRegister(DUO_DAC_ANA2);
    Analog2 &= ~(DUO_DAC_ANALOG_LEFT_OVERRIDE_ENABLE |
                 DUO_DAC_ANALOG_RIGHT_OVERRIDE_ENABLE);
    *DuoDacRegister(DUO_DAC_ANA2) = Analog2;

    *DuoDacRegister(DUO_DAC_AFE1) = DUO_DAC_VOLUME_BOTH;
    return 0;
}

int DuoDacProbe(void) {
    UINT32 Value = *DuoDacRegister(DUO_DAC_CTRL0);

    return ((Value & DUO_DAC_TXDAC_EN) != 0u) ? 1 : 0;
}
