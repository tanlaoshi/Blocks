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
     * dwifi-netif-4：Duo S 没有有线 PHY，DriverNet 后端 Probe 不过 → DriverNetReady() 恒 0，
     * 壳 `net dhcp` 会在入口就拒绝。WiFi 武装后它本身就是可用网卡，故这里放行。
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
    return HalNetReady() ? 0 : -1;
#else
    return -1;
#endif
}

void HalNetPinPrimary(int Pin) {
    (void)Pin;
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
