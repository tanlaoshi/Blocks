/*
 * Power.c — Duo S WiFi（SD1）板级上电与卡检测覆盖（duo-wifi-sdio）
 *
 * 两件事，都在 SDIO 控制器动作之前：
 *   1) GPIOA15 拉高给模块上电，等 ≥50 ms（aicsdio.c:514-519，dts &wifi_pin）。
 *      GPIOA15 的焊盘是 SPK_EN，TRM「FMUX GPIO_REG_IOCTRL_SPK_EN 0x0300_103C」
 *      写明其 function 默认值就是 3 = XGPIOA[15]，U-Boot 也没有重配这个焊盘，
 *      所以直接写 GPIO 的 DDR/DR 即可，不需要再动 pinmux。
 *   2) TOP 0x03000294[8]=1（sd1_carddet_ow）、[9]=1（sd1_carddet_sw）→ 假装有卡。
 *      wifi-sd 是 non-removable、无 CD 脚，不覆盖则 PRESENT_STATE[16] 恒 0。
 *      [10] = sd1_phy_sel（0 = PWR_SD1 pad），厂商 U-Boot 已清，这里只读不改。
 */
#include "Sdhci/Private.h"

static volatile UINT32 *DuoMmio(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

int DuoWifiPowerOn(void) {
    volatile UINT32 *Ddr = DuoMmio(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DDR);
    volatile UINT32 *Dr = DuoMmio(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DR);

    *Ddr = *Ddr | DUO_WIFI_POWER_BIT;
    *Dr = *Dr | DUO_WIFI_POWER_BIT;
    /* aicsdio.c:514 gpio_direction_output(pin,1) + :519 mdelay(50) */
    SdhciDelayMilliseconds(50);
    SdhciMark("WiFi: GPIOA15=1\n");
    return ((*Dr & DUO_WIFI_POWER_BIT) != 0) ? 0 : -1;
}

int DuoWifiCardDetectOverride(void) {
    volatile UINT32 *Reg = DuoMmio(DUO_TOP_BASE + DUO_TOP_SD_CTRL_OPT);
    UINT32 Value = *Reg;

    *Reg = Value | (1u << 8) | (1u << 9);
    Value = *Reg;
    SdhciMarkHex("WiFi: TOP 0x294=", Value, 8);
    SdhciMark(" cd_ow=");
    SdhciMark(((Value >> 8) & 1u) ? "1" : "0");
    SdhciMark(" cd_sw=");
    SdhciMark(((Value >> 9) & 1u) ? "1" : "0");
    SdhciMark(" phy_sel=");
    SdhciMark(((Value >> 10) & 1u) ? "1" : "0");
    SdhciMark("\n");
    if ((Value & ((1u << 8) | (1u << 9))) != ((1u << 8) | (1u << 9))) {
        SdhciMark("WiFi: TOP 0x294 override failed\n");
        return -1;
    }
    return 0;
}
