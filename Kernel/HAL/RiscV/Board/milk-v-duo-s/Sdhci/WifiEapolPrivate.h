/*
 * WifiEapolPrivate.h — PR-33：`wififw eapol poll` 的帧布局镜像、常量与跨文件原型
 *
 * 用户眼里：join 成功后 AP 会立刻发 EAPOL-Key M1（SDIO **数据帧**，不是 CFM 消息）；
 *           这条命令把数据帧从 RX FIFO 里掏出来、认成 EAPOL、打印关键字段。
 * 从哪读：SdhciWifiEapolPoll（WifiEapol.c，收包/分流/入口）
 *         → AicEapolParseData（WifiEapolParse.c，802.11/以太网定位 + EAPOL-Key 解码）。
 * 别改：hw_rxhdr 的 56 B 布局与 60 B 的 MPDU 偏移、EAPOL-Key 的 99 B 固定头，
 *       都是厂商/空口的字节契约；AicEapolLayoutCheck 在收帧前核对 sizeof/offsetof。
 * 来源：
 *   Source/DUO/sdk/.../aic8800_fdrv/{aicwf_sdio.h:66-70、aicwf_sdio.c:2019-2135、
 *                                  aicwf_txrxif.c:308、rwnx_rx.h:230-320、rwnx_rx.c:1981,2141}
 *   + 802.11-2016 §12.7（EAPOL-Key）
 *   + linux_5.10/include/uapi/linux/if_ether.h:88（ETH_P_PAE=0x888E）
 */
#ifndef SDHCI_WIFI_EAPOL_PRIVATE_H
#define SDHCI_WIFI_EAPOL_PRIVATE_H

#include "WifiAssocPrivate.h"
/* MIC@81 / KeyDataLen@97 / KeyData@99 的唯一定义（公共模块，只读不改） */
#include "WifiCrypto/WifiEapol.h"

/* ---- SDIO 帧类型（aicwf_sdio.h:66-70）---- */
#define AIC_SDIO_TYPE_DATA 0x00u
#define AIC_SDIO_TYPE_CFG 0x10u
#define AIC_SDIO_TYPE_CFG_CMD_RSP 0x11u
#define AIC_SDIO_TYPE_CFG_DATA_CFM 0x12u
#define AIC_SDIO_TYPE_CFG_PRINT 0x13u
/*
 * 数据 vs 配置的分流判据：厂商 aicwf_txrxif.c:308
 *   if ((data[2] & SDIO_TYPE_CFG) != SDIO_TYPE_CFG)  // type : data
 * 也就是「bit4 = 0 的都是数据帧」（type 0x00，以及 TX 侧的 0x01 风格）。
 * 0x10..0x13 才是配置：0x11 CMD_RSP / 0x12 DATA_CFM / 0x13 PRINT。
 */
#define AIC_SDIO_TYPE_CFG_MASK 0x10u
/* 类型字节在帧内 +2（aicwf_txrxif.c:308 读的就是 data[2]） */
#define AIC_SDIO_TYPE_OFFSET 2u
#define AIC_SDIO_MIN_FRAME_LENGTH (AIC_SDIO_TYPE_OFFSET + 1u)
/*
 * 一次 CMD53 读回来的是**一串聚合帧**，不是一帧：厂商 aicwf_process_rxframes 用
 * `while (aicwf_another_ptk())` 逐帧走（aicwf_txrxif.c:290-350），每帧按 4 B 对齐
 * （aicwf_txrxif.h 的 RX_ALIGNMENT）。两张长度公式不一样，别共用一个：
 *   数据帧：pkt_len + RX_HWHRD_LEN(60)          aicwf_txrxif.c:309-310
 *   配置帧：align4(pkt_len) + 4（4 B 块头）      aicwf_txrxif.c:338-345
 */
#define AIC_RX_FRAME_ALIGN 4u
#define AIC_RX_CFG_HEADER_LENGTH 4u

