/*
 * PinConfigure.c — GPIO34–39 Alt3（WiFi SDIO）+ GPIO43 32kHz + 扩展 WL_ON
 */
#include "Wifi/Private.h"

#define GPFSEL0_OFFSET 0x00u
#define GPPUD_OFFSET   0x94u
#define GPPUDCLK0_OFFSET 0x98u
#define GPPUDCLK1_OFFSET 0x9Cu

static volatile UINT32 *WifiGpioRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(WIFI_GPIO_BASE + Offset);
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
    /* 无线 32kHz：GPIO43 Alt0 */
    WifiGpioPull(43u, 0u);
    WifiGpioFunctionSelect(43u, 4u);
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
