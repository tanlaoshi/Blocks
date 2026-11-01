/*
 * WifiEapolTx.c — PR-34/36：数据通道 TX（EAPOL 的 M2/M4 + 任意以太网帧）
 *
 * 用户眼里：`WiFi: eapol tx: fc=.. padded=.. blocks=..`（M2/M4）与
 *           `WiFi: data tx: len=.. ethertype=0x.. dst=..`（DHCP/ARP，PR-36）。
 * 从哪读：AicEapolSend（EAPOL 帧，ethertype 固定 0x888e）/ AicWifiDataSend（通用以太网帧，
 *         从帧里取 dst/src/ethertype）→ BuildHostDescriptor（4 B 帧头 + 28 B txdesc）
 *         → AicWifiDataPathWrite（WifiDataPathWrite.c：流控 + 4 B 对齐 + TAIL + 512 对齐块写）。
 * 别改：线上格式是厂商 aicwf_sdio.c:1530-1550（aggr 拼帧）+ :1370-1420（xmit）+
 *       rwnx_tx.c:1497-1530（txdesc 字段）的字节契约：
 *         [4 B sdio hdr][28 B txdesc_api][payload][补 0 到 4][补 4 B TAIL][补到 512]
 *       4 B 帧头 = len(12bit = payload+28) | type=0x01(data) | crc8；
 *       payload **不含 14 B 以太网头**（厂商 rwnx_tx.c:1267 skb_pull(skb, 14)，地址与
 *       ethertype 在 txdesc 里，802.11 头与 LLC/SNAP 由固件补）。
 * 与消息通道的差别（别混）：消息是 16 B lmac_msg 头 + 参数，走 AicSendMessageEx；
 *       数据帧是 txdesc + 载荷，走这里。两条路都用 fn1 0x10 写 FIFO，块模式 512 B。
 */
#include "WifiNetPrivate.h"
#include "CString.h"

/* aicwf_sdio.c:1536-1546：sdio_header 4 B，hdr[2]=0x01 就是"数据帧" */
#define AIC_EAPOL_TX_HEADER_LENGTH 4u
#define AIC_EAPOL_TX_TYPE_DATA 0x01u
/*:1540 帧头 len 只占 12 bit（高 4 bit 空） */
#define AIC_EAPOL_TX_LENGTH_MASK 0x0FFFu
/* sizeof(struct txdesc_api) = 28：宿主 gcc include 厂商 ipc_shared.h 编译实测（报告 §3） */
#define AIC_EAPOL_TX_DESC_LENGTH 28u
/* 一帧 M2 只有 153 B（4+28+121）；缓冲上限一个 SDIO 块（对齐/块写在 WifiDataPathWrite.c） */
#define AIC_EAPOL_TX_MAX 512u
#define AIC_EAPOL_TX_PAYLOAD_MAX \
    (AIC_EAPOL_TX_MAX - AIC_EAPOL_TX_HEADER_LENGTH - AIC_EAPOL_TX_DESC_LENGTH)
/*
 * struct hostdesc / txdesc_api 的字段偏移（宿主 gcc include 厂商 ipc_shared.h，
 * sizeof(hostdesc)=28，报告 §3）：packet_len@0 flags_ext@2 hostid@4 eth_dest@8
 * eth_src@14 ethertype@20 ac@22 tid@23 vif_idx@24 staid@25 flags@26。
 */
#define AIC_TX_DESC_PACKET_LENGTH 0u
#define AIC_TX_DESC_FLAGS_EXT 2u
#define AIC_TX_DESC_HOSTID 4u
#define AIC_TX_DESC_ETH_DEST 8u
#define AIC_TX_DESC_ETH_SRC 14u
#define AIC_TX_DESC_ETHERTYPE 20u
#define AIC_TX_DESC_AC 22u
#define AIC_TX_DESC_TID 23u
#define AIC_TX_DESC_VIF_INDEX 24u
#define AIC_TX_DESC_STA_INDEX 25u
/* 厂商"未知 sta"约定：广播/组播帧上不了空口时用它（真机 DHCP DISCOVER 踩过） */
#define AIC_TX_DESC_STA_INDEX_UNKNOWN 0xFFu
/* hostdesc.tid 注释："Packet TID (0xFF if not a QoS frame)"；Linux 的 DHCP 广播就是非 QoS */
#define AIC_TX_TID_NON_QOS 0xFFu
/* 组播/广播用的 BCMC 站索引基数：厂商 Makefile:199 -DNX_REMOTE_STA_MAX=32，加 vif_idx */
#define AIC_TX_DESC_BCMC_STA_BASE 32u
/* 组播 staid 覆盖值：AUTO = 跟随 ApIndex（默认），其它值 = 强制 */
#define AIC_TX_MCAST_STA_AUTO 0xFFFFFFFFu
static UINT32 sAicMcastStaOverride = AIC_TX_MCAST_STA_AUTO;
static UINT32 sAicMcastAcOverride = AIC_TX_MCAST_STA_AUTO;
static UINT32 sAicMcastTidOverride = AIC_TX_MCAST_STA_AUTO;
/* 0=hostid 保持 0；非 0=写 (1<<31)|1，对表厂商 SDIO txdesc */
static UINT32 sAicHostIdMode;

