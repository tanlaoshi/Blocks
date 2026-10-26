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
    return 0;
}

void HalNetPoll(void) {
    if (LwIpActive()) {
        LwIpService();
        return;
    }
    DriverNetPoll();
}

void HalNetGetMacAddress(UINT8 Mac[6]) {
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
    return 0;
}

int HalNetPrimaryKind(void) {
    return -1;
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
