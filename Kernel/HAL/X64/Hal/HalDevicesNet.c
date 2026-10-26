/*
 * HalDevicesNet.c — x86：Net 门面（PR-S3-haldev-1）
 * HalNetInitialize → NetInitialize（恒 0；见 Drivers/Network/Network.c）。
 */
#include "Hal.h"
#include "DriverNet.h"
#include "Network/Network.h"
#include "E1000.h"
#include "LwIp.h"

int HalNetInitialize(void) {
    return NetInitialize();
}

int HalNetProbeAtModuleInit(void) {
    /* 仍 boot-net-defer：有 Worker + 定时器 IRQ */
    return 0;
}

int HalNetReady(void) {
    return DriverNetReady();
}

UINT32 HalNetNicEpoch(void) {
    return NetNicEpoch();
}

void HalNetNicEpochBump(void) {
    /* X64：epoch 在 Nic.c ApplyPrimary；板驱勿重复 +1 */
}

void HalNetPoll(void) {
    /*
     * lwIP on：走 LwIpService（try_lock + poll），勿再 LwIpLock 阻塞。
     * 阻塞锁 + SchedulerIoBreath 会与 Worker 持锁 Send 互等，表现为
     * sync:catalog 无串口、宿主收不到 GET。
     */
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

/* PR-N-nic：链路走已挂 NIC_L2（e1000 等）；virtio / 无 L2 → 0 */
int HalNetGetLinkInfo(int *Up, UINT32 *Mbps, int *FullDuplex) {
    if (NetNicGetLink(Up, Mbps, FullDuplex) != 0) {
        return 0;
    }
    return 1;
}

int HalNetHasNicL2(void) {
    return NetNicHasL2();
}

int HalNetPrimaryKind(void) {
    return NetNicPrimaryKind();
}

void HalNetDumpNicNote(void (*Write)(const char *Text)) {
    E1000DumpNote(Write);
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
