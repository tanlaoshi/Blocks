/*
 * WifiEapolTx.c — PR-34：把一帧 EAPOL 走 SDIO **数据通道**发出去（本仓第一条数据 TX）
 *
 * 用户眼里：`WiFi: eapol tx: fc=.. padded=.. blocks=..` 和紧随的 `eapol tx m2 …`。
 * 从哪读：AicEapolSend（整帧）→ TxBuildDescriptor（4 B 帧头 + 28 B txdesc）
 *         → TxWrite（流控 + 4 B 对齐 + TAIL + 512 对齐 + 块写）。
 * 别改：线上格式是厂商 aicwf_sdio.c:1530-1550（aggr 拼帧）+ :1370-1420（xmit）+
 *       rwnx_tx.c:1497-1530（txdesc 字段）的字节契约：
 *         [4 B sdio hdr][28 B txdesc_api][payload][补 0 到 4][补 4 B TAIL][补到 512]
 *       4 B 帧头 = len(12bit = payload+28) | type=0x01(data) | crc8；
 *       payload **不含 14 B 以太网头**（厂商 skb_pull(14)，地址与 ethertype 在 txdesc 里，
 *       802.11 头与 LLC/SNAP 由固件补）。
 * 与消息通道的差别（别混）：消息是 16 B lmac_msg 头 + 参数，走 AicSendMessageEx；
 *       数据帧是 txdesc + 载荷，走这里。两条路都用 fn1 0x10 写 FIFO，块模式 512 B。
 */
#include "WifiHandshakePrivate.h"
#include "CString.h"

/* aicwf_sdio.c:1536-1546：sdio_header 4 B，hdr[2]=0x01 就是"数据帧" */
#define AIC_EAPOL_TX_HEADER_LENGTH 4u
#define AIC_EAPOL_TX_TYPE_DATA 0x01u
/*:1540 帧头 len 只占 12 bit（高 4 bit 空） */
#define AIC_EAPOL_TX_LENGTH_MASK 0x0FFFu
/* sizeof(struct txdesc_api) = 28：宿主 gcc include 厂商 ipc_shared.h 编译实测（报告 §3） */
#define AIC_EAPOL_TX_DESC_LENGTH 28u
/* SDIOWIFI_FUNC_BLOCKSIZE（aicwf_sdio.h:20）/ TX_ALIGNMENT（aicwf_txrxif.h:26）/ TAIL_LEN（:53） */
#define AIC_EAPOL_TX_BLOCK 512u
#define AIC_EAPOL_TX_ALIGN 4u
#define AIC_EAPOL_TX_TAIL 4u
/* 一帧 M2 只有 153 B（4+28+121），512 B 的缓冲够两帧；上限就是"别写超一个块" */
#define AIC_EAPOL_TX_MAX 512u
#define AIC_EAPOL_TX_PAYLOAD_MAX \
    (AIC_EAPOL_TX_MAX - AIC_EAPOL_TX_HEADER_LENGTH - AIC_EAPOL_TX_DESC_LENGTH)
/*
 * 数据 TX 的流控门槛：aicwf_sdio.c:160 `if (fc_reg > DATA_FLOW_CTRL_THRESH)`，
 * DATA_FLOW_CTRL_THRESH = 2（aicwf_sdio.h:59）⇒ 至少要 3 个空 buffer 才发。
 * 重试节奏同厂商（:169-176）：前 30 次 200 µs、30-40 次 2 ms、之后 10 ms，共 50 次。
 */
#define AIC_EAPOL_TX_FC_MIN 3u
#define AIC_EAPOL_TX_FC_RETRIES 50u
#define AIC_EAPOL_TX_FC_FAST_TRIES 30u
#define AIC_EAPOL_TX_FC_MID_TRIES 40u
#define AIC_EAPOL_TX_FC_FAST_DELAY_US 200u
#define AIC_EAPOL_TX_FC_MID_DELAY_MS 2u
#define AIC_EAPOL_TX_FC_LATE_DELAY_MS 10u
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
#define AIC_TX_DESC_FLAGS 26u
/*
 * hw queue 编号（rwnx_tx.h:19-22 RWNX_HWQ_BK/BE/VI/VO）。厂商把 txq->hwq->id 直接写进
 * host.ac（rwnx_tx.c:608 "use ac field for hw_txq"）；TID 0 映射到 BE
 * （rwnx_main.c:480-486 的 rwnx_tid2hwq[0]），所以 EAPOL 用 BE=1、tid=0。
 */
