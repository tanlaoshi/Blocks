/*
 * WifiDhcp.c — PR-36：`wififw dhcp [ms]` 的 DHCP 客户端（RFC 2131 §4.3 的最小实现）。
 * 用户眼里：DISCOVER → OFFER → REQUEST → ACK 四行 + `WiFi: dhcp: OK ip=…`，
 *           随后是 `WiFi: arp: reply …`（拿到地址后再问一次网关）。
 * 从哪读：SdhciWifiDhcp（入口）→ DhcpExchange（顺序）→ DhcpRunDiscover / DhcpRunRequest
 *         → DhcpSendAndWait → AicDhcpBuildDiscover / AicDhcpParseReply（WifiDhcpMessage.c）
 *         → AicWifiDataSend（WifiEapolTx.c 的通用以太网 TX）。
 * 别改：收包**复用** WifiEapol.c 的逐帧轮询（AicNetPollRun → AicEapolPollRun），本文件
 *       不碰 SDIO；xid 是"这条应答是不是回给我们的"的唯一判据。只读、可重复执行。
 */
#include "WifiNetPrivate.h"
#include "FileSystem.h"
#include "CString.h"

/* 一次命令的帧缓冲（最大一帧 342 B，512 与 TX 块同档） */
static UINT8 sAicDhcpFrame[AIC_NET_FRAME_MAX];
/* 当前等待的那条应答（sink 在收包路径里写，轮询的 Done 只读） */
static AIC_NET_DHCP_REPLY sAicDhcpReply;
static UINT32 sAicDhcpXid;
static UINT32 sAicDhcpReplyReady;
static UINT32 sAicDhcpWantType;

/* 收包口：xid 对得上才落盘；类型是不是我们在等的由 DhcpReplyDone 判断 */
void AicDhcpHandlePacket(const UINT8 *Payload, UINT32 Length, UINT32 Decryption) {
    AIC_NET_DHCP_REPLY Reply;

    if (sAicDhcpXid == 0u) {
        return; /* dhcp 命令没在跑 */
    }
    if (AicDhcpParseReply(Payload, Length, sAicDhcpXid, Decryption, &Reply) == 0) {
        return;
    }
    sAicDhcpReply = Reply;
    sAicDhcpReplyReady = 1u;
}

/* 等到目标类型就收工；NAK 也算"有结果"，让上层打一行说清楚为什么没拿到地址 */
static int DhcpReplyDone(void) {
    if (sAicDhcpReplyReady == 0u) {
        return 0;
    }
    if (sAicDhcpReply.Type == sAicDhcpWantType) {
        return 1;
    }
    return sAicDhcpReply.Type == AIC_NET_DHCP_MESSAGE_NAK;
}

/* 这一次尝试等多久：剩余预算（25 MHz 裸计数，BOARD_TIMEBASE_HZ）按剩余次数均分（≥1 ms），
   不引入随机定时器。 */
static UINT32 DhcpTryBudgetMs(UINT64 Deadline, UINT32 TriesLeft) {
    UINT64 Now = SdhciReadTime();
    UINT32 Slice;

    if (Now >= Deadline) {
        return 1u;
    }
    Slice = (UINT32)((Deadline - Now) / AIC_NET_TIMEBASE_PER_MS);
    if (TriesLeft != 0u) {
        Slice /= TriesLeft;
    }
    return (Slice == 0u) ? 1u : Slice;
}

static void DhcpMarkIpv4(const char *Tag, const UINT8 *Address) {
    char Text[AIC_NET_IPV4_TEXT_MAX];

    AicNetFormatIpv4(Address, Text);
    SdhciMark(Tag);
    SdhciMark(Text);
}

/*
 * 发同一帧并等应答：每次重发前清掉上一条应答的"就绪"标志（否则会把上一阶段的
 * OFFER 当成这一阶段的 ACK），Done 里用 sAicDhcpWantType 区分。
 */
