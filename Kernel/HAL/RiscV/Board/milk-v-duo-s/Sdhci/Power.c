/*
 * Power.c — Duo S WiFi（SD1）板级时序：时钟门控 / 上电 / 卡检测（duo-wifi-sdio）
 *
 * 顺序按厂商来得（别自己发明）：
 *
 *   1) 时钟门控（DuoWifiClockGate）
 *      Linux 走 CCF：clk-cv181x.c:551-575 声明 clk_axi4_sd1 / clk_sd1 / clk_100k_sd1，
 *      门控位在 CLK_EN_0（控制器基址 0x03002000，见 cv181x_base.dtsi:26-28）：
 *        bit21 = clk_axi4_sd1、bit22 = clk_sd1、bit23 = clk_100k_sd1，
 *      三者都带 CLK_IS_CRITICAL（常开）。我们没有 CCF，只能自己开。
 *
 *   2) 上电（DuoWifiPowerOn）
 *      厂商 aic8800_bsp/aicsdio.c:499-520（CONFIG_PLATFORM_CVITEK 分支）：
 *        gpio_direction_output(power_gpio, 1);  mdelay(50);
 *      全程没有复位脉冲，也没有碰 wakeup-gpio。
 *      `wakeup-gpio`（dts:88 = &porte 7）在整棵内核树里**零调用**：
 *      cvi_wifi_pin.c:41 只把它 export 成 cvi_get_wifi_wakeup_gpio，
 *      没有任何驱动用；它是运行期芯片→主机的唤醒线，不是上电使能，别驱动。
 *      本文件只读 GPIOE 的 DDR/DR 做对照（不写方向、不驱动）。
 *
 *      唯一与厂商不同的一步：**先拉低 100 ms 再拉高**。厂商不需要它，是因为
 *      它上电时模块本来就断电；我们是接手 FSBL/U-Boot 的现场，模块可能已被
 *      固件碰过（真机上 TOP 0x294 在我们写之前就是 0x300），断电再上电才能
 *      保证一次干净的 POR。
 *
 *   3) 上电后等 50 ms（厂商 mdelay(50)）；之后厂商最多给芯片 2000 ms 反应窗口
 *      （aicsdio.c:534 down_timeout(&aic_chipup_sem, msecs_to_jiffies(2000))），
 *      这个窗口落在 Sdio.c 的 CMD5 重试里。
 *
 *   4) 卡检测（DuoWifiCardDetect）
 *      TOP 0x03000294：bit8 = sd1_carddet_ow、bit9 = sd1_carddet_sw、
 *      bit10 = sd1_phy_sel（0 = PWR_SD1 pad，厂商 U-Boot 已清，只读不改）。
 *      wifi-sd 是 non-removable、无 CD 脚，要靠覆盖让 PRESENT_STATE[16] 变 1。
 *      真机上 0x294 = 0x300（ow=1、sw=1）而 PRESENT_STATE[16] 仍是 0，
 *      所以这里做一次**极性自测**：sw=1 读一次、sw=0 读一次，谁让 [16] 变 1 就留谁。
 */
#include "Sdhci/Private.h"

