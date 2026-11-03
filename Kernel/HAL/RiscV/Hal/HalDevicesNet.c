/*
 * HalDevicesNet.c — RiscV：Net 门面（PR-S3-haldev-1 / duo-net net-4…6）
 *
 * virt：VirtioNetInitialize → DriverProbeClass(NET)。
 * Duo：板驱动已 Register；此处只 Probe/Bind，不 VirtioNetRegister。
 * net-6：lwIP on 时 Poll → LwIpService（对表 X64）。
 */
#include "Hal.h"
#include "BoardConfig.h"
#include "Driver.h"
#include "DriverNet.h"
#include "LwIp.h"
#include "NetConfig.h"
#include "Netif.h"
/*
 * WifiNetif.h 只在 milk-v-duo-s（BOARD_HAS_WIFI=1）板目录；CI 默认 BOARD=virt 没有该头。
 * 调用点已用 BOARD_HAS_WIFI 包住，include 也必须同条件，否则 riscv virt Actions 必挂。
 */
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
#include "WifiNetif.h"
#endif
#if BOARD_IS_VIRT && BOARD_HAS_NET
#include "Virtio/Net/Net.h"
#endif
#if !BOARD_IS_VIRT && BOARD_HAS_NET
int BoardEthernetGetLink(int *Up, UINT32 *Megabits, int *FullDuplex);
#endif

#if BOARD_HAS_NET && !BOARD_IS_VIRT
static UINT32 gNicEpoch;
/* 对表 X64 Nic.c：0=有线 1=无线；Pin 期间禁止自动切 */
static int gPrimaryKind;
static int gPrimaryPinned;
static UINT32 gSavedWiredIp;
static UINT32 gSavedWiredMask;
static UINT32 gSavedWiredGw;
static UINT32 gSavedWiredDns;
static int gSavedWiredValid;
#endif

int HalNetInitialize(void) {
#if BOARD_HAS_NET
#if BOARD_IS_VIRT
    return VirtioNetInitialize();
#else
    (void)DriverProbeClass(DRIVER_CLASS_NET);
    return 0;
#endif
#else
    return 0;
#endif
}

int HalNetProbeAtModuleInit(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    return 1;
#else
    return 0;
#endif
}

int HalNetReady(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    /*
     * Duo：有线 dwmac 与 Wi‑Fi 均可作 L2。Wi‑Fi 武装后也要放行壳 `net dhcp` /
     * HalNetHasNicL2（对表 Arm64 武装语义；真机已有有线 DHCP）。
     */
    if (BoardWifiLinkOutputIsArmed() != 0) {
        return 1;
    }
#endif
    return DriverNetReady();
}

UINT32 HalNetNicEpoch(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    return gNicEpoch;
#else
    return 0;
#endif
}

void HalNetNicEpochBump(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    gNicEpoch++;
#endif
}

void HalNetPoll(void) {
    if (LwIpActive()) {
        LwIpService();
        return;
    }
    DriverNetPoll();
}

void HalNetGetMacAddress(UINT8 Mac[6]) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    /* dwifi-netif-3：武装后对外就是站点 MAC（lwIP DHCP 的 chaddr 靠它） */
    if (BoardWifiCopyStationMacIfArmed(Mac) == 0) {
        return;
    }
#endif
    DriverNetGetMac(Mac);
}

void HalNetEnsureLwIpActive(void) {
    if (!LwIpActive()) {
        (void)LwIpInitialize();
    }
}

UINT32 HalNetGetIpAddress(void) {
    return DriverNetGetIp();
}

void HalNetSetIpAddress(UINT32 Ip) {
    DriverNetSetIp(Ip);
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    DriverNetFormatIp(Ip, Buf, BufLen);
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    return DriverNetParseIp(Text, Ip);
}

int HalNetPing(const char *Host, int TimeoutMs) {
    return DriverNetPing(Host, TimeoutMs);
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    DriverNetGetStats(TxDone, RxFrames);
}

