/*
 * LwIp.c — lwIP 初始化与轮询（NO_SYS）（PR-S3-lwip-1）
 *
 * socket / DNS 见 LwIpSocket.c。
 */
#include "LwIp.h"
#include "Hal.h"
#include "Debug.h"
#include "Tcp.h"
#include "Udp.h"
#include "NetConfig.h"
#include "Private.h"
#include "Scheduler.h"
#include "Atomic.h"

#ifdef LWIP_ENABLE

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "Ping.h"
#include "HalDevices.h"
#include "DriverNet.h"

static int gLwIpReady;
/* 软锁：持锁期间保持 IF=1，避免 ping/ARP 时关中断饿死 USB 鼠 */
static volatile UINT32 gLwIpSoft;

/*
 * 墙钟毫秒。必须跟真实时间走：
 * · 旧：每 LwIpService +1 → 忙泵「时间飞逝」/ Halt 慢拍 RTO 虚高；
 * · 曾改 HalCpuTicks：Duo ConsoleOnly 上 tick 不推进 → DHCP 不重发 Discover，
 *   首包 OFFER 若在 Halt 窗口被错过就整段 no offer（私有 wififw dhcp 长轮询能绿）。
 * · 现：HalMilliseconds（RiscV timebase / Arm64 CNTV），与 Dhcp.c 非 x86 超时同钟。
 */
u32_t sys_now(void) {
    return (u32_t)HalMilliseconds();
}

void LwIpLock(void) {
    while (BLOCKS_ATOMIC_TAS(&gLwIpSoft, 1u)) {
        SchedulerIoBreath();
    }
}

void LwIpUnlock(void) {
    BLOCKS_ATOMIC_RELEASE(&gLwIpSoft);
}

int LwIpInitialize(void) {
    if (!HalNetReady()) {
        return -1;
    }
    NetConfigEnsure();
    HalNetSetIpAddress(NetConfigGetIp());
    LwIpLock();
    TcpInitialize();
    UdpInitialize();
    lwip_init();
    if (LwIpConfigBindNetif() != 0) {
        LwIpUnlock();
        return -1;
    }
    LwIpConfigPushDns();
    HalNetSetLwipReceive(1);
    gLwIpReady = 1;
    LwIpUnlock();
    LwIpConfigLogDns();
    return 0;
}

void LwIpPoll(void) {
    if (!gLwIpReady) {
        return;
    }
    if (BLOCKS_ATOMIC_TAS(&gLwIpSoft, 1u)) {
        return;
    }
    sys_check_timeouts();
    BLOCKS_ATOMIC_RELEASE(&gLwIpSoft);
}

/*
 * NO_SYS：Shell 与 Worker 都可能进来。软锁保持 IF=1；
 * 抢不到锁则本拍跳过（调用方会再转），绝不 SpinLock cli。
 *
 * 必须把 NetPoll 也放进同一把锁：否则 Shell 与 HttpGet Worker 并发
 * AlxPoll 会踩 RX 环 → store sync 只发出 GET、收 0 字节（http empty）。
 * 勿调 HalNetPoll（其内部也会抢锁，非递归）。
 */
void LwIpService(void) {
    if (!gLwIpReady) {
        DriverNetPoll();
        return;
    }
    if (BLOCKS_ATOMIC_TAS(&gLwIpSoft, 1u)) {
        SchedulerIoBreath();
        return;
    }
    DriverNetPoll();
    /* Pi WiFi 武装后 F2 DATA→lwIP（对表 SMSC Poll；短喂，勿 Present） */
    HalNetFeedAlternateReceive();
    sys_check_timeouts();
    BLOCKS_ATOMIC_RELEASE(&gLwIpSoft);
}

int LwIpActive(void) {
    return gLwIpReady;
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    return ToyPing(DstIp, TimeoutMs);
}

#else

int LwIpInitialize(void) {
    return -1;
}

void LwIpPoll(void) {
}

void LwIpService(void) {
    HalNetPoll();
}

void LwIpLock(void) {
}

void LwIpUnlock(void) {
}

int LwIpActive(void) {
    return 0;
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    (void)DstIp;
    (void)TimeoutMs;
    return -1;
}

int LwIpTcpListen(UINT16 Port) {
    (void)Port;
    return -1;
}

int LwIpTcpListenStop(void) {
    return -1;
}

UINT16 LwIpTcpListenPort(void) {
    return 0;
}

int LwIpUdpBind(UINT16 Port) {
    (void)Port;
    return -1;
}

UINT16 LwIpUdpBoundPort(void) {
    return 0;
}

int LwIpUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len) {
    (void)DstIp;
    (void)DstPort;
    (void)Data;
    (void)Len;
    return -1;
}

int LwIpUdpRecv(UDP_DATAGRAM *Out) {
    (void)Out;
    return 0;
}

int LwIpTcpConnectSend(UINT32 DstIp, UINT16 DstPort,
                       const void *Data, UINTN Len, int TimeoutMs) {
    (void)DstIp;
    (void)DstPort;
    (void)Data;
    (void)Len;
    (void)TimeoutMs;
    return -1;
}

#endif