static int DhcpSendAndWait(SDHCI_HOST *Host, const char *Name, UINT32 Length, UINT32 Tries,
                           UINT8 WantType, UINT64 Deadline, UINT8 VifIndex, UINT8 ApIndex) {
    UINT32 Try;

    for (Try = 0; Try < Tries; Try++) {
        sAicDhcpReplyReady = 0u;
        sAicDhcpWantType = WantType;
        if (AicWifiDataSend(Host, sAicDhcpFrame, Length, VifIndex, ApIndex) != 0) {
            SdhciMark("WiFi: dhcp: ");
            SdhciMark(Name);
            SdhciMark(" tx failed (see the data tx line above)\n");
            return -1;
        }
        SdhciMark("WiFi: dhcp: ");
        SdhciMark(Name);
        SdhciMark(" tx len=");
        SdhciMarkDecimal("", Length);
        SdhciMarkHex(" xid=0x", sAicDhcpXid, 8);
        SdhciMark("\n");
        if (AicNetPollRun(Host, DhcpTryBudgetMs(Deadline, Tries - Try), DhcpReplyDone) == 0) {
            return 0;
        }
        if (Try + 1u >= Tries || SdhciReadTime() >= Deadline) {
            break;
        }
        SdhciMark("WiFi: dhcp: no answer in this window, resending ");
        SdhciMark(Name);
        SdhciMark("\n");
    }
    return -1;
}

/* DHCPDISCOVER（RFC 2131 §4.3.1）→ 等 DHCPOFFER（§4.3.2） */
static int DhcpRunDiscover(SDHCI_HOST *Host, const UINT8 *Station, UINT64 Deadline, UINT8 VifIndex,
                           UINT8 ApIndex) {
    UINT32 Length = AicDhcpBuildDiscover(sAicDhcpFrame, (UINT32)sizeof(sAicDhcpFrame), Station,
                                         sAicDhcpXid);

    if (Length == 0u) {
        SdhciMark("WiFi: dhcp: build DISCOVER failed\n");
        return -1;
    }
    if (DhcpSendAndWait(Host, "DISCOVER", Length, AIC_NET_DHCP_DISCOVER_TRIES,
                        AIC_NET_DHCP_MESSAGE_OFFER, Deadline, VifIndex, ApIndex) != 0) {
        SdhciMark("WiFi: dhcp: FAILED at DISCOVER: no OFFER in budget"
                  " (GTK wrong? check the 'data frame after keys' decr lines)\n");
        return -1;
    }
    if (sAicDhcpReply.Type != AIC_NET_DHCP_MESSAGE_OFFER) {
        SdhciMark("WiFi: dhcp: FAILED at DISCOVER: the server sent a DHCPNAK\n");
        return -1;
    }
    SdhciMark("WiFi: dhcp: OFFER rx yiaddr=");
    DhcpMarkIpv4("", sAicDhcpReply.YourIp);
    SdhciMark(" server=");
    DhcpMarkIpv4("", sAicDhcpReply.ServerIp);
    SdhciMark(" decr=");
    SdhciMarkDecimal("", sAicDhcpReply.Decryption);
    SdhciMark("\n");
    return 0;
}

/* DHCPREQUEST（RFC 2131 §4.3.2：option 50 请求的 IP + option 54 选中的服务器）→ 等 ACK */
static int DhcpRunRequest(SDHCI_HOST *Host, const UINT8 *Station, UINT64 Deadline, UINT8 VifIndex,
                          UINT8 ApIndex) {
    UINT32 Length = AicDhcpBuildRequest(sAicDhcpFrame, (UINT32)sizeof(sAicDhcpFrame), Station,
                                        sAicDhcpXid, sAicDhcpReply.YourIp, sAicDhcpReply.ServerIp);

    if (Length == 0u) {
        SdhciMark("WiFi: dhcp: build REQUEST failed\n");
        return -1;
    }
    if (DhcpSendAndWait(Host, "REQUEST", Length, AIC_NET_DHCP_REQUEST_TRIES,
                        AIC_NET_DHCP_MESSAGE_ACK, Deadline, VifIndex, ApIndex) != 0) {
        SdhciMark("WiFi: dhcp: FAILED at REQUEST: no ACK in budget\n");
        return -1;
    }
    if (sAicDhcpReply.Type != AIC_NET_DHCP_MESSAGE_ACK) {
        SdhciMark("WiFi: dhcp: FAILED at REQUEST: the server sent a DHCPNAK\n");
        return -1;
    }
    SdhciMark("WiFi: dhcp: ACK rx yiaddr=");
    DhcpMarkIpv4("", sAicDhcpReply.YourIp);
    SdhciMark(" mask=");
    if (sAicDhcpReply.HasMask != 0u) {
        DhcpMarkIpv4("", sAicDhcpReply.Mask);
    } else {
        SdhciMark("none");
    }
    SdhciMark(" gw=");
    if (sAicDhcpReply.HasRouter != 0u) {
        DhcpMarkIpv4("", sAicDhcpReply.Router);
    } else {
        SdhciMark("none");
    }
    SdhciMark(" dns=");
    if (sAicDhcpReply.HasDns != 0u) {
        DhcpMarkIpv4("", sAicDhcpReply.Dns);
    } else {
        SdhciMark("none");
    }
    SdhciMark("\n");
    return 0;
}

