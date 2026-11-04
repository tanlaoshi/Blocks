/*
 * WifiNetifLinkOutput.c — dwifi-netif-1：lwIP 出口 → AIC 发送（Duo）
 *
 * 对表 Pi：BoardWifiArmNetifLinkOutput / BoardWifiTryLinkOutput
 *          （HAL/Arm64/Board/rpi-3b/Wifi/WifiNetLinkOutput.c）
 *
 * 语义：
 *   Arm(StationMac)    join + 装钥成功后才允许武装 —— 未装钥就武装等于假 link up
 *   TryLinkOutput      武装时把整帧以太网帧交给 AicWifiDataSend；返回 0 = 已交出去
 *                      未武装 / 参数无效 / 发送失败 → 返回 -1，调用方回退 dwmac 有线
 *   计数               lwip_out：成功交给 AIC 的帧数（判据用）
 *
 * 为什么取 Host/索引这么取：
 *   · Host = SdhciHostAt(1)（AIC 是 SDIO function 1，与 WifiDhcpProbe.c 同源）
 *   · Vif/Ap = AicJoinLastIndexes（join 时的索引，对表 WifiAssoc.c:112）
 *   · MAC 由调用方传入（join 后 AicNetStationAddress；DHCP 需要站点 MAC 才不发零地址）
 *
 * 别改：Try 里任何失败都必须 **不** 打 net-lwip-out-ok；判据只能由真实成功触发。
 */
#include "WifiNetif.h"
#include "Wifi/AssocPrivate.h"
#include "Sdhci/Private.h"
#include "Wifi/HandshakePrivate.h" /* AicWifiDataSend */
#include "Wifi/NetPrivate.h"      /* AicNetStationAddress（经 WifiNetFrame 链也可，这里显式） */
#include "HalDevices.h"

#define AIC_NETIF_MAC_LENGTH     6u

static int sAicNetifArmed;
static UINT8 sAicNetifStationMac[AIC_NETIF_MAC_LENGTH];
static UINT32 sAicNetifLinkOutputCount;
static UINT32 sAicNetifLinkOutputFail;
static int sAicNetifOkPrinted;

int BoardWifiLinkOutputIsArmed(void) {
    return sAicNetifArmed;
}

void BoardWifiArmNetifLinkOutput(const UINT8 StationMac[AIC_NETIF_MAC_LENGTH]) {
    UINT32 Index;

    if (StationMac == 0) {
        return;
    }
    for (Index = 0u; Index < AIC_NETIF_MAC_LENGTH; Index++) {
        if (StationMac[Index] != 0u) {
            break;
        }
    }
    if (Index == AIC_NETIF_MAC_LENGTH) {
        SdhciMark("WiFi: netif arm refused (all-zero station mac)\n");
        return;
    }
    for (Index = 0u; Index < AIC_NETIF_MAC_LENGTH; Index++) {
        sAicNetifStationMac[Index] = StationMac[Index];
    }
    sAicNetifArmed = 1;
    sAicNetifOkPrinted = 0;
    /* 武装 = 开始允许 RX 喂 lwIP；sink 随武装挂上（评审红线①） */
    BoardWifiFeedAttachSink(1);
    SdhciMark("WiFi: netif armed\n");
}

void BoardWifiDisarmNetifLinkOutput(void) {
    sAicNetifArmed = 0;
    BoardWifiFeedAttachSink(0);
    HalNetWifiLeavePrimary();
    SdhciMark("WiFi: netif disarmed\n");
}

int BoardWifiCopyStationMacIfArmed(UINT8 OutMac[AIC_NETIF_MAC_LENGTH]) {
    UINT32 Index;

    if (OutMac == 0 || sAicNetifArmed == 0) {
        return -1;
    }
    for (Index = 0u; Index < AIC_NETIF_MAC_LENGTH; Index++) {
        OutMac[Index] = sAicNetifStationMac[Index];
    }
    return 0;
}

UINT32 BoardWifiLinkOutputCount(void) {
    return sAicNetifLinkOutputCount;
}

UINT32 BoardWifiLinkOutputFailCount(void) {
    return sAicNetifLinkOutputFail;
}

int BoardWifiTryLinkOutput(const UINT8 *Frame, UINTN FrameLength) {
    SDHCI_HOST *Host;
    UINT8 VifIndex = 0u;
    UINT8 ApIndex = 0u;

    if (sAicNetifArmed == 0 || Frame == 0) {
        return -1;
    }
    Host = SdhciHostAt(1);
    if (Host == 0) {
        sAicNetifLinkOutputFail++;
        return -1;
    }
    AicJoinLastIndexes(&VifIndex, &ApIndex);
    if (AicWifiDataSend(Host, Frame, (UINT32)FrameLength, VifIndex, ApIndex) != 0) {
        sAicNetifLinkOutputFail++;
        return -1;
    }
    sAicNetifLinkOutputCount++;
    if (sAicNetifOkPrinted == 0) {
        sAicNetifOkPrinted = 1;
        SdhciMark("WiFi: net-lwip-out-ok lwip_out=");
        SdhciMarkDecimal("", sAicNetifLinkOutputCount);
        SdhciMark("\n");
    }
    return 0;
}

/*
 * 从 join 现状武装（供壳命令与 netif-3 的自动武装共用）：
 *   MAC 取 AicNetStationAddress（M1 addr1 → 回退 scan 地址）。
 * 返回 0 = 已武装；-1 = 还没 join（或 MAC 全零）。
 */
int BoardWifiArmNetifFromJoin(void) {
    UINT8 Station[WIFI_NETIF_MAC_LENGTH];

    if (AicNetStationAddress(Station) != 0) {
        return -1;
    }
    BoardWifiArmNetifLinkOutput(Station);
    return BoardWifiLinkOutputIsArmed() != 0 ? 0 : -1;
}
