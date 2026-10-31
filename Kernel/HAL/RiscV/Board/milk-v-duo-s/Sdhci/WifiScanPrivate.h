/*
 * WifiScanPrivate.h — PR-31 第二刀：MM_ADD_IF + SCANU_START + 扫描结果的内部声明
 *
 * 只被 WifiScan.c / WifiScanMessage.c / WifiScanResult.c / WifiScanDump.c 包含。
 * 内容分四块：消息 id 与参数长度、厂商结构体的逐字段镜像、扫描解析常量、
 *            verbose 取证用的打印宽度与 IE 偏移（第三刀追加）。
 * 来源：Source/DUO/sdk/.../aic8800_fdrv/{lmac_msg.h,lmac_mac.h}
 *       + 目标编译器（riscv-none-elf-gcc 13.2.0）实测 sizeof/offsetof。
 */
#ifndef SDHCI_WIFI_SCAN_PRIVATE_H
#define SDHCI_WIFI_SCAN_PRIVATE_H

#include "WifiPrivate.h"

/* ---- 消息 id ----
 * LMAC_FIRST_MSG(task) = task << 10（lmac_msg.h:123），TASK_MM=0、TASK_SCANU=4。
 * mm_msg_tag 里 MM_ADD_IF_REQ 是第 7 项（0x0006），CFM 紧随其后（0x0007）。
 * SCANU 枚举（lmac_msg.h:1558-1566）：START_REQ=0x1000、START_CFM=0x1001、
 * RESULT_IND=0x1004；0x1009 是厂商实际等的那条 START_CFM_ADDTIONAL
 * （rwnx_msg_tx.c:2821，见报告 §5 的预判）。
 */
#define AIC_MM_ADD_IF_REQ 0x0006u
#define AIC_MM_ADD_IF_CFM 0x0007u
#define AIC_TASK_SCANU 4u
#define AIC_SCANU_START_REQ 0x1000u
#define AIC_SCANU_START_CFM 0x1001u
#define AIC_SCANU_RESULT_IND 0x1004u
#define AIC_SCANU_START_CFM_ADDITIONAL 0x1009u

/*
 * 扫描期间固件主动上报的另一类 MM 消息（真机日志里的 `ignored id=004f`）。
 * 抄的：mm_msg_tag 第 79 项（lmac_msg.h:299）+ 结构体（lmac_msg.h:1095-1103）：
 *   freq(u16) noise_dbm(s8) [1 B 对齐填充] chan_time_ms(u32) chan_time_busy_ms(u32)
 * ⇒ sizeof = 12，与真机 plen=000c 一致。它不是错误，是每过一个信道的一条测量。
 */
#define AIC_MM_CHANNEL_SURVEY_IND 0x004Fu
#define AIC_MM_CHANNEL_SURVEY_LENGTH 12u
#define AIC_MM_SURVEY_FREQUENCY_OFFSET 0u
#define AIC_MM_SURVEY_NOISE_OFFSET 2u
#define AIC_MM_SURVEY_TIME_OFFSET 4u
#define AIC_MM_SURVEY_BUSY_OFFSET 8u

/* ---- 参数长度：全部来自厂商头文件编译出的 sizeof（报告 §1 字段表） ---- */
#define AIC_MM_ADD_IF_LENGTH 10u
#define AIC_ADD_IF_CFM_LENGTH 2u
#define AIC_SCANU_START_LENGTH 376u
#define AIC_SCANU_START_CFM_LENGTH 3u
/* sizeof(struct scanu_result_ind) = 12；payload[] 的 offset 也是 12 */
#define AIC_SCANU_RESULT_HEADER 12u

/* ---- 厂商常量（lmac_msg.h:1500-1503、lmac_mac.h:93/96） ---- */
#define AIC_SCAN_SSID_MAX 3u
#define AIC_SCAN_CHANNEL_MAX 42u /* MAC_DOMAINCHANNEL_24G_MAX(14)+5G_MAX(28) */
#define AIC_SCAN_CHANNEL_24G 13u /* 本刀只发 1..13；厂商会把 ch14 一起发，我们不发 */