/* 拿到地址之后的"更强判据"：ARP 问网关。失败只提示，不否定 DHCP 的成功 */
static void DhcpArpGateway(SDHCI_HOST *Host, const UINT8 *Station, UINT8 VifIndex, UINT8 ApIndex) {
    UINT8 GatewayMac[AIC_NET_MAC_LENGTH];

    if (sAicDhcpReply.HasRouter == 0u) {
        SdhciMark("WiFi: dhcp: no router option, skip ARP\n");
        return;
    }
    if (AicArpResolveGateway(Host, Station, sAicDhcpReply.YourIp, sAicDhcpReply.Router, VifIndex,
                             ApIndex, GatewayMac, AIC_NET_ARP_BUDGET_MS) == 0) {
        SdhciMark("WiFi: dhcp: ARP gw ok (both directions work; next = ICMP ping)\n");
        return;
    }
    SdhciMark("WiFi: dhcp: ARP gw failed (DHCP ok, but tx/unicast may be one-way)\n");
}

/* 四步顺序（RFC 2131 §4.4.1 的 INIT → SELECTING → REQUESTING → BOUND 的简化） */
static int DhcpExchange(SDHCI_HOST *Host, const UINT8 *Station, UINT32 BudgetMs, UINT8 VifIndex,
                        UINT8 ApIndex) {
    UINT64 Deadline = SdhciReadTime() + (UINT64)BudgetMs * AIC_NET_TIMEBASE_PER_MS;

    if (DhcpRunDiscover(Host, Station, Deadline, VifIndex, ApIndex) != 0) {
        return -1;
    }
    if (DhcpRunRequest(Host, Station, Deadline, VifIndex, ApIndex) != 0) {
        return -1;
    }
    SdhciMark("WiFi: dhcp: OK ip=");
    DhcpMarkIpv4("", sAicDhcpReply.YourIp);
    SdhciMark("\n");
    /* DHCP 到此为止（xid 清零后迟到的 ACK 重传不会再改 Reply），接着问网关 */
    sAicDhcpXid = 0u;
    DhcpArpGateway(Host, Station, VifIndex, ApIndex);
    return 0;
}

/* 十进制毫秒；0 表示用默认值。与 `wififw eapol poll` 的解析同一套规则 */
static int DhcpParseMilliseconds(const char *Text, UINT32 *OutMilliseconds) {
    UINT32 Value = 0;
    UINT32 Index = 0;

    if (Text == 0 || Text[0] == 0) {
        return -1;
    }
    while (Text[Index] != 0) {
        char One = Text[Index];

        if (One < '0' || One > '9') {
            return -1;
        }
        Value = Value * 10u + (UINT32)(One - '0');
        if (Value > AIC_NET_DHCP_MAX_MS) {
            return -1;
        }
        Index++;
    }
    if (Value == 0u) {
        return -1;
    }
    *OutMilliseconds = Value;
    return 0;
}

/*
 * `wififw dhcp [ms]` 的板级入口。只读、可重复执行：不写 flash、不动 join 的既有状态。
 * 前置：卡已探测（wifiprobe）、join 过且钥匙已装（gAicEapolKeysInstalled）——
 * 没有钥匙时固件不会解密任何数据帧，DHCP 一定收不到应答，这里先说清楚。
 */
