/*
 * WifiNetifFeed.c — dwifi-netif-2：AIC 解密数据帧 → LwIpNetifInput（Duo）
 *
 * 对表 Pi：BoardWifiPollFeedLwIp（HAL/Arm64/Board/rpi-3b/Wifi/WifiNetLwIpFeed.c）
 *
 * 通路：HalNetFeedAlternateReceive（每拍）→ BoardWifiPollFeedLwIp(8)
 *       → AicEapolPollRun（复用握手那条逐帧轮询）→ EapolDispatchFrameSink（整帧）
 *       → 本文件 NetifFrameSink → LwIpNetifInput
 *
 * 为什么走"整帧 sink"而不是 EtherSink：LwIpNetifInput 要 dst/src/type 全都对，
 * 而 EtherSink 只给 L3 载荷；整帧拼装在 WifiEapolEther.c 里完成。
 *
 * 三条铁律（评审红线）：
 *   ① 未武装不喂 —— 否则会把 AP 的广播当成本机网卡的数据；
 *   ② 每拍限量（≤8 包，对表 Pi）—— 喂太多会把壳饿死；
 *   ③ 喂的时候开"安静窗口"（SdhciMarkSetQuiet）—— EAPOL 解码每帧都打证据行，
 *      不静音每拍刷屏，壳直接卡死。
 *
 * 握手期为何安全：join/handshake 自己调 AicEapolPollRun，且此处要求"已武装"才喂；
 * 武装发生在装钥之后（netif-3），所以 EAPOL 阶段不会与本文件抢帧。
 */
#include "WifiNetif.h"
#include "WifiEapolPrivate.h"
#include "Sdhci.h"
#include "Private.h"
#include "Netif.h"
#include "CString.h"

#define AIC_NETIF_FEED_MAX_PACKETS 8u
/*
 * 每拍 SDIO 轮询预算。2ms 太短；DHCP Job 期间由 HalNetFeedAlternateReceive
 * 多轮调用。100ms 对齐「Discover 后立刻有机会捞到 OFFER」，仍远短于私有 8s。
 */
#define AIC_NETIF_FEED_BUDGET_MS   100u
#define AIC_NETIF_FEED_FRAME_MIN   14u

static UINT32 sAicNetifFeedLimit;
static UINT32 sAicNetifFeedThisPoll;
static UINT32 sAicNetifFeedTotal;
static UINT32 sAicNetifFeedDropShort;
static int sAicNetifFeedOkPrinted;

static int NetifFeedDone(void) {
    return (sAicNetifFeedThisPoll >= sAicNetifFeedLimit) ? 1 : 0;
}

static void NetifFrameSink(const UINT8 *Frame, UINT32 Length, UINT32 Decryption) {
    /* 双保险：sink 由 Arm/Disarm 挂摘，这里再查一次，Disarm 之后绝不喂 */
    if (BoardWifiLinkOutputIsArmed() == 0) {
        return;
    }
    if (Frame == 0 || Length < AIC_NETIF_FEED_FRAME_MIN) {
        sAicNetifFeedDropShort++;
        return;
    }
    (void)Decryption;
    LwIpNetifInput(Frame, (UINTN)Length);
    sAicNetifFeedTotal++;
    sAicNetifFeedThisPoll++;
    if (sAicNetifFeedOkPrinted == 0) {
        /*
         * 判据标签必须在"第一帧真的喂进 lwIP"的这一刻打印：
         * 此时通常处于安静窗口内（喂包轮询），所以临时开窗打印再关回去。
         */
        sAicNetifFeedOkPrinted = 1;
        SdhciMarkSetQuiet(0);
        SdhciMark("WiFi: net-lwip-in-ok lwip_in=");
        SdhciMarkDecimal("", sAicNetifFeedTotal);
        SdhciMark("\n");
        SdhciMarkSetQuiet(1);
    }
}

int BoardWifiPollFeedLwIp(UINT32 MaxPackets) {
    AIC_EAPOL_COUNTERS Counters;

    if (BoardWifiLinkOutputIsArmed() == 0) {
        return 0;
    }
    sAicNetifFeedLimit = (MaxPackets == 0u) ? AIC_NETIF_FEED_MAX_PACKETS : MaxPackets;
    sAicNetifFeedThisPoll = 0u;
    (void)memset(&Counters, 0, sizeof(Counters));
    /*
     * 不在这里挂 sink：sink 的生命周期 = 武装生命周期（Arm 挂、Disarm 摘），
     * 否则 Disarm 之后残留的 sink 仍会把帧喂进 lwIP。
     */
    SdhciMarkSetQuiet(1);
    (void)AicEapolPollRun(SdhciHostAt(1), AIC_NETIF_FEED_BUDGET_MS, &Counters, NetifFeedDone);
    SdhciMarkSetQuiet(0);
    return (int)sAicNetifFeedThisPoll;
}

UINT32 BoardWifiFeedCount(void) {
    return sAicNetifFeedTotal;
}

UINT32 BoardWifiFeedDropCount(void) {
    return sAicNetifFeedDropShort;
}

void BoardWifiFeedReset(void) {
    sAicNetifFeedTotal = 0u;
    sAicNetifFeedThisPoll = 0u;
    sAicNetifFeedDropShort = 0u;
    sAicNetifFeedOkPrinted = 0;
}

/* 武装/解除时由 WifiNetifLinkOutput 调：sink 常驻与否完全跟随武装状态 */
void BoardWifiFeedAttachSink(int Enable) {
    if (Enable != 0) {
        sAicNetifFeedOkPrinted = 0;
        AicEapolSetFrameSink(NetifFrameSink);
        return;
    }
    AicEapolSetFrameSink(0);
}