/* 信道 n 的频率 = 2412 + 5*(n-1)，n=1..13 ⇒ 2412..2472 MHz */
#define AIC_SCAN_CHANNEL_2G4_FIRST 2412u
#define AIC_SCAN_CHANNEL_2G4_STEP 5u
#define AIC_SCAN_CHANNEL_2G4_LAST 2472u
#define AIC_SCAN_BAND_2G4 0u          /* PHY_BAND_2G4（lmac_mac.h:69） */
#define AIC_SCAN_CHANNEL_FLAGS_OPEN 0u /* 无 NO_IR/DISABLED/RADAR（lmac_mac.h:99-107） */
#define AIC_SCAN_TXPOWER_DBM 20u      /* 与 bringup 的 chan_config 同源，见报告 §1 */

/* ---- 镜像结构体的字段值 ---- */
#define AIC_SCAN_IF_TYPE_STATION 0u /* enum { MM_STA, ... } ⇒ MM_STA=0（lmac_msg.h:407） */
#define AIC_SCAN_P2P_OFF 0u
#define AIC_SCAN_MAC_LENGTH 6u
#define AIC_SCAN_BSSID_WILDCARD 0xFFFFu /* struct mac_addr 是 u16[3]，每字 0xFFFF = 全 FF */
#define AIC_SCAN_DRY_VIF_INDEX 0u       /* dry 没有 CFM，用 0 展示布局 */
#define AIC_SCAN_NO_ALTERNATE 0xFFFFu   /* 不是任何合法消息 id */

/* ---- 多轮扫描 ----
 * 单轮驻留只有 30–50 ms/信道（真机 survey），而 beacon 间隔 100 ms ⇒ 一轮必然漏 AP。
 * 默认 3 轮、最多 8 轮；每轮都是同一 vif 上的一次 SCANU_START + 收集，轮间不重加 vif。
 * 上限 8：13 ch × 2.5 s 静默已经 ~20 s，再多就不是“扫一下”了；8 也是 3 bit
 * （MaxBSSID Indicator 常用 3）能覆盖的量级，便于和 MBSSID 的 idx 对照。
 */
#define AIC_SCAN_ROUNDS_DEFAULT 3u
#define AIC_SCAN_ROUNDS_MAX 8u

/* ---- 接收预算 ----
 * 主判据是 0x1001：厂商 rwnx_msg_rx.c:586 的 rwnx_rx_scanu_start_cfm 收到它就直接
 * cfg80211_scan_done()，语义就是“扫描结束”；start 的回执是 0x1009（rwnx_msg_tx.c:2821
 * rwnx_send_msg(..., SCANU_START_CFM_ADDTIONAL, NULL)）。真机已验证固件不发 0x1001，
 * 所以 6 s 静默是纯等待、没有收益；本刀把单轮静默降到 2.5 s（5 × 500 ms）：
 *   一轮正常 = 13 ch × 30–50 ms（0.4–0.65 s）+ 2.5 s 静默 ≈ 3.2 s；
 *   单轮硬上限 6 s（仍小于上一刀的 12 s），3 轮 collect 正常 ≈ 10 s、最坏 18 s，
 *   不会 36 s（另有每轮 start cfm ≤2 s，CFM 正常是立即回的）。
 *   只有结果持续不断到 6 s 才会用满预算 —— 那是在收数据，不是在空等。
 * duration 不动：厂商 rwnx_msg_tx.c:2723-2725 同样把它置 0（那行被注释掉）。
 * CFM 仍是 4 × 500 ms = 2 s，与 ver/mmreset 同一档；它发生在每轮 collect 之前。
 */
#define AIC_SCAN_ROUND_BUDGET_MS 6000u
#define AIC_SCAN_RX_POLL_MS 500u
#define AIC_SCAN_IDLE_LIMIT 5u
#define AIC_SCAN_CFM_TRIES 4u

/* 收尾原因：只用于 scan summary 的尾标和失败文案分支 */
#define AIC_SCAN_END_DONE 0
#define AIC_SCAN_END_IDLE 1
#define AIC_SCAN_END_BUDGET 2