/* ---- 数据帧的 RX 硬件头（rwnx_rx.h:230-320）----
 * 厂商 sizeof(struct hw_rxhdr) = 56（见 AicEapolLayoutCheck 的核对项），
 * 中间 2 B 对齐、末尾 2 B 补齐到 4 ⇒ RX_HWHRD_LEN = 60（aicwf_txrxif.h:28
 * 的 60，注释 "58->60 word allined"）。厂商取 MPDU 的方式是
 *   msdu_offset = sizeof(struct hw_rxhdr) + 2;   // 58
 *   skb_pull(skb, msdu_offset + 2);              // 60（rwnx_rx.c:2141）
 * 所以 **802.11 帧从 +60 开始**；FIFO 里这一帧的总长 = 60 + hwvect.len
 * （aicwf_txrxif.c:309-310 的 aggr_len = pkt_len + RX_HWHRD_LEN）。
 */
#define AIC_RX_HW_HEADER_LENGTH 60u
#define AIC_RX_HW_STRUCT_LENGTH 56u
#define AIC_RX_HW_STATUS_OFFSET 36u
#define AIC_RX_HW_FLAGS_OFFSET 48u
#define AIC_RX_HW_PATTERN_OFFSET 52u
/*
 * status 位域（rwnx_rx.h:250-272，u32 从 +36 起）：
 *   bit0 rx_vect2_valid / bit1 resp_frame / bit2-4 decr_status / bit5 rx_fifo_oflow
 *   bit6 undef_err / bit7 phy_err / **bit8 fcs_err** / bit9 addr_mismatch / ...
 * fcs_err = 1 就是"这帧 CRC 没过"——本刀要求遇到它打一行、丢掉、继续。
 */
#define AIC_RX_HW_STATUS_FCS_ERR 0x00000100u
#define AIC_RX_HW_STATUS_DECR_SHIFT 2u
#define AIC_RX_HW_STATUS_DECR_MASK 0x7u
#define AIC_RX_HW_DECR_UNENC 0u /* RWNX_RX_HD_DECR_UNENC（rwnx_rx.h:46） */
/* flags 位域（rwnx_rx.h:294-312）：bit8-15 = vif_idx、bit16-23 = sta_idx */
#define AIC_RX_HW_FLAGS_INDEX_SHIFT 8u
#define AIC_RX_HW_FLAGS_INDEX_MASK 0xFFu
#define AIC_RX_HW_FLAGS_STA_SHIFT 16u

/* ---- 802.11 data 头（802.11-2016 §9.3.2.1）---- */
#define AIC_DOT11_HEADER_LENGTH 24u
#define AIC_DOT11_ADDR1_OFFSET 4u /* addr1（RA/DA）：AP 发给我们时就是本机 STA MAC */
#define AIC_DOT11_QOS_LENGTH 2u  /* QoS Control：subtype bit7=1 时 +2 */
#define AIC_DOT11_HTC_LENGTH 4u  /* Order/HTC：FC1 bit7=1 时 +4 */
#define AIC_DOT11_ADDR4_LENGTH 6u /* ToDS+FromDS 同时置位（4 地址/WDS）时 +6 */
#define AIC_DOT11_FRAME_TYPE_MASK 0x0Cu
#define AIC_DOT11_FRAME_TYPE_DATA 0x08u
#define AIC_DOT11_FC0_QOS 0x80u
#define AIC_DOT11_FC1_HTC 0x80u
#define AIC_DOT11_FC1_TO_DS 0x01u
#define AIC_DOT11_FC1_FROM_DS 0x02u
/* LLC/SNAP 8 B：AA AA 03 | 00 00 00 | 88 8E（RFC 1042 + ETH_P_PAE） */
#define AIC_LLC_SNAP_LENGTH 8u
#define AIC_ETH_HEADER_LENGTH 14u /* 12 B MAC + 2 B ethertype */
#define AIC_ETHERTYPE_PAE 0x888Eu  /* ETH_P_PAE（if_ether.h:88）：EAPOL */

