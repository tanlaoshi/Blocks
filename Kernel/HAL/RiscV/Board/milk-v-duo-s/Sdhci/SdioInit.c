/*
 * SdioInit.c — Duo S SD1（wifi-sd）SDIO 枚举：probe 生命周期 + 控制器/电源起步
 *
 * 从 Sdio.c 的 SdhciWifiInitialize（原 494 行）拆出的业务步骤：
 *   ProbePrepare     入口准备：清 Cis、状态门、读 HOST_VERSION
 *   ControllerBringUp 时钟门控 + 软复位 + 400 kHz bring-up
 *   ModulePowerOn    模块上电 + 卡检测覆盖 + PRESENT_STATE 现场
 *   ProbeFinish      收尾：enum 结论、Host 状态、patch 三态复位
 *
 * 只被 SdhciWifiInitialize 的顺序表调用；步骤间上下文是 Private.h 的 WIFI_PROBE。
 * 行为与拆分前逐条一致（顺序、寄存器值、延时、日志字符串都不动）。
 */
#include "Sdhci/Private.h"

/*
 * 第 0 步：准备上下文 + 状态门 + 读 HOST_VERSION。
 * 返回 0 = 继续；1 = 上次已成功（主函数直接返回 0）；-1 = 失败。
 */
int ProbePrepare(WIFI_PROBE *P) {
    UINT8 *Raw = (UINT8 *)&P->Cis;
    UINT32 k;

    P->Host = SdhciHostAt(1);
    P->CisSeen = 0;

    /* 无条件先清零，避免任何"没解析就用残值"的路径（评审 P0-1） */
    for (k = 0; k < (UINT32)sizeof(P->Cis); k++) {
        Raw[k] = 0;
    }

    /* 无条件第一行：证明本函数被调到、以及跑的是哪一次构建 */
    SdhciMark("WiFi: probe start build=" __DATE__ " " __TIME__ "\n");

    if (gWifiSdioState != 0) {
        SdhciMark(gWifiSdioState > 0 ? "WiFi: skip (already ok; use wifiprobe to retry)\n"
                                     : "WiFi: skip (last=fail; use wifiprobe to retry)\n");
        return (gWifiSdioState > 0) ? 1 : -1;
    }
    if (P->Host == 0) {
        return SdioFail("SdhciHostAt(1)==NULL");
    }

    P->Version = SdhciRead16(P->Host, SDHCI_HOST_VERSION);
    SdhciMarkHex("WiFi: SD1 base=", (UINT32)P->Host->Base, 8);
    SdhciMarkHex(" version=", P->Version, 4);
    if (P->Version == 0x0000u || P->Version == 0xFFFFu) {
        SdhciMark(" (MMIO no response: clock/power/base?)");
    }
    SdhciMark("\n");
    return 0;
}

/*
 * 第 1 步：时钟门控 + 控制器软复位 + 400 kHz bring-up。
 * 原 Sdio.c 159-203，寄存器顺序/日志逐字保留。
 */