#define AIC_TX_HWQ_BE 1u
#define AIC_TX_TID_BE 0u

/* 4+28+121 = 153 B；UINT32 数组保证 4 B 对齐（txdesc 里有 u32 字段） */
static UINT32 sAicEapolTxBuffer[(AIC_EAPOL_TX_MAX + 3u) / 4u];

/* 数据 TX 流控：读 fn1 0x03（V3 的 FLOW_CTRL_Q1），> 2 才发；失败把读到的值带出来 */
static int TxFlowCtrl(SDHCI_HOST *Host, UINT32 *OutCredit) {
    UINT32 Try = 0;
    UINT8 Credit = 0;

    for (;;) {
        if (SdhciIoRead8(Host, 1u, AIC_FLOW_CTRL_REG_V3, &Credit) != 0) {
            AicMarkR5(Host->LastStatus);
            SdhciMark("WiFi: eapol tx: fc read failed (cmd52 fn1 0x03)\n");
            return -1;
        }
        if ((UINT32)Credit >= AIC_EAPOL_TX_FC_MIN) {
            *OutCredit = (UINT32)Credit;
            return 0;
        }
        if (Try >= AIC_EAPOL_TX_FC_RETRIES) {
            *OutCredit = (UINT32)Credit;
            return -1;
        }
        Try++;
        if (Try < AIC_EAPOL_TX_FC_FAST_TRIES) {
            SdhciDelayMicroseconds(AIC_EAPOL_TX_FC_FAST_DELAY_US);
        } else if (Try < AIC_EAPOL_TX_FC_MID_TRIES) {
            SdhciDelayMilliseconds(AIC_EAPOL_TX_FC_MID_DELAY_MS);
        } else {
            SdhciDelayMilliseconds(AIC_EAPOL_TX_FC_LATE_DELAY_MS);
        }
    }
}

/*
 * 4 B sdio 帧头 + 28 B txdesc。len 字段 = payload + 28（厂商 :1540 把 txdesc 算进长度）；
 * ethertype 按**网络字节序**写在内存里（厂商 :1524 直接抄 eth_t.h_proto，那 2 B 是大端），
 * 所以先写高字节 0x88 再写 0x8e —— 写成小端 0x8e88 的话固件看见的就是另一个值。
 */
static void TxBuildDescriptor(UINT8 *Buffer, const UINT8 *Destination, const UINT8 *Source,
                              UINT8 VifIndex, UINT8 ApIndex, UINT32 PayloadLength) {
    UINT8 *Desc = Buffer + AIC_EAPOL_TX_HEADER_LENGTH;
    UINT32 Body = AIC_EAPOL_TX_DESC_LENGTH + PayloadLength;

    Buffer[0] = (UINT8)(Body & 0xFFu);
    Buffer[1] = (UINT8)((Body >> 8) & (AIC_EAPOL_TX_LENGTH_MASK >> 8));
    Buffer[2] = (UINT8)AIC_EAPOL_TX_TYPE_DATA;
    Buffer[3] = AicCrc8(Buffer, 3u);
    AicPut16(Desc + AIC_TX_DESC_PACKET_LENGTH, (UINT16)PayloadLength);
    AicPut16(Desc + AIC_TX_DESC_FLAGS_EXT, 0u);
    /*
     * hostid（u32）保持 0：厂商用它把 TX confirm 对回自己的 skb（rwnx_tx.c:1439），
     * 本刀不等 confirm，AicEapolSend 已经先把 txdesc 整段清过零。
     */
    (void)memcpy(Desc + AIC_TX_DESC_ETH_DEST, Destination, WIFI_MAC_ADDRESS_LENGTH);
    (void)memcpy(Desc + AIC_TX_DESC_ETH_SRC, Source, WIFI_MAC_ADDRESS_LENGTH);
    Desc[AIC_TX_DESC_ETHERTYPE] = (UINT8)(AIC_ETHERTYPE_PAE >> 8);
    Desc[AIC_TX_DESC_ETHERTYPE + 1u] = (UINT8)(AIC_ETHERTYPE_PAE & 0xFFu);
    Desc[AIC_TX_DESC_AC] = (UINT8)AIC_TX_HWQ_BE;
    Desc[AIC_TX_DESC_TID] = (UINT8)AIC_TX_TID_BE;
    Desc[AIC_TX_DESC_VIF_INDEX] = VifIndex;
    Desc[AIC_TX_DESC_STA_INDEX] = ApIndex;
    AicPut16(Desc + AIC_TX_DESC_FLAGS, 0u);
}

