/*
 * HalDevicesNet.c — Arm64：Net 门面（PR-S3-haldev-1 / rnet-1）
 *
 * virt：VirtioNetInitialize。
 * Pi：LAN9514 smsc95xx（BOARD_HAS_NET）；USB 枚举在 DWC2 hub 扫口。
 * lwIP on：Poll → LwIpService（对表 X64 / RiscV）。鼠优先见 EthPoll。
 */
#include "Hal.h"
#include "BoardConfig.h"
#include "Driver.h"
#include "DriverNet.h"
#include "LwIp.h"
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
    /* Pi：CNTV poll、Worker 可能饿死；LAN9514 已在 DWC2 hub 扫完 */
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
#else
    /* virtio / 无板网：无外置 L2 epoch */
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
    /* 7d：武装后 SyncMac/DHCP SA 须用站点 MAC，勿回 SMSC */
    if (BoardWifiCopyStationMacIfArmed(Mac) == 0) {
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
    /* Pi LAN9514 等：真 L2，勿走 NetConfig QEMU slirp 默认 10.0.2.15 */
    return HalNetReady() ? 1 : 0;
#else
    return 0;
#endif
}

int HalNetPrimaryKind(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    if (BoardWifiLinkOutputIsArmed()) {
        return 1; /* 无线出站 */
    }
#endif
#if BOARD_HAS_NET && !BOARD_IS_VIRT
    return HalNetReady() ? 0 : -1; /* 0=有线 */
#else
    return -1;
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
    DriverNetSetLwIpRx(Enable);
}

void HalNetFeedAlternateReceive(void) {
#if defined(BOARD_HAS_WIFI) && BOARD_HAS_WIFI
    BoardWifiPollFeedLwIp(8u);
#endif
}