int ControllerBringUp(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;

    /*
     * ---------- S1：时钟门控（先于控制器复位） ----------
     * Linux 由 CCF 打开 clk_axi4_sd1/clk_sd1/clk_100k_sd1，我们得自己开；
     * 真机上 shift 前的 CLK_EN_0 就是这段要查的东西。
     */
    (void)DuoWifiClockGate();
    DuoWifiPadPeek();

    Host->Specification = (UINT16)(P->Version & 0xFFu);
    if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0) {
        SdhciMarkHex("WiFi:   SRSI=", SdhciRead8(Host, SDHCI_SOFTWARE_RESET), 2);
        SdhciMarkHex(" clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
        SdhciMark("\n");
        return SdioFail("software reset ALL");
    }
    SdhciMark("Duo SDHCI1 ");
    SdhciMarkHex("ver=", Host->Specification, 8);
    SdhciMarkHex(" cap=", SdhciRead32(Host, SDHCI_CAPABILITIES), 8);
    SdhciMarkHex(" area=", Host->VendorArea, 3);
    SdhciMark("\n");
    SdhciMarkHex("WiFi: MSHC_CTRL=", SdhciRead32(Host, Host->VendorArea + CV_MSHC_CTRL), 8);
    SdhciMarkHex(" phy=", SdhciRead32(Host, Host->VendorArea + CV_PHY_TX_RX), 8);
    SdhciMark("\n");

    /*
     * INT_ENABLE = 0xFFFFFFFF（和 SD0 的 Card.c:52 完全一样）。
     *
     * 这里原来是 0 —— 那是 08:24 那次真机 "CMD5 有去无回、INT_STATUS 恒 0、
     * PRESENT_STATE[0] CMD_INHIBIT 卡死" 的根因：SDHCI 的 Interrupt Status
     * Enable 是状态位的总闸，闸关着 -> 事件不发生 -> 命令完成/超时都看不见 ->
     * 命令 FSM 等软件 W1C 应答等到天荒地老 -> CMD_INHIBIT 恒 1，重发全部
     * 在第一道门就失败。SD0 一直是 0xFFFFFFFF，所以启动盘从没出过这问题。
     */
    if (SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
        SdhciMarkHex("WiFi:   clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
        SdhciMarkHex(" hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
        SdhciMark("\n");
        return SdioFail("clock 400kHz");
    }
    SdhciMarkHex("WiFi: 400k ok clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
    SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
    SdhciMarkHex(" hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
    SdhciMark("\n");
    return 0;
}

/*
 * 第 2 步：模块上电 + 卡检测覆盖 + PRESENT_STATE 现场。
 * 原 Sdio.c 205-230，时序/日志逐字保留。
 */
int ModulePowerOn(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;

    /*
     * ---------- S1：上电（时钟已经跑起来了，厂商也是先有 400k 再给模块上电） ----------
     * 厂商 aicsdio.c:499-520：gpio 高 → mdelay(50)；这里多一个低 100ms 的断电脉冲，
     * 因为我们是接手 FSBL/U-Boot 留下的现场。
     */
    if (DuoWifiPowerOn() != 0) {
        return SdioFail("GPIOA15 power-on");
    }
    DuoWifiWakePeek();

    /*
     * 卡检测覆盖 + 极性自测（要控制器起来才能读 PRESENT_STATE）。
     * Linux 在 non-removable 上根本不看这两位（sdhci.c:2438-2466 直接 return 1），
     * 所以只打印、不当作继续/放弃的门禁。
     */
    if (DuoWifiCardDetect(Host) != 0) {
        return SdioFail("TOP 0x294 card-detect override");
    }

    P->Present = SdhciRead32(Host, SDHCI_PRESENT_STATE);
    SdhciMarkHex("SD1: present=", P->Present, 8);
    SdhciMark(" CARD_INSERTED=");
    SdhciMark((P->Present & SDHCI_CARD_INSERTED) ? "1" : "0");
    SdhciMark(" CARD_STABLE=");
    SdhciMark((P->Present & SDHCI_CARD_STABLE) ? "1" : "0");
    SdhciMark("\n");
    return 0;
}

/*
 * ---------- S3 收尾 ----------
 * **验收线是 CIS，不是 FIFO 回环。** 早期把"写 1536 B 图案再读回来比对"当成功判据，
 * 那是错的：1536 是固件消息补齐后的线上长度（时序表 §2.6），读回来的长度得由
 * fn1 0x04 的待收块数决定，图案回读对不上是正常现象。
 * 更要紧的是：协议还没开始就往发送 FIFO 推 1536 B 垃圾，可能污染 S5 的第一条消息。
 * 所以默认路径**不跑**，只留一句"跳过"，要跑得人工 `wififw fifotest`。
 */
/*
 * 最后一步：enum 结论、misc 打印、Host 状态、patch 三态复位、probe 结论。
 * 原 Sdio.c 566-608；结论三档与日志逐字保留。
 */
int ProbeFinish(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;

    /* 评审 P0-2：CIS 没过就**不许**打 enum ok；Ready/身份各自独立 */
    SdhciMark(P->CisSeen != 0 ? "WiFi: sdio enum ok (cis)\n"
                              : "WiFi: sdio enum FAILED (no cis/manfid)\n");
    SdhciMark("WiFi: sdio fifo self-test skipped (run 'wififw fifotest')\n");

    /* fn1 0x04 杂项中断状态：本轮不轮询，只打一次，供 S4 对照 */
    if (SdhciIoRead8(Host, 1, AIC_MISC_INT_STATUS_REG_V3, &P->Data) == 0) {
        SdhciMarkHex("WiFi: fn1 misc=0x", P->Data, 2);
        SdhciMark("\n");
    }
    Host->CardType = SDHCI_CARD_SDIO;
    Host->Ready = 1; /* Ready 只表示"总线枚举完了"，与身份无关 */
    gWifiSdioState = 1;
    /*
     * 评审 P1-4：probe 成功说明"芯片重新握过手"，之前那次 patch 的结论不再可信
     * （可能掉过电、可能被 reset）。清掉三态，允许重新走 write confirm。
     */
    {
        extern void SdhciWifiPatchStateReset(void);
        SdhciWifiPatchStateReset();
    }
    /*
     * 评审 P0-2：结论行分三档，身份真的参与验收 ——
     *   cis+id 都有 → probe ok
     *   只有 cis     → probe ok (no identity)（可用，但 wififw 会拒绝写）
     *   cis 都没有   → probe ok (no cis, bus only)
     */
    if (P->CisSeen != 0 && SdhciWifiChipIsD80() != 0) {
        SdhciMark("WiFi: probe ok (cis+id)\n");
    } else if (P->CisSeen != 0) {
        SdhciMark("WiFi: probe ok (no identity)\n");
    } else {
        SdhciMark("WiFi: probe ok (no cis, bus only)\n");
    }
    return 0;
}
