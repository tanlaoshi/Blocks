/*
 * HalDevicesNetWifiPrimary.c — RiscV：有线/Wi‑Fi 主槽（Adopt / Leave / TX 归属）
 *
 * 对表 X64 Nic 粘滞 + Arm64 HalDevicesNetWifiPrimary：假有线不得抢 Wi‑Fi 主槽。
 */
#include "Hal.h"
#include "BoardConfig.h"
#include "DriverNet.h"
#include "LwIp.h"
#include "NetConfig.h"
#include "Netif.h"
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
#include "WifiNetif.h"
#endif
#if !BOARD_IS_VIRT && BOARD_HAS_NET
int BoardEthernetGetLink(int *Up, UINT32 *Megabits, int *FullDuplex);
#endif

#if BOARD_HAS_NET && !BOARD_IS_VIRT
static int gPrimaryKind;
static int gPrimaryPinned;
static UINT32 gSavedWiredIp;
static UINT32 gSavedWiredMask;
static UINT32 gSavedWiredGw;
static UINT32 gSavedWiredDns;
static int gSavedWiredValid;
static UINT32 gNicEpoch;

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

void HalNetPollWiredOverWifi(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI && BOARD_HAS_NET && !BOARD_IS_VIRT
    /*
     * 插真网线 → 退回有线（对表 Arm64 / X64「有线优先」）。
     * 须 BoardEthernetGetLink 真实；假 link 会把 Wi‑Fi 主槽打成 via wired。
     */
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

int HalNetWifiShouldOwnTransmit(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI && BOARD_HAS_NET && !BOARD_IS_VIRT
    /*
     * 主槽优先（对表 X64 ApplyPrimary）：已是 Wi‑Fi 主槽则出站走 Wi‑Fi，
     * 勿被瞬时假有线 link 抢走 TX（否则 serve 回程黑洞）。
     * 真插线：HalNetPollWiredOverWifi 先 LeavePrimary 再出站。
     */
    if (gPrimaryKind == 1) {
        return 1;
    }
    if (EthernetLinkIsUp()) {
        return 0;
    }
    if (BoardWifiLinkOutputIsArmed() != 0) {
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
        return;
    }
#endif
    if (Mask == 0u) {
        Mask = 0xFFFFFF00u; /* /24 */
    }
    /*
     * 只在有线 link 真 up 且已有非零租约时记一份，供拔线回切。
     * 真机驱动/NetConfig 初值须为 0；QEMU 10.0.2.15 不得出现在本路径
     * （见 NetConfig ApplyQemuSlirp / Duo·X64 gIp 初值）。
     */
    if (gSavedWiredValid == 0 && gPrimaryKind == 0 && EthernetLinkIsUp()) {
        UINT32 WiredLeaseIp = NetConfigGetIp();

        if (WiredLeaseIp != 0u) {
            gSavedWiredIp = WiredLeaseIp;
            gSavedWiredMask = NetConfigGetMask();
            gSavedWiredGw = NetConfigGetGw();
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
        NetConfigAdopt(gSavedWiredIp, gSavedWiredMask, gSavedWiredGw, gSavedWiredDns);
        (void)LwIpApplyConfig();
    }
    gPrimaryKind = 0;
    gNicEpoch++;
    LwIpNetifSyncMac();
#endif
}
