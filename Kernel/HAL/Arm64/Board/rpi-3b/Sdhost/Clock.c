/*
 * Clock.c — SDHOST 复位与时钟/总线宽（bd-pi-sd）
 */
#include "Sdhost/Private.h"

void SdhostResetController(void) {
    UINT32 Temp;

    SdhostWrite32(SDVDD, SDVDD_POWER_OFF);
    SdhostWrite32(SDCMD, 0);
    SdhostWrite32(SDARG, 0);
    SdhostWrite32(SDTOUT, 0xf00000u);
    SdhostWrite32(SDCDIV, 0);
    SdhostWrite32(SDHSTS, SDHSTS_CLEAR_MASK);
    SdhostWrite32(SDHCFG, 0);
    SdhostWrite32(SDHBCT, 0);
    SdhostWrite32(SDHBLC, 0);

    Temp = SdhostRead32(SDEDM);
    Temp &= ~((SDEDM_THRESHOLD_MASK << SDEDM_READ_THRESHOLD_SHIFT) |
              (SDEDM_THRESHOLD_MASK << SDEDM_WRITE_THRESHOLD_SHIFT));
    Temp |= (FIFO_READ_THRESHOLD << SDEDM_READ_THRESHOLD_SHIFT) |
            (FIFO_WRITE_THRESHOLD << SDEDM_WRITE_THRESHOLD_SHIFT);
    SdhostWrite32(SDEDM, Temp);
    SdhostDelayMilliseconds(20);
    SdhostWrite32(SDVDD, SDVDD_POWER_ON);
    SdhostDelayMilliseconds(20);
    SdhostWrite32(SDHCFG, gSdhostHostConfiguration);
    SdhostWrite32(SDCDIV, gSdhostClockDivider);
}

void SdhostClockConfigure(UINT32 Clock) {
    UINT32 Div;
    UINT32 Actual;

    if (Clock < 100000u) {
        gSdhostClockDivider = SDCDIV_MAX_CDIV;
        SdhostWrite32(SDCDIV, gSdhostClockDivider);
        return;
    }
    if (gSdhostMaximumClock == 0) {
        gSdhostMaximumClock = 250000000u;
    }
    Div = gSdhostMaximumClock / Clock;
    if (Div < 2u) {
        Div = 2u;
    }
    if ((gSdhostMaximumClock / Div) > Clock) {
        Div++;
    }
    Div -= 2u;
    if (Div > SDCDIV_MAX_CDIV) {
        Div = SDCDIV_MAX_CDIV;
    }
    Actual = gSdhostMaximumClock / (Div + 2u);
    gSdhostClockDivider = Div;
    SdhostWrite32(SDCDIV, gSdhostClockDivider);
    SdhostWrite32(SDTOUT, Actual / 2u);
}

void SdhostInterfaceConfigure(UINT32 ClockHertz, int WideFourBit) {
    SdhostClockConfigure(ClockHertz);
    gSdhostHostConfiguration &= ~SDHCFG_WIDE_EXT_BUS;
    if (WideFourBit) {
        gSdhostHostConfiguration |= SDHCFG_WIDE_EXT_BUS;
    }
    gSdhostHostConfiguration |= SDHCFG_WIDE_INT_BUS;
    gSdhostHostConfiguration |= SDHCFG_SLOW_CARD;
    SdhostWrite32(SDHCFG, gSdhostHostConfiguration);
}

/* —— 命令 / PIO（对齐 send_command / finish / transfer）—— */