void AicWifiSetMcastAc(UINT32 Value) { sAicMcastAcOverride = Value; }
void AicWifiSetMcastTid(UINT32 Value) { sAicMcastTidOverride = Value; }

void AicWifiSetMcastSta(UINT32 Value) {
    sAicMcastStaOverride = (Value == AIC_TX_MCAST_STA_AUTO) ? AIC_TX_MCAST_STA_AUTO : Value;
    SdhciMarkHex("WiFi: probe: multicast staid -> ", Value, 2);
    SdhciMark("\n");
}

void AicWifiSetHostIdMode(UINT32 Value) {
    sAicHostIdMode = Value;
    SdhciMark("WiFi: probe: hostid mode=");
    SdhciMarkDecimal("", sAicHostIdMode);
    SdhciMark("\n");
}
#define AIC_TX_DESC_HOST_ID 4u
#define AIC_TX_DESC_FLAGS 26u
#define AIC_TX_HOST_ID_SDIO_STYLE 0x80000001u
/*
 * hw queue 编号（rwnx_tx.h:19-22 RWNX_HWQ_BK/BE/VI/VO）。厂商把 txq->hwq->id 直接写进
 * host.ac（rwnx_tx.c:608 "use ac field for hw_txq"）；TID 0 映射到 BE
 * （rwnx_main.c:480-486 的 rwnx_tid2hwq[0]），所以 EAPOL 用 BE=1、tid=0。
 */
#define AIC_TX_HWQ_BE 1u
#define AIC_TX_TID_BE 0u

/*
 * 日志前缀：同一条 TX 路径被两种帧共用，前缀跟着调用者走。
 * `eapol tx` 是 PR-34 的既有判据（join 的输出不能变），`data tx` 是 PR-36 的判据。
 */
#define AIC_TX_LOG_EAPOL "eapol tx"
#define AIC_TX_LOG_DATA "data tx"

/* 以太网头在完整帧里的位置（RFC 894 §2）：dst[6] src[6] ethertype[2] */
#define AIC_TX_ETHERTYPE_OFFSET 12u

/* 4+28+121 = 153 B；UINT32 数组保证 4 B 对齐（txdesc 里有 u32 字段） */
static UINT32 sAicEapolTxBuffer[(AIC_EAPOL_TX_MAX + 3u) / 4u];

/*
 * 4 B sdio 帧头 + 28 B txdesc。len 字段 = payload + 28（厂商 :1540 把 txdesc 算进长度）；
 * ethertype 按**网络字节序**写在内存里（厂商 rwnx_tx.c:1298 直接抄 eth_t.h_proto，
 * 那 2 B 是大端），所以先写高字节再写低字节 —— 写成小端的话固件看见的就是另一个值。
 * EtherType 由调用者给：EAPOL 固定 0x888e（PR-34），DHCP/ARP 从以太网头里取（PR-36）。
 */