/* ---- 整帧缓冲与解析常量 ---- */
#define AIC_SCAN_FRAME_MAX 1600u /* = AIC_MSG_MAX；单条消息的硬上限 */
#define AIC_SCAN_RAW_HEAD_BYTES 16u
#define AIC_SCAN_SSID_TEXT_MAX 33u /* 32 B SSID + NUL */
#define AIC_SCAN_MAC_TEXT_MAX 18u  /* 17 个字符 + NUL */

/* ---- verbose 取证（本刀压缩）----
 * 只读不判：打印原始字节和字段，不纠正、不猜。输出格式就是验收判据（报告里贴了原文）。
 * 前 32 B 覆盖 24 B 定长头 + 8 B 定长体（timestamp 前 8 B），够核对 payload 起点；
 * IE 链只列前 6 条，`ie@36` 首行 + `left=` 仍在（真机判据，不能删）。
 */
#define AIC_SCAN_VERBOSE_OFF 0
#define AIC_SCAN_VERBOSE_ON 1
#define AIC_SCAN_HEX_HEAD_BYTES 32u
#define AIC_SCAN_HEX_COLUMNS 16u  /* 一行 16 B，与 Wireshark 默认宽度一致 */
#define AIC_SCAN_IE_PRINT_MAX 6u  /* 一条结果最多列 6 个 IE，防刷屏 */
#define AIC_SCAN_IE_BODY_SHOW 8u  /* 每个 IE 只打前 8 B 体 */
#define AIC_SCAN_WORD_BYTES 4u    /* 厂商 u32_l 字段 = 4 B 小端 */
#define AIC_SCAN_BYTE_BITS 8u     /* 每个字节 8 bit */

/* SdhciWifiScan 的模式（板级 ↔ Shell 各镜像一份，改这里要同步 Wifi.c） */
#define AIC_SCAN_MODE_RUN 0
#define AIC_SCAN_MODE_DRY 1
#define AIC_SCAN_MODE_VERBOSE 2

/* 802.11 管理帧定长头（ieee80211_mgmt 的前 24 B） */
#define AIC_DOT11_HEADER_LENGTH 24u
#define AIC_DOT11_DURATION_OFFSET 2u
#define AIC_DOT11_ADDRESS1_OFFSET 4u
#define AIC_DOT11_ADDRESS2_OFFSET 10u
#define AIC_DOT11_ADDRESS3_OFFSET 16u
#define AIC_DOT11_SEQUENCE_OFFSET 22u
#define AIC_DOT11_TYPE_MASK 0x000Cu
#define AIC_DOT11_TYPE_MANAGEMENT 0x0000u
#define AIC_DOT11_SUBTYPE_MASK 0x000Fu
#define AIC_DOT11_SUBTYPE_SHIFT 4u
#define AIC_DOT11_SUBTYPE_PROBE_RESPONSE 5u
#define AIC_DOT11_SUBTYPE_BEACON 8u
#define AIC_DOT11_VERSION_MASK 0x0003u
#define AIC_DOT11_FLAGS_SHIFT 8u
#define AIC_DOT11_FLAGS_MASK 0x00FFu
#define AIC_DOT11_SEQUENCE_FRAG_MASK 0x000Fu
#define AIC_DOT11_SEQUENCE_NUMBER_SHIFT 4u

/*
 * Beacon / Probe Response 在 24 B 定长头之后还有 12 B 定长体：
 * timestamp(8) + beacon_interval(2) + capability(2)，IE 链从 36 起，**不是 24**。
 * 抄的（厂商源码）：rwnx_msg_rx.c:662  ie = mgmt->u.probe_resp.variable
 *                   rwnx_msg_rx.c:663  ielen = len - offsetof(struct ieee80211_mgmt,
 *                                                      u.probe_resp.variable)
 * 抄的（内核同款）：linux_5.10/net/wireless/scan.c:2309/2339/2340。
 * 用 24 当 IE 起点时，第一个“IE”就是 timestamp 的前两个字节 —— 真机 4 条 SSID 全空、
 * 有一条 ch 与 freq 打架，都是这一个偏移错造成的（本刀报告 §4）。
 */
#define AIC_DOT11_FIXED_BODY_LENGTH 12u
#define AIC_DOT11_IE_OFFSET (AIC_DOT11_HEADER_LENGTH + AIC_DOT11_FIXED_BODY_LENGTH)

