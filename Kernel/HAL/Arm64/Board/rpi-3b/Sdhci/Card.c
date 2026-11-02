/*
 * Card.c — SDHCI 主机准备（WiFi SDIO 用；不含 SD 存储卡识别）
 *
 * 旧 SdhciCardInitialize / SdhciHostInitialize 走 CMD8/ACMD41 挂 µSD，
 * 与现网 Sdhost 块路径重复且无人调用，已删。
 */
#include "Sdhci/Private.h"

int SdhciHostPrepare(void) {
    UINT32 Hertz;

    (void)BoardSdhciPowerOn();
    SdhciDelayMicroseconds(10000);
    gSdhciHostSpecification = (UINT16)(SdhciRead16(SDHCI_HOST_VERSION) & 0xFFu);
    {
        UINT32 Cap = SdhciRead32(SDHCI_CAPABILITIES);
        UINT32 CapMhz = (gSdhciHostSpecification >= SDHCI_SPEC_300)
                            ? ((Cap >> 8) & 0xFFu)
                            : ((Cap >> 8) & 0x3Fu);
        if (CapMhz != 0) {
            gSdhciBaseClockHertz = CapMhz * 1000000u;
        } else if (BoardMboxGetClockHz(BOARD_MBOX_CLOCK_EMMC, &Hertz) == 0 &&
                   Hertz != 0) {
            gSdhciBaseClockHertz = Hertz;
        } else {
            gSdhciBaseClockHertz = 100000000u;
            HalSerialBootMark("Boot: SDHCI clk fallback 100M\n");
        }
    }
    HalSerialBootMark("Boot: SDHCI ver=");
    {
        char Buffer[9];
        HalSerialFormatHex(Buffer, (UINT32)gSdhciHostSpecification, 2);
        HalSerialBootMark(Buffer);
        HalSerialBootMark(" base=");
        HalSerialFormatHex(Buffer, gSdhciBaseClockHertz, 8);
        HalSerialBootMark(Buffer);
    }
    HalSerialBootMark("Hertz\n");

    if ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT) == 0) {
        /* 部分板 CD 位不可靠；仍试（U-Boot 亦不唯 CD） */
        HalSerialBootMark("Boot: SDHCI CD=0 try anyway\n");
    }
    return 0;
}