/*
 * 补齐 + 块写。补齐规则照厂商：先 4 B 对齐，再"不是 512 整数倍就补 4 B TAIL 并向上取整"
 * （aicwf_sdio.c:1225-1237 的消息路径 / :1555-1600 的数据路径是同一套算术）。
 * 写法和消息通道一致：SdhciIoRwExtended 块模式、固定地址（OP_CODE=0）写 fn1 0x10。
 */
static int TxWrite(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 Length) {
    UINT32 Aligned = (Length + (AIC_EAPOL_TX_ALIGN - 1u)) & ~(AIC_EAPOL_TX_ALIGN - 1u);
    UINT32 Padded = Aligned;
    UINT32 Credit = 0;
    UINT32 Index;

    for (Index = Length; Index < Aligned; Index++) {
        Buffer[Index] = 0;
    }
    if ((Aligned % AIC_EAPOL_TX_BLOCK) != 0u) {
        Padded = ((Aligned + AIC_EAPOL_TX_TAIL + AIC_EAPOL_TX_BLOCK - 1u) / AIC_EAPOL_TX_BLOCK) *
                 AIC_EAPOL_TX_BLOCK;
    }
    if (Padded > AIC_EAPOL_TX_MAX) {
        SdhciMarkHex("WiFi: eapol tx: frame too big for one block, len=", Padded, 4);
        SdhciMark("\n");
        return -1;
    }
    for (Index = Aligned; Index < Padded; Index++) {
        Buffer[Index] = 0;
    }
    if (TxFlowCtrl(Host, &Credit) != 0) {
        SdhciMarkHex("WiFi: eapol tx FAILED at flow control: fc=", Credit, 2);
        SdhciMarkHex(" need>=", AIC_EAPOL_TX_FC_MIN, 2);
        SdhciMark(" (chip has no free data buffer; nothing was written)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: eapol tx: fc=", Credit, 2);
    SdhciMarkHex(" payload=", Length, 4);
    SdhciMarkHex(" padded=", Padded, 4);
    SdhciMarkHex(" blocks=", Padded / AIC_EAPOL_TX_BLOCK, 2);
    SdhciMark("\n");
    if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, Padded / AIC_EAPOL_TX_BLOCK,
                          AIC_EAPOL_TX_BLOCK, Buffer) != 0) {
        AicMarkXferParams(Host, "WiFi: eapol tx block write FAIL", Padded,
                          Padded / AIC_EAPOL_TX_BLOCK);
        (void)SdhciWifiBusRecover(Host);
        return -1;
    }
    return 0;
}

/*
 * 对外唯一入口：Eapol 是 121 B 的 M2（不含以太网头），Destination = AP 的 BSSID，
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
    TxBuildDescriptor(Buffer, Destination, Source, VifIndex, ApIndex, Length);
    for (Index = 0; Index < Length; Index++) {
        Buffer[Header + Index] = Eapol[Index];
    }
    return TxWrite(Host, Buffer, Header + Length);
}