/* 信息元素：id(1) + length(1) + body */
#define AIC_IE_HEADER_LENGTH 2u
#define AIC_IE_SSID 0u
#define AIC_IE_DS_PARAMETER 3u
#define AIC_SSID_PRINTABLE_MIN 0x20u
#define AIC_SSID_PRINTABLE_MAX 0x7Eu

/* ---- Multiple BSSID（IE 0x47）----
 * 元素体：MaxBSSID Indicator(1) + Optional Subelements。
 * 非传输 BSSID 在 Subelement ID 0（Nontransmitted BSSID Profile）里，profile
 * 自身又是一条 IE 链：第一个元素必须是 Nontransmitted BSSID Capability(83,len=2)，
 * 后面才有 SSID(0) 和 Multiple BSSID-Index(85)。ID 值照 linux_5.10
 * include/linux/ieee80211.h:2754/2766/2768；解析骨架照
 * net/wireless/scan.c:2081-2136（只取 index/SSID，不做 profile 继承合并）。
 *
 * 派生规则（**变的是最后一个字节 A5，不是第一个字节**）：
 *   IEEE 802.11ax-2021 的 Multiple BSSID 集合：
 *     A0-A1-A2-A3-A4-A5 = 参考（传输）BSSID，n = MaxBSSID Indicator
 *     B = A5 mod 2^n
 *     A5(i) = A5 - B + ((B + i) mod 2^n)
 *   即 A5(i) = (A5 & ~mask) | (((A5 & mask) + i) & mask)，mask = 2^n - 1。
 *   内核同款实现：linux_5.10/include/net/cfg80211.h:6125-6137
 *   cfg80211_gen_new_bssid()（u64 的低位就是 A5）。真机看到的 04:…:f2 与
 *   0a:…:f2 差在第一个字节，按这条规则不可能由 idx 派生 —— 先解 IE 0x47，
 *   有 idx 才按上式派生；解不出就保留空 SSID，不猜。
 */
#define AIC_IE_MULTIPLE_BSSID 71u      /* WLAN_EID_MULTIPLE_BSSID = 0x47 */
#define AIC_IE_NON_TX_BSSID_CAP 83u    /* WLAN_EID_NON_TX_BSSID_CAP = 0x53 */
#define AIC_IE_MULTI_BSSID_INDEX 85u   /* WLAN_EID_MULTI_BSSID_IDX = 0x55 */
#define AIC_IE_NON_TX_BSSID_CAP_LENGTH 2u /* capability 元素体固定 2 B（scan.c:2103） */
#define AIC_MBSSID_PROFILE_MIN 4u      /* id+len+2 B capability = 4 B（scan.c:2097） */
#define AIC_MBSSID_INDICATOR_LENGTH 1u /* MaxBSSID Indicator 占 1 B（元素体首字节） */
#define AIC_MBSSID_MAX_BITS 8u         /* MaxBSSID Indicator 是 1 B；mask 用 u32 算 */
#define AIC_MBSSID_INDEX_MIN 1u        /* index 0 是传输 BSSID（scan.c:2112） */
#define AIC_SCAN_MBSSID_ERROR_NONE 0xFFFFu
#define AIC_SCAN_RECORD_INDEX_NONE 0xFFFFu

/* 合并表：32 个 BSSID 足够家庭/办公室；超出只丢展示，不丢轮次计数 */
#define AIC_SCAN_AP_MAX 32u
/* 一帧里最多取 16 个 AP（1 个传输 + 15 个 profile；n=4 的常见集合正好 16）。
 * 再多也不扩栈：畸形 0x47 最多写满 16 项，溢出 profile 直接忽略。 */
#define AIC_SCAN_AP_PER_FRAME 16u

/* 一个可打印的 AP：由 Result 解析、Report 按 BSSID 合并 */
typedef struct {
    UINT8 Bssid[AIC_SCAN_MAC_LENGTH];
    char Ssid[AIC_SCAN_SSID_TEXT_MAX];
    UINT16 Frequency;
    INT8 Rssi;
    UINT8 Channel;
    UINT8 MbssidIndex;       /* 0 = 传输 BSSID；1.. = 非传输，打印 (mbssid idx=N) */
    UINT16 MbssidErrorOffset; /* 0xFFFF = 没出错；否则是 profile 解析失败的帧内偏移 */
} AIC_SCAN_AP;