/*
 * 文件驱动探针：从 BLOCKS:probe.cfg 读一行 `mcast=<n>`，覆盖组播帧的 txdesc staid。
 * 目的：把"组播用哪个 sta 索引"做成**改文件就换参数**（ksend 写 20 字节 ≈ 2 秒），
 * 不用每试一个值就重刷内核。取值：-1（或没有该行）= 用 ap_idx；否则用给定值。
 */
static void WifiDhcpLoadProbe(void) {
    char Text[64];
    UINTN Got = 0;
    int i;

    if (FileSystemReadFileAt("BLOCKS:probe.cfg", 0, Text, sizeof(Text) - 1u, &Got) != 0 || Got == 0u) {
        return;
    }
    Text[Got] = 0;
    {
        int k;
        for (k = 0; (UINTN)k + 3u < Got; k++) {
            if (Text[k] == 'a' && Text[k + 1] == 'c' && Text[k + 2] == '=') {
                int v = 0; int j = k + 3;
                while (Text[j] >= '0' && Text[j] <= '9') { v = v * 10 + (Text[j] - '0'); j++; }
                AicWifiSetMcastAc((UINT32)v);
            }
            if (Text[k] == 't' && Text[k + 1] == 'i' && Text[k + 2] == 'd' && Text[k + 3] == '=') {
                int v = 0; int j = k + 4;
                while (Text[j] >= '0' && Text[j] <= '9') { v = v * 10 + (Text[j] - '0'); j++; }
                AicWifiSetMcastTid((UINT32)v);
            }
        }
    }
    for (i = 0; (UINTN)i + 6u < Got; i++) {
        if (Text[i] == 'm' && Text[i + 1] == 'c' && Text[i + 2] == 'a' &&
            Text[i + 3] == 's' && Text[i + 4] == 't' && Text[i + 5] == '=') {
            int Value = 0;
            int Sign = 1;
            int k = i + 6;
            if (Text[k] == '-') { Sign = -1; k++; }
            while (Text[k] >= '0' && Text[k] <= '9') { Value = Value * 10 + (Text[k] - '0'); k++; }
            AicWifiSetMcastSta((UINT32)(Sign * Value));
            return;
        }
    }
}

int SdhciWifiDhcp(const char *MsText) {
    SDHCI_HOST *Host;
    UINT8 Station[AIC_NET_MAC_LENGTH];
    UINT8 VifIndex = 0;
    UINT8 ApIndex = 0;
    UINT32 Budget = AIC_NET_DHCP_DEFAULT_MS;
    int Status;

    if (MsText != 0 && DhcpParseMilliseconds(MsText, &Budget) != 0) {
        SdhciMark("WiFi: dhcp: bad ms (decimal 1..30000)\n");
        return -1;
    }
    Host = SdhciHostAt(1);
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: dhcp needs a probed card (run wifiprobe + wififw join first)\n");
        return -1;
    }
    if (AicNetLayoutCheck() != 0) {
        return -1;
    }
    if (gAicEapolKeysInstalled == 0u) {
        SdhciMark("WiFi: dhcp: keys not installed (run 'wififw join <ssid> <password>' first:\n");
        SdhciMark("WiFi: dhcp:  the firmware only decrypts data frames after PTK/GTK are added)\n");
        return -1;
    }
    if (AicNetStationAddress(Station) != 0) {
        SdhciMark("WiFi: dhcp: no station MAC (join did not run in this boot)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: dhcp: vendor prepare failed\n");
        return -1;
    }
    AicJoinLastIndexes(&VifIndex, &ApIndex);
    WifiDhcpLoadProbe();
    SdhciMark("WiFi: dhcp: budget=");
    SdhciMarkDecimal("", Budget);
    SdhciMark("ms vif=");
    SdhciMarkDecimal("", VifIndex);
    SdhciMark(" ap=");
    SdhciMarkDecimal("", ApIndex);
    SdhciMark(" station=");
    AicNetMarkMac("", Station);
    SdhciMark("\n");
    sAicDhcpXid = AicDhcpMakeXid(Station);
    sAicDhcpReplyReady = 0u;
    AicNetStart();
    Status = DhcpExchange(Host, Station, Budget, VifIndex, ApIndex);
    AicNetStop();
    sAicDhcpXid = 0u;
    sAicDhcpReplyReady = 0u;
    return Status;
}