static volatile UINT32 *DuoMmio(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

static void DuoBitLine(const char *Tag, UINT32 Value, UINT32 Bit) {
    SdhciMark(Tag);
    SdhciMark((Value & Bit) ? "1" : "0");
}

/*
 * SD1 三路时钟门控：Linux 由 CCF 在 probe 时打开（CLK_IS_CRITICAL），
 * 我们没人替它开，所以先读后补。恒返回 0；真开不出来，后面时钟起不来会另报。
 */
int DuoWifiClockGate(void) {
    volatile UINT32 *En = DuoMmio(DUO_CLK_BASE + DUO_CLK_EN_0);
    const UINT32 Want = DUO_CLK_SD1_AXI4_BIT | DUO_CLK_SD1_CARD_BIT | DUO_CLK_SD1_100K_BIT;
    UINT32 Before = *En;
    UINT32 After;

    SdhciMarkHex("WiFi: CLK_EN_0=", Before, 8);
    DuoBitLine(" axi4_sd1=", Before, DUO_CLK_SD1_AXI4_BIT);
    DuoBitLine(" clk_sd1=", Before, DUO_CLK_SD1_CARD_BIT);
    DuoBitLine(" clk_100k_sd1=", Before, DUO_CLK_SD1_100K_BIT);
    SdhciMark("\n");

    if ((Before & Want) != Want) {
        *En = Before | Want;
        After = *En;
        SdhciMarkHex("WiFi: CLK_EN_0 -> ", After, 8);
        DuoBitLine(" axi4_sd1=", After, DUO_CLK_SD1_AXI4_BIT);
        DuoBitLine(" clk_sd1=", After, DUO_CLK_SD1_CARD_BIT);
        DuoBitLine(" clk_100k_sd1=", After, DUO_CLK_SD1_100K_BIT);
        SdhciMark(((After & Want) == Want) ? " (enabled)\n" : " (still off!)\n");
    }
    return 0;
}

int DuoWifiPowerOn(void) {
    volatile UINT32 *Ddr = DuoMmio(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DDR);
    volatile UINT32 *Dr = DuoMmio(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DR);
    UINT32 DdrValue;
    UINT32 DrValue;

    SdhciMarkHex("WiFi: GPIOA15 ddr=", *Ddr, 8);
    SdhciMarkHex(" dr=", *Dr, 8);
    SdhciMark("\n");

    /* 方向：输出（只改 bit15，其余位保持） */
    *Ddr = *Ddr | DUO_WIFI_POWER_BIT;
    /* 复位脉冲：先断电，再上电（见文件头第 2 条） */
    *Dr = *Dr & ~DUO_WIFI_POWER_BIT;
    SdhciDelayMilliseconds(DUO_WIFI_POWER_DOWN_MS);
    *Dr = *Dr | DUO_WIFI_POWER_BIT;
    /* 厂商 aicsdio.c:519 mdelay(50) */
    SdhciDelayMilliseconds(DUO_WIFI_POWER_UP_MS);

    DdrValue = *Ddr;
    DrValue = *Dr;
    SdhciMarkHex("WiFi: GPIOA15 -> ddr=", DdrValue, 8);
    SdhciMarkHex(" dr=", DrValue, 8);
    SdhciMark("\n");
    if ((DdrValue & DUO_WIFI_POWER_BIT) == 0) {
        SdhciMark("WiFi: GPIOA15 DDR bit15 write did not stick (GPIO clock/base?)\n");
        return -1;
    }
    if ((DrValue & DUO_WIFI_POWER_BIT) == 0) {
        SdhciMark("WiFi: GPIOA15 DR bit15 write did not stick\n");
        return -1;
    }
    SdhciMark("WiFi: GPIOA15=1 (cycle -");
    SdhciMarkDecimal("", DUO_WIFI_POWER_DOWN_MS);
    SdhciMark("ms/+");
    SdhciMarkDecimal("", DUO_WIFI_POWER_UP_MS);
    SdhciMark("ms)\n");
    return 0;
}

/*
 * SD1 焊盘功能选择（只读）。U-Boot `board/cvitek/cv181x/board.c:89-104` 的
 * PINMUX_SDIO1 / SD1_SDIO_PAD 分支写这六个：
 *   FMUX base 0x03001000 + D3=0xD0 / D2=0xD4 / D1=0xD8 / D0=0xDC / CMD=0xE0 / CLK=0xE4
 *   功能号 0 = PWR_SD1_*（即 SD1 功能本身，cv181x_pinlist_swconfig.h:273-320）
 * 如果真机上这六个不全是 0，说明 SD1 焊盘根本没被切成 SD1 功能 —— 那 CMD/CLK
 * 压根出不了一根线，比任何上电时序都致命。所以先只读打印，不擅自改。
 */
void DuoWifiPadPeek(void) {
    static const UINT32 Off[6] = {0xD0u, 0xD4u, 0xD8u, 0xDCu, 0xE0u, 0xE4u};
    static const char *const Name[6] = {"d3", "d2", "d1", "d0", "cmd", "clk"};
    int i;
    UINT32 Value;

    SdhciMark("WiFi: pads(0=SD1 func)");
    for (i = 0; i < 6; i++) {
        Value = *DuoMmio(DUO_PINMUX_BASE + Off[i]);
        SdhciMark(" ");
        SdhciMark(Name[i]);
        SdhciMark("=");
        SdhciMarkDecimal("", Value & 0x7u);
    }
    SdhciMark("\n");
}

/*
 * 实验开关：把 PE7（dts wakeup-gpio）驱动成输出并拉高。
 *
 * 默认**不做**：整棵厂商内核树里 cvi_get_wifi_wakeup_gpio() 只有定义+EXPORT，
 * 零调用；原厂 Linux 不碰这根线 WiFi 也能起来。只有 `wifiprobe wake` 会走到这里，
 * 用来一次性排掉"是不是漏了这根线"。注意若这根实际是模块→主机的输出脚，
 * 驱动成高就和对端打架，所以只当一次性实验，不做进默认路径。
 */
void DuoWifiWakeDrive(int Level) {
    volatile UINT32 *Ddr = DuoMmio(DUO_GPIOE_BASE + DUO_GPIO_SWPORT_DDR);
    volatile UINT32 *Dr = DuoMmio(DUO_GPIOE_BASE + DUO_GPIO_SWPORT_DR);

    SdhciMarkHex("WiFi: GPIOE7 drive -> ddr=", *Ddr, 8);
    SdhciMarkHex(" dr=", *Dr, 8);
    SdhciMark("\n");
    *Ddr = *Ddr | DUO_WIFI_WAKE_BIT;
    if (Level != 0) {
        *Dr = *Dr | DUO_WIFI_WAKE_BIT;
    } else {
        *Dr = *Dr & ~DUO_WIFI_WAKE_BIT;
    }
    SdhciMarkHex("WiFi: GPIOE7 -> ddr=", *Ddr, 8);
    SdhciMarkHex(" dr=", *Dr, 8);
    SdhciMark("\n");
}

/*
 * 只读对照：port E 不在 0x0302_x000 那一排（gpio@05021000，见 cv181x_base_riscv.dtsi:246），
 * PE7 是 dts 的 wakeup-gpio。厂商软件从不驱动它，所以这里既不写 DDR 也不写 DR，
 * 只把当前值打出来供对照。
 */
void DuoWifiWakePeek(void) {
    volatile UINT32 *Ddr = DuoMmio(DUO_GPIOE_BASE + DUO_GPIO_SWPORT_DDR);
    volatile UINT32 *Dr = DuoMmio(DUO_GPIOE_BASE + DUO_GPIO_SWPORT_DR);
    UINT32 DdrValue = *Ddr;
    UINT32 DrValue = *Dr;

    SdhciMarkHex("WiFi: GPIOE7 ddr=", DdrValue, 8);
    SdhciMarkHex(" dr=", DrValue, 8);
    DuoBitLine(" level=", DrValue, DUO_WIFI_WAKE_BIT);
    SdhciMark(((DdrValue & DUO_WIFI_WAKE_BIT) != 0) ? " (pad is output, value is just a snapshot)\n"
                                                     : " (pad is input)\n");
}

static UINT32 DuoPresentState(SDHCI_HOST *Host) {
    return SdhciRead32(Host, SDHCI_PRESENT_STATE);
}

static void DuoMarkCardDetect(const char *Tag, UINT32 Reg, UINT32 Pst) {
    SdhciMarkHex(Tag, Reg, 8);
    SdhciMarkHex(" pst=", Pst, 8);
    DuoBitLine(" inserted=", Pst, SDHCI_CARD_INSERTED);
    DuoBitLine(" stable=", Pst, SDHCI_CARD_STABLE);
    SdhciMark("\n");
}

/*
 * 卡检测覆盖 + 极性自测。要在控制器 400 kHz 跑起来之后调（要读 PRESENT_STATE）。
 * 返回 0：覆盖位写进去了（[16] 变不变都不门禁 —— Linux 在 non-removable 上
 * 也根本不看这位）；返回 -1：覆盖位本身写不进去。
 */
int DuoWifiCardDetect(SDHCI_HOST *Host) {
    volatile UINT32 *Reg = DuoMmio(DUO_TOP_BASE + DUO_TOP_SD_CTRL_OPT);
    UINT32 Before = *Reg;
    UINT32 Value;
    UINT32 Pst;

    SdhciMarkHex("WiFi: TOP 0x294 before=", Before, 8);
    DuoBitLine(" cd_ow=", Before, 1u << 8);
    DuoBitLine(" cd_sw=", Before, 1u << 9);
    DuoBitLine(" phy_sel=", Before, 1u << 10);
    SdhciMark("\n");

    /* 按 TRM：bit8 ow=1，bit9 sw=1（“有卡”），bit10 保持 0（PWR_SD1 pad） */
    *Reg = (Before | (1u << 8) | (1u << 9)) & ~(1u << 10);
    Value = *Reg;
    Pst = DuoPresentState(Host);
    DuoMarkCardDetect("WiFi: cd sw=1 -> 0x294=", Value, Pst);
    if ((Value & ((1u << 8) | (1u << 9))) != ((1u << 8) | (1u << 9))) {
        SdhciMark("WiFi: TOP 0x294 override did not stick\n");
        return -1;
    }
    if ((Pst & SDHCI_CARD_INSERTED) != 0) {
        return 0;
    }

    /* 反极性试一次：真机上 sw=1 时 [16] 仍 0，说明这颗核的位义/极性与 TRM 不一致 */
    *Reg = (Before | (1u << 8)) & ~((1u << 9) | (1u << 10));
    Value = *Reg;
    Pst = DuoPresentState(Host);
    DuoMarkCardDetect("WiFi: cd sw=0 -> 0x294=", Value, Pst);
    if ((Pst & SDHCI_CARD_INSERTED) != 0) {
        SdhciMark("WiFi: CD polarity is inverted, keep sw=0\n");
        return 0;
    }

    /* 两种极性都不进 PRESENT_STATE：恢复厂商固件的原值，别把状态改坏 */
    *Reg = Before;
    SdhciMark("WiFi: TOP 0x294 override never reaches PRESENT_STATE[16] (not a gate, continue)\n");
    return 0;
}
