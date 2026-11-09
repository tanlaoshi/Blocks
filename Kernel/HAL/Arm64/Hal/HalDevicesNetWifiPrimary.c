/*
 * HalDevicesNetWifiPrimary.c — Arm64：有线/Wi‑Fi 主槽（Adopt / Leave / TX 归属）
 *
 * 对表 X64 Nic 粘滞 + RiscV HalDevicesNetWifiPrimary：假有线不得抢 Wi‑Fi 主槽。
 */
#include "Hal.h"
#include "HalConsole.h"
#include "BoardConfig.h"
#include "DriverNet.h"
#include "LwIp.h"
#include "NetConfig.h"
#include "Netif.h"
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

int HalNetPrimaryIsPinned(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    return gPrimaryPinned;
#else
    return 0;
#endif
}

void HalNetPollWiredOverWifi(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI && BOARD_HAS_NET && !BOARD_IS_VIRT
    /* 插线且未 Pin：退回有线（否证 via wifi → TCP header） */
    if (gPrimaryPinned == 0 && gPrimaryKind == 1 && EthernetLinkIsUp()) {
        HalNetWifiLeavePrimary();
    }
#endif
}

int HalNetPrimaryKind(void) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    if (gPrimaryPinned) {
        return gPrimaryKind;
    }
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

int HalNetWifiShouldOwnTransmit(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI && BOARD_HAS_NET && !BOARD_IS_VIRT
    /*
     * 主槽优先（对表 X64 / RiscV）：已是 Wi‑Fi 主槽则出站走 Wi‑Fi。
     * 真插线：HalNetPollWiredOverWifi 先 LeavePrimary（有线优先），再出站走 SMSC。
     */
    if (gPrimaryKind == 1) {
        return 1;
    }
    if (EthernetLinkIsUp()) {
        return 0;
    }
    if (BoardWifiLinkOutputIsArmed()) {
        return 1;
    }
#endif
    return 0;
}

void HalNetWifiAdoptLease(UINT32 Ip, UINT32 Mask, UINT32 Gw, UINT32 Dns) {
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    if (Ip == 0u) {
        return;
    }
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    if (EthernetLinkIsUp()) {
        HalConsoleWriteSerial("net: adopt skip (wired link up)\n");
        return;
    }
#endif
    if (Mask == 0u) {
        Mask = 0xFFFFFF00u;
    }
    /*
     * 只在有线 link 真 up 且址不是 QEMU slirp 占位时记租约（对表 RiscV）。
     * 否则 LeavePrimary 会打成 10.0.2.15 via wired。
     */
    if (gSavedWiredValid == 0 && gPrimaryKind == 0 && EthernetLinkIsUp()) {
        UINT32 Candidate = NetConfigGetIp();

        if (Candidate != 0u && Candidate != (UINT32)HAL_NET_IP_DEFAULT) {
            gSavedWiredIp = Candidate;
            gSavedWiredMask = NetConfigGetMask();
            gSavedWiredGateway = NetConfigGetGw();
            gSavedWiredDns = NetConfigGetDns();
            gSavedWiredValid = 1;
        }
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