static void BuildHostDescriptor(UINT8 *Buffer, const UINT8 *Destination, const UINT8 *Source,
                              UINT16 EtherType, UINT8 VifIndex, UINT8 ApIndex,
                              UINT32 PayloadLength) {
    UINT8 *Desc = Buffer + AIC_EAPOL_TX_HEADER_LENGTH;
    UINT32 Body = AIC_EAPOL_TX_DESC_LENGTH + PayloadLength;

    Buffer[0] = (UINT8)(Body & 0xFFu);
    Buffer[1] = (UINT8)((Body >> 8) & (AIC_EAPOL_TX_LENGTH_MASK >> 8));
    Buffer[2] = (UINT8)AIC_EAPOL_TX_TYPE_DATA;
    Buffer[3] = AicCrc8(Buffer, 3u);
    AicPut16(Desc + AIC_TX_DESC_PACKET_LENGTH, (UINT16)PayloadLength);
    AicPut16(Desc + AIC_TX_DESC_FLAGS_EXT, 0u);
    /*
     * hostid：默认 0。probe hostid=1 时写厂商 SDIO 形 (1<<31)|idx（rwnx_tx.c:642），
     * 证伪「hostid=0 导致固件丢加密数据帧」。
     */
    if (sAicHostIdMode != 0u) {
        Desc[AIC_TX_DESC_HOST_ID] = (UINT8)(AIC_TX_HOST_ID_SDIO_STYLE & 0xFFu);
        Desc[AIC_TX_DESC_HOST_ID + 1u] =
            (UINT8)((AIC_TX_HOST_ID_SDIO_STYLE >> 8) & 0xFFu);
        Desc[AIC_TX_DESC_HOST_ID + 2u] =
            (UINT8)((AIC_TX_HOST_ID_SDIO_STYLE >> 16) & 0xFFu);
        Desc[AIC_TX_DESC_HOST_ID + 3u] =
            (UINT8)((AIC_TX_HOST_ID_SDIO_STYLE >> 24) & 0xFFu);
    }
    (void)memcpy(Desc + AIC_TX_DESC_ETH_DEST, Destination, WIFI_MAC_ADDRESS_LENGTH);
    (void)memcpy(Desc + AIC_TX_DESC_ETH_SRC, Source, WIFI_MAC_ADDRESS_LENGTH);
    Desc[AIC_TX_DESC_ETHERTYPE] = (UINT8)(EtherType >> 8);
    Desc[AIC_TX_DESC_ETHERTYPE + 1u] = (UINT8)(EtherType & 0xFFu);
    Desc[AIC_TX_DESC_AC] = (sAicMcastAcOverride == AIC_TX_MCAST_STA_AUTO)
                               ? (UINT8)AIC_TX_HWQ_BE
                               : (UINT8)sAicMcastAcOverride;
    Desc[AIC_TX_DESC_TID] = (sAicMcastTidOverride == AIC_TX_MCAST_STA_AUTO)
                                ? (UINT8)AIC_TX_TID_BE
                                : (UINT8)sAicMcastTidOverride;
    Desc[AIC_TX_DESC_VIF_INDEX] = VifIndex;
    /*
     * 厂商 rwnx_tx.c · NL80211_IFTYPE_STATION：单播与组播一律 sta = vif->sta.ap
     * （ap_idx），**不**走 AP/mesh 的 BCMC（32+vif）。GTK 是 sta_idx=0xFF 的默认钥，
     * 由固件按 DA 组播选钥；hostdesc.staid 仍填 ap_idx。真机：ap_idx 能上空气但
     * 空口 KeyID=0（Docs/14）；probe.cfg 的 mcast= 仅作证伪覆盖，默认 AUTO=ap_idx。
     */
    if ((Destination[0] & 0x01u) != 0u) {
        Desc[AIC_TX_DESC_STA_INDEX] = (sAicMcastStaOverride == AIC_TX_MCAST_STA_AUTO)
                                           ? ApIndex
                                           : (UINT8)sAicMcastStaOverride;
    } else {
        Desc[AIC_TX_DESC_STA_INDEX] = ApIndex;
    }
    AicPut16(Desc + AIC_TX_DESC_FLAGS, 0u);
}

/*
 * EAPOL 入口：Eapol 是 121 B 的 M2（不含以太网头），Destination = AP 的 BSSID，
 * Source = 本机 STA MAC（必须与 PTK 里用的一致），ApIndex = SM_CONNECT_IND.ap_idx。
 * 返回 0 只代表"这一帧写进了 SDIO FIFO"，**不代表 AP 收到**——收到与否看后面的 M3。
 */