/*
 * struct mac_chan_def（lmac_mac.h:109-118）：5 B 字段 + 1 B 对齐填充 = 6 B/项。
 * 填充显式写出来，数组步长就与厂商一样是 6（否则 freq 会排歪）。
 */
typedef struct {
    UINT16 Frequency; /* +0 MHz */
    UINT8 Band;       /* +2 PHY_BAND_2G4 */
    UINT8 Flags;      /* +3 mac_chan_flags */
    INT8 TxPower;     /* +4 dBm */
    UINT8 TailPad[1]; /* +5 对齐到 2 */
} AIC_SCAN_CHANNEL;

/* struct mac_ssid（lmac_mac.h:52-56）：length + 32 B，共 33 B，对齐 1 */
typedef struct {
    UINT8 Length;
    UINT8 Array[32]; /* MAC_SSID_LEN */
} AIC_SCAN_SSID;

/*
 * struct mm_add_if_req（lmac_msg.h:622-628）：u8 type + struct mac_addr + bool p2p。
 * sizeof=10：type@0、填充@1、addr@2（u16[3]，6 B）、p2p@8、尾填充@9。
 */
typedef struct {
    UINT8 Type;        /* +0 */
    UINT8 TypePad;     /* +1 对齐 struct mac_addr 的 2 B 对齐 */
    UINT16 Address[3]; /* +2 struct mac_addr（u16 array[3]） */
    UINT8 P2p;         /* +8 bool */
    UINT8 TailPad;     /* +9 尾填充到 2 的倍数 = 10 */
} AIC_SCAN_ADD_IF;

/*
 * struct scanu_start_req（lmac_msg.h:1586-1614），sizeof=376。
 * 厂商没用 __packed，按自然对齐；下面的填充全部显式写出。
 *   chan@0(252) ssid@252(99) pad@351 bssid@352(6) pad@358 add_ies@360
 *   add_ie_len@364 vif_idx@366 chan_cnt@367 ssid_cnt@368 no_cck@369
 *   pad@370 duration@372
 */
typedef struct {
    AIC_SCAN_CHANNEL Channel[AIC_SCAN_CHANNEL_MAX]; /* +0   */
    AIC_SCAN_SSID Ssid[AIC_SCAN_SSID_MAX];          /* +252 */
    UINT8 SsidPad[1];                               /* +351 对齐 bssid 的 2 B 对齐 */
    UINT16 Bssid[3];                                /* +352 struct mac_addr */
    UINT8 BssidPad[2];                              /* +358 对齐 u32 */
    UINT32 AddIes;                                  /* +360 */
    UINT16 AddIeLength;                             /* +364 */
    UINT8 VifIndex;                                 /* +366 */
    UINT8 ChannelCount;                             /* +367 */
    UINT8 SsidCount;                                /* +368 */
    UINT8 NoCck;                                    /* +369 bool */
    UINT8 DurationPad[2];                           /* +370 对齐 u32 */
    UINT32 Duration;                                /* +372 */
} AIC_SCANU_START;

/* struct mm_add_if_cfm（lmac_msg.h:697-702）：2 B */
typedef struct {
    UINT8 Status;         /* +0 0 = 成功 */
    UINT8 InstanceNumber; /* +1 LMAC 分配的 vif_idx */
} AIC_ADD_IF_CFM;

/* struct scanu_start_cfm（lmac_msg.h:1617-1624）：3 B */
typedef struct {
    UINT8 VifIndex;   /* +0 */
    UINT8 Status;     /* +1 */
    UINT8 ResultCount; /* +2 */
} AIC_SCANU_START_CONFIRM;

/*
 * struct scanu_result_ind（lmac_msg.h:1627-1641），sizeof=12，payload@12。
 * payload[] 是 C99 柔性数组，镜像成同名字段后可以用 BLOCKS_OFFSETOF 核对偏移；
 * 本结构体只做指针转换，从不实例化。
 */