/* ---- EAPOL-Key（802.11-2016 §12.7.2 图 12-25；802.1X-2020 §11.9）---- */
#define AIC_EAPOL_HEADER_LENGTH 4u /* version(1) type(1) length(2, BE) */
#define AIC_EAPOL_TYPE_EAP_PACKET 0u
#define AIC_EAPOL_TYPE_START 1u
#define AIC_EAPOL_TYPE_LOGOFF 2u
#define AIC_EAPOL_TYPE_KEY 3u
#define AIC_EAPOL_TYPE_ASF_ALERT 4u
#define AIC_EAPOL_VERSION_2001 1u
#define AIC_EAPOL_VERSION_2004 2u
#define AIC_EAPOL_VERSION_2010 3u
#define AIC_EAPOL_KEY_DESCRIPTOR_OFFSET 4u
#define AIC_EAPOL_KEY_INFORMATION_OFFSET 5u
#define AIC_EAPOL_KEY_LENGTH_OFFSET 7u
#define AIC_EAPOL_REPLAY_OFFSET 9u
#define AIC_EAPOL_REPLAY_LENGTH 8u
#define AIC_EAPOL_NONCE_OFFSET 17u
#define AIC_EAPOL_NONCE_LENGTH 32u
/* MIC@81（WIFI_EAPOL_MIC_OFFSET）、KeyDataLen@97、KeyData@99 来自公共模块 */
#define AIC_EAPOL_KEY_FIXED_LENGTH 99u /* 4 + 95：Key Data 为空时的最小 EAPOL-Key 帧 */
#define AIC_EAPOL_KEY_DATA_DUMP 24u    /* 只打 Key Data 前 24 B（PMKID KDE 够看） */

/* Key Information 位（802.11-2016 §12.7.2 图 12-25） */
#define AIC_EAPOL_KEY_INFORMATION_VERSION_MASK 0x0007u
#define AIC_EAPOL_KEY_INFORMATION_PAIRWISE 0x0008u
#define AIC_EAPOL_KEY_INFORMATION_INSTALL 0x0040u
#define AIC_EAPOL_KEY_INFORMATION_ACK 0x0080u
#define AIC_EAPOL_KEY_INFORMATION_MIC 0x0100u
#define AIC_EAPOL_KEY_INFORMATION_SECURE 0x0200u
#define AIC_EAPOL_KEY_INFORMATION_ERROR 0x0400u
#define AIC_EAPOL_KEY_INFORMATION_REQUEST 0x0800u
#define AIC_EAPOL_KEY_INFORMATION_ENCRYPTED 0x1000u

/* ---- 轮询预算 ---- */
#define AIC_EAPOL_POLL_DEFAULT_MS 3000u /* M1 在 assoc 后通常几十 ms 内到；3 s 留 AP 重传 */
#define AIC_EAPOL_POLL_MAX_MS 60000u    /* 手打上限：再长就不像"看一眼"了 */
/*
 * 一次轮询最多解这么多帧。PR-34 从 64 抬到 256：四次握手要在 3 s 里从"assoc 之后的
 * FIFO 积压"（beacon 100 ms 一发，加上广播/组播）里把 M1 捞出来 —— 64 帧在一台有广播
 * 流量的 AP 上可能还没走到 M1 就停。真正防死循环的是预算（rdtime），帧数只是第二道闸。
 */
#define AIC_EAPOL_POLL_MAX_FRAMES 256u
/* 60 B hdr + 1500 B MPDU + 4 B 对齐余量 ⇒ 2048 足够；超出部分读掉丢弃，防串包 */
#define AIC_EAPOL_FRAME_MAX 2048u
#define AIC_EAPOL_HEAD_DUMP 32u /* 第一帧数据帧的头部取证宽度 */
#define AIC_EAPOL_MPDU_DUMP 16u /* 802.11 帧头取证宽度（看 +60 到底对不对） */

/*
 * struct hw_rxhdr 的镜像：只摆到我们能引用的字段，位域用整字 + 具名偏移表达
 * （C 不能 offsetof 位域，所以偏移由 AicEapolLayoutCheck 核对整字偏移）。
 * 字段宽度照 rwnx_rx.h:230-320；rx_vector_1 是 __packed 的 16 B。
 */
typedef struct {
    UINT32 LengthAndType; /* +0  hwvect.len:16 | reserved(type):8 | mpdu/ampdu_cnt:8 */
    UINT32 TsfLow;        /* +4  hwvect.tsf_lo */
    UINT32 TsfHigh;       /* +8  hwvect.tsf_hi */
    UINT32 Vector1[4];    /* +12 hwvect.rx_vect1（__packed 16 B） */
    UINT32 Vector2[2];    /* +28 hwvect.rx_vect2（8 B） */
    UINT32 Status;        /* +36 位域整字：bit8 = fcs_err */
    UINT32 PhyInfo[2];    /* +40 phy_channel_info_desc（8 B） */
    UINT32 Flags;         /* +48 vif_idx/sta_idx 位域整字 */
    UINT32 Pattern;       /* +52 */
} AIC_RX_HW_HEADER;

