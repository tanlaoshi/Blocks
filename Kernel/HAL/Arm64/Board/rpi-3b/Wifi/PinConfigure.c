/*
 * PinConfigure.c — GPIO34–39 Alt3（WiFi SDIO）+ GPIO43 GPCLK2 32kHz + 扩展 WL_ON
 *
 * NVRAM boardflags3=0x08000000 要外部 LPO。GPIO43=Alt0 后须 CM_GP2 出 ~32.768 kHz。
 * 若固件/U-Boot 已开 GP2：勿先关掉再开错（会害 HT csr=0x50）。
 */
#include "Wifi/Private.h"

#define GPFSEL0_OFFSET 0x00u
#define GPPUD_OFFSET   0x94u
#define GPPUDCLK0_OFFSET 0x98u
#define GPPUDCLK1_OFFSET 0x9Cu

static volatile UINT32 *WifiGpioRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(WIFI_GPIO_BASE + Offset);
}

static volatile UINT32 *WifiClockManagerRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(WIFI_CLOCK_MANAGER_BASE + Offset);
}

static void WifiGpioFunctionSelect(UINT32 Pin, UINT32 Alternate) {
    UINT32 SelectIndex = Pin / 10u;
    UINT32 Shift = (Pin % 10u) * 3u;
    volatile UINT32 *Select = WifiGpioRegister(GPFSEL0_OFFSET + SelectIndex * 4u);
    UINT32 Value = *Select;

    Value &= ~(7u << Shift);
    Value |= (Alternate & 7u) << Shift;
    *Select = Value;
}

static void WifiGpioPull(UINT32 Pin, UINT32 Pull) {
    volatile UINT32 i;
    UINT32 ClockOffset = (Pin < 32u) ? GPPUDCLK0_OFFSET : GPPUDCLK1_OFFSET;
    UINT32 Bit = 1u << (Pin & 31u);

    *WifiGpioRegister(GPPUD_OFFSET) = Pull & 3u;
    for (i = 0; i < 150u; i++) {
        __asm__ volatile("nop");
    }
    *WifiGpioRegister(ClockOffset) = Bit;
    for (i = 0; i < 150u; i++) {
        __asm__ volatile("nop");
    }
    *WifiGpioRegister(GPPUD_OFFSET) = 0;
    *WifiGpioRegister(ClockOffset) = 0;
}

/*
 * 对表 BCM2835 §6.3 + Linux clk_set_rate(gp2, 32768)：
 *   19.2e6/32768 = 585 + 3840/4096（12-bit 小数）；MASH=1。
 * 已 ENAB|BUSY 则不动（保 start.elf/U-Boot 留下的钟）。
 */
static void WifiGpioClock2Enable32k(void) {
    volatile UINT32 *Control = WifiClockManagerRegister(WIFI_CLOCK_MANAGER_GP2_CONTROL_OFFSET);
    volatile UINT32 *Divider = WifiClockManagerRegister(WIFI_CLOCK_MANAGER_GP2_DIVIDER_OFFSET);
    UINT32 Status;
    UINT32 Attempt;
    /* DIVI=585, DIVF=3840 → ~32768 Hz；MASH=1（bits 10:9） */
    const UINT32 MashOne = (1u << 9);
    const UINT32 DividerValue = (585u << 12) | 3840u;

    Status = *Control;
    WifiBootMarkHexadecimal32("Boot: Pi WIFI GPCLK2 ctl0=", Status);
    HalSerialBootMarkForce("\n");
    if ((Status & (WIFI_CLOCK_MANAGER_ENABLE | WIFI_CLOCK_MANAGER_BUSY)) ==
        (WIFI_CLOCK_MANAGER_ENABLE | WIFI_CLOCK_MANAGER_BUSY)) {
        HalSerialBootMarkForce("Boot: Pi WIFI GPCLK2 keep\n");
        return;
    }

    *Control = WIFI_CLOCK_MANAGER_PASSWORD | (Status & ~WIFI_CLOCK_MANAGER_ENABLE);
    for (Attempt = 0; Attempt < 1000u; Attempt++) {
        if ((*Control & WIFI_CLOCK_MANAGER_BUSY) == 0) {
            break;
        }
        SdhciDelayMicroseconds(10);
    }
    *Divider = WIFI_CLOCK_MANAGER_PASSWORD | DividerValue;
    SdhciDelayMicroseconds(10);
    *Control = WIFI_CLOCK_MANAGER_PASSWORD | WIFI_CLOCK_MANAGER_ENABLE | MashOne |
               WIFI_CLOCK_MANAGER_SOURCE_OSCILLATOR;
    for (Attempt = 0; Attempt < 1000u; Attempt++) {
        if ((*Control & WIFI_CLOCK_MANAGER_BUSY) != 0) {
            break;
        }
        SdhciDelayMicroseconds(10);
    }
    SdhciDelayMicroseconds(1000);
    Status = *Control;
    WifiBootMarkHexadecimal32("Boot: Pi WIFI GPCLK2 ctl1=", Status);
    HalSerialBootMarkForce("\n");
    if ((Status & WIFI_CLOCK_MANAGER_BUSY) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI GPCLK2 32k\n");
    } else {
        HalSerialBootMarkForce("Boot: Pi WIFI GPCLK2 fail (no BUSY)\n");
    }
}

void WifiPinConfigure(void) {
    UINT32 Pin;

    /* µSD 走 SDHOST：GPIO48–53 Alt0（对表 Circle / Ultibo） */
    for (Pin = 48u; Pin <= 53u; Pin++) {
        WifiGpioFunctionSelect(Pin, 4u); /* Alt0 */
    }
    /* Arasan → WiFi：GPIO34–39 Alt3 */
    for (Pin = 34u; Pin <= 39u; Pin++) {
        WifiGpioFunctionSelect(Pin, 7u); /* Alt3 */
        if (Pin == 34u) {
            WifiGpioPull(Pin, 0u); /* off */
        } else {
            WifiGpioPull(Pin, 2u); /* up */
        }
    }
    /* 无线 32kHz：GPIO43 Alt0 = GPCLK2 */
    WifiGpioPull(43u, 0u);
    WifiGpioFunctionSelect(43u, 4u);
    WifiGpioClock2Enable32k();
}

int WifiWirelessPowerOn(void) {
    if (BoardExpanderGpioWrite(BOARD_EXPANDER_WIRELESS_ON, 1u) != 0) {
        HalSerialBootMark("Boot: Pi WIFI WL_ON mbox fail\n");
        return -1;
    }
    SdhciDelayMicroseconds(150000u);
    HalSerialBootMark("Boot: Pi WIFI WL_ON\n");
    return 0;
}