typedef struct {
    UINT16 Length;          /* +0 帧长 */
    UINT16 FrameControl;    /* +2 802.11 frame control */
    UINT16 CenterFrequency; /* +4 MHz */
    UINT8 Band;             /* +6 */
    UINT8 StationIndex;     /* +7 */
    UINT8 InstanceNumber;   /* +8 */
    INT8 Rssi;              /* +9 dBm */
    UINT32 Payload[];       /* +12 802.11 管理帧本体 */
} AIC_SCANU_RESULT;

/*
 * verbose 取证帧：只给“合并后每个 AP 的最强那次”留一份整帧，普通 scan 不拷。
 * 接收路径仍只 memcpy、不打印：115200 下几十行 dump 要几百毫秒，期间不轮询 SDIO，
 * 固件侧的结果/测量帧会积压甚至被丢 —— 真机 verbose 那次 survey 缺了 2422..2442。
 * 每槽存整帧 1600 B（= AIC_SCAN_FRAME_MAX，单条消息硬上限），dump 的 left= 才准确。
 * 16 槽 × 1600 B = 25 KiB 静态 BSS；Duo S 128 MiB DRAM，不做动态分配。
 * 多轮合并到 32 个 AP，但只有前 16 个“更强帧”能留 dump；其余结果行照打，只是没 dump。
 */
#define AIC_SCAN_RESULT_KEEP_MAX 16u

typedef struct {
    UINT16 FrameLength; /* e2a 整帧长，≤ AIC_SCAN_FRAME_MAX */
    UINT16 ParamLength; /* scanu_result_ind 参数长 */
    UINT8 Frame[AIC_SCAN_FRAME_MAX];
} AIC_SCAN_RECORD;

/* ---- 跨文件原型（WifiScanMessage.c / WifiScanResult.c / WifiScanDump.c / WifiScanReport.c） ---- */
UINT32 AicMmAddIfBuild(UINT8 *Buffer);
UINT32 AicScanuStartBuild(UINT8 *Buffer, UINT8 VifIndex);
int AicScanLayoutCheck(void);
int SdhciWifiScan(int Mode, UINT32 Rounds);
void AicScanDumpVerbose(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength, UINT32 Index);
void AicScanFormatAddress(const UINT8 *Address, char *Text);
/* MM_ADD_IF 里写的本机 vif MAC（厂商 wdev.address 的替代）。只读；DHCP/ARP 拿它兜底 */
const UINT8 *AicScanStationAddress(void);
void AicScanMarkSigned(INT32 Value);
/* Report：入口做一次 Reset，每轮 Start/Collect 前 Round，收帧 Add，轮末 RoundDone */
void AicScanReportReset(UINT32 Rounds, int Verbose);
void AicScanReportRound(UINT32 Round);
void AicScanReportAdd(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength);
void AicScanReportRoundDone(UINT32 Elapsed, int Done, int End);
int AicScanReportFinish(void);
void AicScanReportIgnored(UINT16 Id, UINT32 ParamLength, const UINT8 *Frame, int Verbose);
/* join 用：在合并表里找目标 / 问"扫过没有"（WifiAssoc.c） */
int AicScanReportFind(const char *Ssid, const UINT8 *Bssid, AIC_SCAN_AP *Out);
UINT32 AicScanReportCount(void);
/* join 用：拿扫描时建的 vif。Cached 不问卡；Ensure 没有就补一条 MM_ADD_IF */
int AicScanVifCached(UINT8 *OutVifIndex);
int AicScanVifEnsure(SDHCI_HOST *Host, UINT8 *OutVifIndex);
/* mmreset 之后固件的 vif/BSS/信道上下文已失效：只丢本模块缓存，流程不动 */
void AicScanVifForget(void);
void AicScanReportClear(void);
/* Result：一帧 → 1 个传输 BSSID + 0..N 个非传输 BSSID；0 = 不是可解析的 Beacon/ProbeRsp */
UINT32 AicScanResultParse(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength,
                          AIC_SCAN_AP *Entries, UINT32 MaxEntries);
void AicScanResultPrintAp(const AIC_SCAN_AP *Entry, UINT32 Index);
void AicScanResultPrintMbssidFailure(const AIC_SCAN_AP *Entry);

#endif
