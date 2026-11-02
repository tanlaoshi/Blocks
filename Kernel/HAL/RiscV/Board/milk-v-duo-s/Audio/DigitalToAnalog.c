/*
 * DigitalToAnalog.c — 片内 DAC（dac@0300A000）使能与探针
 *
 * 对表厂商 sound/soc/cvitek/cv181xdac.c 的 cv181xdac_on():
 *   CTRL0 |= TXDAC_EN | I2S_RX_EN（位 0/1）—— 就这两句，DAC 其余寄存器用开机默认值。
 * 板级 dts 已删 mute-gpio-l/r ⇒ 无外置 mute 脚（Docs/29 §6.2 #4）。
 */
#include "AudioPrivate.h"

static volatile UINT32 *DuoDacRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DUO_DAC_BASE + (UINT64)Offset);
}

int DuoDacInitialize(void) {
    UINT32 Value = *DuoDacRegister(DUO_DAC_CTRL0);

    Value |= DUO_DAC_TXDAC_EN | DUO_DAC_I2S_RX_EN;
    *DuoDacRegister(DUO_DAC_CTRL0) = Value;
    return 0;
}

int DuoDacProbe(void) {
    UINT32 Value = *DuoDacRegister(DUO_DAC_CTRL0);

    return ((Value & DUO_DAC_TXDAC_EN) != 0u) ? 1 : 0;
}
