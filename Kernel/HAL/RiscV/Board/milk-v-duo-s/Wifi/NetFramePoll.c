/*
 * NetFramePoll.c — 解密数据帧分流 + 预算内收包等待
 *
 * 自 NetFrame.c 拆出（行数债）。组帧/字节序仍在 NetFrame.c。
 */
#include "Wifi/NetPrivate.h"
#include "CString.h"

/*
 * 本机 STA MAC：优先 M1 的 addr1（AP 拿它算 PTK），没握过手就退回 MM_ADD_IF vif MAC。
 */
int AicNetStationAddress(UINT8 *OutAddress) {
    const UINT8 *Station = AicHandshakeStationAddress();

    if (Station == 0) {
        Station = AicScanStationAddress();
    }
    if (Station == 0) {
        return -1;
    }
    (void)memcpy(OutAddress, Station, AIC_NET_MAC_LENGTH);
    return 0;
}

/*
 * 解密数据帧分流口：WifiEapolParse.c 在解出 ethertype 之后调这里。
 * 两个处理函数各自看自己的 active 标志。
 */
static void NetEtherSink(UINT16 EtherType, const UINT8 *Payload, UINT32 Length,
                         UINT32 Decryption) {
    if (EtherType == AIC_NET_ETHERTYPE_IPV4) {
        AicDhcpHandlePacket(Payload, Length, Decryption);
        AicIcmpHandlePacket(Payload, Length, Decryption);
        return;
    }
    if (EtherType == AIC_NET_ETHERTYPE_ARP) {
        AicArpHandlePacket(Payload, Length, Decryption);
    }
}

void AicNetStart(void) {
    AicEapolSetEtherSink(NetEtherSink);
}

void AicNetStop(void) {
    AicEapolSetEtherSink(0);
}

/* 整个 dhcp 期间的累计（跨窗口），用于判断「AP 到底给没给我们帧」 */
static UINT32 sAicNetWindowFrames;
static UINT32 sAicNetWindowData;
static UINT32 sAicNetWindowDecrypted;

/*
 * 预算内收包：交给 WifiEapol.c 逐帧轮询。每轮清零计数器（256 帧硬上限），
 * 预算才是真闸门。
 */
int AicNetPollRun(SDHCI_HOST *Host, UINT32 BudgetMs, int (*Done)(void)) {
    static AIC_EAPOL_COUNTERS Counters;
    UINT64 Start = SdhciReadTime();
    UINT64 Budget = (UINT64)BudgetMs * AIC_NET_TIMEBASE_PER_MS;

    for (;;) {
        UINT64 Elapsed = SdhciReadTime() - Start;
        UINT64 Left;
        UINT32 LeftMs;

        if (Done != 0 && Done() != 0) {
            return 0;
        }
        if (Elapsed >= Budget) {
            return -1;
        }
        Left = Budget - Elapsed;
        LeftMs = (UINT32)(Left / AIC_NET_TIMEBASE_PER_MS);
        if (LeftMs == 0u) {
            LeftMs = 1u;
        }
        memset(&Counters, 0, sizeof(Counters));
        if (AicEapolPollRun(Host, LeftMs, &Counters, Done) != 0) {
            return -1;
        }
        sAicNetWindowFrames += Counters.Frames;
        sAicNetWindowData += Counters.DataFrames;
        sAicNetWindowDecrypted += Counters.EncryptedFrames;
        SdhciMark("WiFi: net poll: window frames=");
        SdhciMarkDecimal("", Counters.Frames);
        SdhciMark(" data=");
        SdhciMarkDecimal("", Counters.DataFrames);
        SdhciMark(" decrypted=");
        SdhciMarkDecimal("", Counters.EncryptedFrames);
        SdhciMark(" (total frames=");
        SdhciMarkDecimal("", sAicNetWindowFrames);
        SdhciMark(" data=");
        SdhciMarkDecimal("", sAicNetWindowData);
        SdhciMark(" decrypted=");
        SdhciMarkDecimal("", sAicNetWindowDecrypted);
        SdhciMark(")\n");
    }
}
