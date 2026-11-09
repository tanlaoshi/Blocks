/*
 * HalDevicesNet.c — Arm64：Net 门面（PR-S3-haldev-1 / rnet-1）
 *
 * Pi 契约：插线→有线；无缆→Wi‑Fi。主槽见 HalDevicesNetWifiPrimary.c。
 */
#include "Hal.h"
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

void HalNetPollWiredOverWifi(void);
int HalNetPrimaryIsPinned(void);

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

void HalNetPoll(void) {
    HalNetPollWiredOverWifi();
    if (LwIpActive()) {
        LwIpService();
        return;
    }
    DriverNetPoll();
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
        Budget = (HalNetPrimaryIsPinned() != 0) ? 32u : 16u;
#else
        Budget = 16u;
#endif
        BoardWifiPollFeedLwIp(Budget);
    }
#endif
}
