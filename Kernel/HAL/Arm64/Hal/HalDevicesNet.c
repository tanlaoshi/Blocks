/*
 * HalDevicesNet.c — Arm64：Net 门面（PR-S3-haldev-1）
 */
#include "Hal.h"
#include "DriverNet.h"
#include "Virtio/Net/Net.h"

int HalNetInitialize(void) {
    return VirtioNetInitialize();
}

int HalNetReady(void) {
    return DriverNetReady();
}

UINT32 HalNetNicEpoch(void) {
    return 0;
}

void HalNetPoll(void) {
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
    (void)Up;
    (void)Mbps;
    (void)FullDuplex;
    return 0;
}

int HalNetHasNicL2(void) {
    return 0;
}

int HalNetPrimaryKind(void) {
    return -1;
}

void HalNetDumpNicNote(void (*Write)(const char *Text)) {
    if (Write) {
        Write("e1000 note: n/a (not x86)\n");
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
