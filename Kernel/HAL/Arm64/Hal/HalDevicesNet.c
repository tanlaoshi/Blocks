/*
 * HalDevicesNet.c — Arm64：Net 门面（PR-S3-haldev-1 / rnet-1）
 *
 * Pi 契约（2026-11-03）：插线→有线；无缆→Wi‑Fi（Adopt/探网武装）。
 * 插线 LeavePrimary；Adopt 时若 link up 则拒绝。
 */
#include "Hal.h"
#include "HalConsole.h"
#include "BoardConfig.h"
#include "Driver.h"
#include "DriverNet.h"
#include "LwIp.h"
#include "NetConfig.h"
#include "Netif.h"
#if BOARD_IS_VIRT && BOARD_HAS_NET
#include "Virtio/Net/Net.h"
#endif
#if !BOARD_IS_VIRT && BOARD_HAS_NET
int BoardEthernetGetLink(int *Up, UINT32 *Megabits, int *FullDuplex);
#endif
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
#include "Wifi.h"
#endif

#if BOARD_HAS_NET && !BOARD_IS_VIRT
static UINT32 gNicEpoch;
static int gPrimaryKind;
static int gPrimaryPinned;
static UINT32 gSavedWiredIp;
static UINT32 gSavedWiredMask;
static UINT32 gSavedWiredGateway;
static UINT32 gSavedWiredDns;
static int gSavedWiredValid;

static int EthernetLinkIsUp(void) {
    int LinkUp = 0;

    return (BoardEthernetGetLink(&LinkUp, 0, 0) != 0 && LinkUp != 0) ? 1 : 0;
}
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

int HalNetProbeAtModuleInitialize(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    return 1;
#else
    return 0;
#endif
}

int HalNetReady(void) {
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
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI && BOARD_HAS_NET && !BOARD_IS_VIRT
    /* 插线且未 Pin：退回有线（否证 via wifi → TCP header） */
    if (gPrimaryPinned == 0 && gPrimaryKind == 1 && EthernetLinkIsUp()) {
        HalNetWifiLeavePrimary();
    }
#endif
    if (LwIpActive()) {
        LwIpService();
        return;
    }
    DriverNetPoll();
}

int HalNetWifiShouldOwnTransmit(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI && BOARD_HAS_NET && !BOARD_IS_VIRT
    /* 有线 link up：绝对 SMSC（互斥） */
    if (EthernetLinkIsUp()) {
        return 0;
    }
    if (gPrimaryKind == 1) {
        return 1;
    }
    /* 无缆探网：已武装、尚未 Adopt */
    if (BoardWifiLinkOutputIsArmed()) {
        return 1;
    }
#endif
    return 0;
}

void HalNetGetMacAddress(UINT8 Mac[6]) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    if (HalNetWifiShouldOwnTransmit() &&
        BoardWifiCopyStationMacIfArmed(Mac) == 0) {
        return;
    }
#endif
    DriverNetGetMac(Mac);
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
    if (HalNetPrimaryKind() == 1) {
        if (Up) {
            *Up = 1;
        }
        if (Mbps) {
            *Mbps = 0;
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
    /* 未 AdoptLease 前保持有线（武装不抢 TX / ip） */
    if (gPrimaryKind == 1) {
        return 1;
    }
#endif
    return HalNetReady() ? 0 : -1;
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
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    /* 插线不 Adopt：热更/IP 走有线 */
    if (EthernetLinkIsUp()) {
        HalConsoleWriteSerial("net: adopt skip (wired link up)\n");
        return;
    }
#endif
    if (Mask == 0u) {
        Mask = 0xFFFFFF00u;
    }
    if (gSavedWiredValid == 0 && gPrimaryKind == 0) {
        gSavedWiredIp = NetConfigGetIp();
        gSavedWiredMask = NetConfigGetMask();
        gSavedWiredGateway = NetConfigGetGw();
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
        NetConfigAdopt(gSavedWiredIp, gSavedWiredMask, gSavedWiredGateway,
                       gSavedWiredDns);
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
        Write("ethernet note: board MAC\n");
#elif !BOARD_IS_VIRT
        Write("ethernet note: smsc95xx LAN9514 USB\n");
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
    DriverNetSetLwIpReceive(Enable);
}

void HalNetFeedAlternateReceive(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    /*
     * 武装后每拍抽 DATA→lwIP。serve/Pin 时加大：表后首块 4KB 突发曾因
     * 事件占满 8 槽喂不进 → reason=header（Pi 2026-11-03；1 轮否证）。
     */
    UINT32 Budget = 8u;

    if (BoardWifiLinkOutputIsArmed()) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
        Budget = (gPrimaryPinned != 0) ? 32u : 16u;
#else
        Budget = 16u;
#endif
        BoardWifiPollFeedLwIp(Budget);
    }
#endif
}