/* 一次 poll 的计数：判据行就是把它打出来 */
typedef struct {
    UINT32 Frames;       /* 从 FIFO 读到的 SDIO 帧（数据 + 配置） */
    UINT32 DataFrames;   /* 其中数据帧（type & 0x10 == 0） */
    UINT32 ConfigFrames; /* 其中配置帧（CFM/PRINT/…） */
    UINT32 EapolFrames;  /* 认出来的 EAPOL（ethertype 0x888e） */
    UINT32 KeyFrames;    /* 其中 EAPOL-Key（type=3）：M1..M4 的载体，判据只认它 */
    UINT32 BadFrames;    /* 帧短 / 长度自相矛盾 / fcs_err / EAPOL 太短 */
    UINT32 HeadDumped;   /* 第一帧数据帧的取证已打印（0/1） */
    /* 诊断：窗口里轮询了几次、其中几次"固件没东西给"。
       polls 远大于 frames ⇒ 固件在那段时间基本不上送（问题在固件侧）。 */
    UINT32 Polls;
    UINT32 EmptyPolls;
} AIC_EAPOL_COUNTERS;

/* WifiEapol.c */
int AicEapolLayoutCheck(void);
int SdhciWifiEapolPoll(const char *MsText);
/*
 * 轮询主循环：预算内把 FIFO 里的数据帧解到 EAPOL。Done != 0 时每解完一帧就问一次，
 * 它返回 1 就提前收工 —— 四次握手要在"收到 M1 的同一刻"停下，而不是等满 3 s。
 * `wififw eapol poll` 传 Done=0（行为与 PR-33 完全一致）。
 */
int AicEapolPollRun(SDHCI_HOST *Host, UINT32 BudgetMs, AIC_EAPOL_COUNTERS *Counters,
                    int (*Done)(void));
/* WifiEapolParse.c：把一帧数据帧解到 EAPOL（不认识的帧只打一行） */
void AicEapolParseData(const UINT8 *Frame, UINT32 FrameLength, AIC_EAPOL_COUNTERS *Counters);
/* WifiEapolKey.c：EAPOL 头分流 + Key 字段解码 + 取证 dump（只打印/计数） */
UINT16 AicEapolGetBigEndian16(const UINT8 *At);
void AicEapolMarkBytes(const char *Tag, const UINT8 *Data, UINT32 Length);
void AicEapolDumpHead(const UINT8 *Frame, UINT32 FrameLength);
/*
 * Station = 这帧 802.11 头的 addr1（AP 发给我们 ⇒ 本机 STA MAC）；裸扫兜底路径拿不到
 * 802.11 头，传 0。四次握手就靠它把 PTK 的 STA 侧 MAC 校准到固件真正在用的那个。
 */
void AicEapolParseBody(const UINT8 *Body, UINT32 Available, const UINT8 *Station,
                       AIC_EAPOL_COUNTERS *Counters);
/*
 * "接住下一帧 EAPOL-Key"的钩子：PR-34 起 WifiHandshake.c 挂一个 sink，
 * 在 Key 帧打印完之后被调用（Body = EAPOL 帧起点，Length = 可用字节，Station 同上）。
 * 挂 0 就是关掉，PR-33 的 `wififw eapol poll` 不受影响。
 */
typedef void (*AIC_EAPOL_KEY_SINK)(const UINT8 *Body, UINT32 Length, const UINT8 *Station);
void AicEapolSetKeySink(AIC_EAPOL_KEY_SINK Sink);
/* 定义在 WifiEapolKey.c；调用方只经 AicEapolSetKeySink 改它 */
extern AIC_EAPOL_KEY_SINK gAicEapolKeySink;
/*
 * 见过 ME_SET_CONTROL_PORT_CFM(0x1405) 没有（0/1）。PR-34 用它在"开控制端口"的同时
 * 继续走同一条收包路径 —— 因为控制端口一开，固件可能先吐排队已久的 M1、后写 CFM，
 * 两条消息都在同一条 SDIO FIFO 上，用一个循环收才不会把 M1 当成"陌生帧"丢掉。
 * 由 EapolPrintConfigFrame 置位，WifiHandshake.c 在发请求前清零。
 */
extern UINT32 gAicEapolControlPortCfm;

#endif