int HalNetGetLinkInfo(int *Up, UINT32 *Mbps, int *FullDuplex) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    /* 对表 Arm64：武装后链路就是 WiFi，勿被无 PHY 的 dwmac「link down」盖掉 */
    if (BoardWifiLinkOutputIsArmed() != 0) {
        if (Up) {
            *Up = 1;
        }
        if (Mbps) {
            *Mbps = 100u; /* 显示用；WiFi 无 MII 速率 */
        }
        if (FullDuplex) {
            *FullDuplex = 1;
        }
        return 1;
    }
#endif
#if !BOARD_IS_VIRT && BOARD_HAS_NET
    return BoardEthernetGetLink(Up, Mbps, FullDuplex);
#else
    (void)Up;
    (void)Mbps;
    (void)FullDuplex;
    return 0;
#endif
}

int HalNetHasNicL2(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    /* Duo dwmac 等：真 L2，勿走 NetConfig QEMU slirp 默认 10.0.2.15 */
    return HalNetReady() ? 1 : 0;
#else
    return 0;
#endif
}

int HalNetPrimaryKind(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    if (gPrimaryPinned) {
        return gPrimaryKind;
    }
    /*
     * 主槽只跟采纳/离开：未 HalNetWifiAdoptLease 前保持有线（武装不抢 TX）。
     * 已采纳则报 Wi‑Fi，即使瞬时未武装也勿把 Wi‑Fi 址从 dwmac 送出。
     */
    if (gPrimaryKind == 1) {
        return 1;
    }
#endif
    return DriverNetReady() ? 0 : (HalNetReady() ? 0 : -1);
#else
    return -1;
#endif
}

void HalNetPinPrimary(int Pin) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    gPrimaryPinned = Pin ? 1 : 0;
#else
    (void)Pin;
#endif
}

void HalNetWifiAdoptLease(UINT32 Ip, UINT32 Mask, UINT32 Gw, UINT32 Dns) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    if (Ip == 0u) {
        return;
    }
    if (Mask == 0u) {
        Mask = 0xFFFFFF00u; /* /24 */
    }
    /* 第一次切 Wi‑Fi 前记住有线租约，Disarm 后可回切 */
    if (gSavedWiredValid == 0 && gPrimaryKind == 0) {
        gSavedWiredIp = NetConfigGetIp();
        gSavedWiredMask = NetConfigGetMask();
        gSavedWiredGw = NetConfigGetGw();
        gSavedWiredDns = NetConfigGetDns();
        gSavedWiredValid = 1;
    }
    NetConfigAdopt(Ip, Mask, Gw, Dns);
    (void)LwIpApplyConfig();
    if (gPrimaryPinned == 0) {
        gPrimaryKind = 1;
    }
    gNicEpoch++;
    LwIpNetifSyncMac();
#else
    (void)Ip;
    (void)Mask;
    (void)Gw;
    (void)Dns;
#endif
}

void HalNetWifiLeavePrimary(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    if (gPrimaryPinned != 0) {
        return;
    }
    if (gSavedWiredValid != 0 && gSavedWiredIp != 0u) {
        NetConfigAdopt(gSavedWiredIp, gSavedWiredMask, gSavedWiredGw, gSavedWiredDns);
        (void)LwIpApplyConfig();
    }
    gPrimaryKind = 0;
    gNicEpoch++;
    LwIpNetifSyncMac();
#endif
}

void HalNetDumpNicNote(void (*Write)(const char *Text)) {
    if (Write) {
#if !BOARD_IS_VIRT && BOARD_HAS_NET
        Write("ethernet note: cv181x dwmac-3.70a\n");
#else
        Write("e1000 note: n/a (not x86)\n");
#endif
    }
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return DriverNetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return DriverNetChecksum(Data, Len);
}

void HalNetSetLwipReceive(int Enable) {
    DriverNetSetLwIpRx(Enable);
}

void HalNetFeedAlternateReceive(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    /*
     * 对表 Arm64：武装后每拍短喂。DHCP Job 忙时多喂几轮——Discover 发出后
     * OFFER 窗口短，ConsoleOnly 一拍一 Halt 容易空过（见 WifiNetifFeed 预算注释）。
     */
    UINT32 Rounds = LwIpDhcpJobBusy() ? 4u : 1u;
    UINT32 Index;

    for (Index = 0u; Index < Rounds; Index++) {
        if (BoardWifiPollFeedLwIp(8u) == 0 && Index > 0u) {
            break;
        }
    }
#endif
}