int AicEapolSend(SDHCI_HOST *Host, const UINT8 *Eapol, UINT32 Length, const UINT8 *Destination,
                 const UINT8 *Source, UINT8 VifIndex, UINT8 ApIndex) {
    UINT8 *Buffer = (UINT8 *)sAicEapolTxBuffer;
    UINT32 Header = AIC_EAPOL_TX_HEADER_LENGTH + AIC_EAPOL_TX_DESC_LENGTH;
    UINT32 Index;

    if (Length == 0u || Length > AIC_EAPOL_TX_PAYLOAD_MAX) {
        SdhciMarkHex("WiFi: eapol tx: bad payload length=", Length, 4);
        SdhciMark("\n");
        return -1;
    }
    for (Index = 0; Index < Header; Index++) {
        Buffer[Index] = 0;
    }
    BuildHostDescriptor(Buffer, Destination, Source, AIC_ETHERTYPE_PAE, VifIndex, ApIndex,
                        Length);
    for (Index = 0; Index < Length; Index++) {
        Buffer[Header + Index] = Eapol[Index];
    }
    return AicWifiDataPathWrite(Host, Buffer, Header + Length, AIC_TX_LOG_EAPOL,
                                "WiFi: eapol tx block write FAIL");
}

/*
 * PR-36 的通用入口：输入一整个以太网帧（dst[6] src[6] ethertype[2] payload，含 60 B 下限），
 * txdesc 里的 packet_len/dest/src/ethertype 全部从帧里取，payload = 帧去掉 14 B 以太网头。
 * 802.11 头与 LLC/SNAP 由固件补（与 EAPOL 同一条契约），所以这里**不碰** 802.11。
 * 判据：`WiFi: data tx: len=… ethertype=0x… dst=…` + 紧随的 SDIO 写返回 0。
 */
int AicWifiDataSend(SDHCI_HOST *Host, const UINT8 *Frame, UINT32 Length, UINT8 VifIndex,
                    UINT8 ApIndex) {
    UINT8 *Buffer = (UINT8 *)sAicEapolTxBuffer;
    UINT32 Header = AIC_EAPOL_TX_HEADER_LENGTH + AIC_EAPOL_TX_DESC_LENGTH;
    UINT32 Body;
    UINT32 Index;
    UINT16 EtherType;
    char Destination[AIC_SCAN_MAC_TEXT_MAX];

    if (Frame == 0 || Length <= AIC_NET_ETHERNET_HEADER_LENGTH ||
        Length > AIC_NET_ETHERNET_HEADER_LENGTH + AIC_EAPOL_TX_PAYLOAD_MAX) {
        SdhciMarkHex("WiFi: data tx: bad frame length=", Length, 4);
        SdhciMark("\n");
        return -1;
    }
    Body = Length - AIC_NET_ETHERNET_HEADER_LENGTH;
    EtherType = AicNetGetBe16(Frame + AIC_TX_ETHERTYPE_OFFSET);
    for (Index = 0; Index < Header; Index++) {
        Buffer[Index] = 0;
    }
    BuildHostDescriptor(Buffer, Frame, Frame + AIC_NET_MAC_LENGTH, EtherType, VifIndex, ApIndex,
                        Body);
    for (Index = 0; Index < Body; Index++) {
        Buffer[Header + Index] = Frame[AIC_NET_ETHERNET_HEADER_LENGTH + Index];
    }
    /* 判据行在写前：含 staid/vif/mcast，对 Docs/18 眼睛清单；不改行为 */
    AicScanFormatAddress(Frame, Destination);
    SdhciMark("WiFi: data tx: len=");
    SdhciMarkDecimal("", Body);
    SdhciMarkHex(" ethertype=0x", (UINT32)EtherType, 4);
    SdhciMark(" dst=");
    SdhciMark(Destination);
    SdhciMarkHex(" vif=", (UINT32)Buffer[AIC_EAPOL_TX_HEADER_LENGTH + AIC_TX_DESC_VIF_INDEX],
                 2);
    SdhciMarkHex(" staid=", (UINT32)Buffer[AIC_EAPOL_TX_HEADER_LENGTH + AIC_TX_DESC_STA_INDEX],
                 2);
    SdhciMarkHex(" ac=", (UINT32)Buffer[AIC_EAPOL_TX_HEADER_LENGTH + AIC_TX_DESC_AC], 2);
    SdhciMarkHex(" tid=", (UINT32)Buffer[AIC_EAPOL_TX_HEADER_LENGTH + AIC_TX_DESC_TID], 2);
    SdhciMark((Frame[0] & 0x01u) != 0u ? " mcast=1\n" : " mcast=0\n");
    return AicWifiDataPathWrite(Host, Buffer, Header + Body, AIC_TX_LOG_DATA,
                                "WiFi: data tx block write FAIL");
}
