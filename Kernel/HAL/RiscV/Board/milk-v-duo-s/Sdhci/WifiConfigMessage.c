/*
 * WifiConfigMessage.c — 配置无线电的三条 LMAC 消息，逐字段拼字节（只拼，不发）
 *
 * 用户眼里：`wififw bringup` 发出去的那三个包长什么样、每个字段的值从哪来。
 * 从哪读：AicMeConfigBuild / AicChanConfigBuild / AicMmStartBuild / AicBringupLayoutCheck。
 * 别改：下面三个结构体是厂商 lmac_mac.h / lmac_msg.h 的**逐字段镜像**（连编译器会插的
 *       填充都显式写出来），字段顺序/宽度/偏移必须与厂商一致。发之前
 *       AicBringupLayoutCheck 核对 sizeof，排歪了就拒绝发送 —— 不猜。
 * 来源：Source/DUO/sdk/.../aic8800_fdrv/{lmac_mac.h,lmac_msg.h,rwnx_msg_tx.c,rwnx_main.c}
 */
#include "WifiPrivate.h"

/* 厂商 MAC_DOMAINCHANNEL_24G_MAX / MAC_DOMAINCHANNEL_5G_MAX（lmac_mac.h:93、:96） */
#define AIC_CHAN_24G_MAX 14u
#define AIC_CHAN_5G_MAX 28u
/* 本刀只声明 2.4G 的 1..13 信道（用户指定）；厂商会把 2484 MHz(ch14) 一起发，我们不发 */
#define AIC_CHAN_24G_USED 13u
/* 信道 n 的频率：freq = 2412 + 5*(n-1) MHz（n=1..13 ⇒ 2412..2472） */
#define AIC_CHAN_24G_FIRST_FREQ 2412u
#define AIC_CHAN_24G_STEP 5u
/* PHY_BAND_2G4 = 0（lmac_mac.h:69），也就是 NL80211_BAND_2GHZ = 0 */
#define AIC_CHAN_BAND_24G 0u
/* 三个标志位都不置：没有 CHAN_NO_IR(1)/CHAN_DISABLED(2)/CHAN_RADAR(4)（lmac_mac.h:99-107） */
#define AIC_CHAN_FLAGS_OPEN 0u
/*
 * 信道发射功率 20 dBm。厂商 CHAN() 宏写死 30（rwnx_main.c:182，厂商自己标了 FIXME），
 * 但 custom regd 落地时 cfg80211 把 chan->max_power **直接改成**规则里的 max_eirp
 * （net/wireless/reg.c:2514-2524 handle_channel_custom），厂商自带 regdb 的 "00" 域
 * 2.4G 规则是 REG_RULE(2380, 2520, 40, 0, 20, 0)（regdb.c:25，eirp 单位为 dBm）⇒ 20。
 */
#define AIC_CHAN_TXPOWER_DBM 20u

/* ---- struct mac_htcapability 的取值（rwnx_msg_tx.c:1932-1940）---- */
/* ht_cap->cap(0) | IEEE80211_HT_CAP_LDPC_CODING(0x0001, ieee80211.h:1528) */
#define AIC_HT_CAPA_INFO 0x0001u
/* ampdu_factor(MAX_AMPDU_64K=3) | ampdu_density(MPDU_DENSITY_16=7) << 2（ieee80211.h:1557/1567/1596）*/
#define AIC_HT_AMPDU_PARAM 0x1Fu
/* mcs_rate[0] = rx_mask[0] = 0xff（单流 MCS0-7，rwnx_main.c:74），其余 9 字节为 0 */
#define AIC_HT_RX_MASK_FIRST 0xFFu
/* mcs_rate[10..11] = rx_highest = 65（rwnx_main.c:75）*/
#define AIC_HT_RX_HIGHEST 65u
/* mcs_rate[12] = tx_params = IEEE80211_HT_MCS_TX_DEFINED(0x01, ieee80211.h:1490）*/
#define AIC_HT_TX_PARAMS 0x01u
/*
 * mcs_rate[16] 是 struct ieee80211_mcs_info 的 16 B 原样拷贝（rwnx_msg_tx.c:1936-1937
 * 逐字节拷）：rx_mask[10] + rx_highest(le16) + tx_params + reserved[3]（ieee80211.h:1459-1474）。
 * 三个字段的字节偏移就从这张表来，不许另算。
 */
#define AIC_HT_MCS_RX_MASK_OFFSET 0u
#define AIC_HT_MCS_RX_HIGHEST_OFFSET 10u
#define AIC_HT_MCS_TX_PARAMS_OFFSET 12u

/* ---- HE 能力（rwnx_main.c:142-170 的 RWNX_HE_CAPABILITIES，D80 里 has_he=false）---- */
/* rx_mcs_80 = tx_mcs_80 = 0xfffa：NSS1 = 2(MCS0-11)，NSS2..8 = 3(不支持) */
#define AIC_HE_MCS_80 0xFFFAu
/* 160 / 80+80 一律 0xffff（全部不支持）*/
#define AIC_HE_MCS_NONE 0xFFFFu
/* ppe_thres = {0x08, 0x1c, 0x07}（rwnx_main.c:169），其余 22 B 为 0 */
#define AIC_HE_PPE_0 0x08u
#define AIC_HE_PPE_1 0x1Cu
#define AIC_HE_PPE_2 0x07u

/* ---- 尾部标量（rwnx_msg_tx.c:1984-1993）---- */
/* tx_lft = RWNX_TX_LIFETIME_MS = 1000（rwnx_tx.h:31）*/
#define AIC_TX_LIFETIME_MS 1000u
/* phy_bw_max = PHY_CHNL_BW_80 = 2（lmac_mac.h:83）；D80 里 use_80 被强制 true（rwnx_msg_tx.c:1899）*/
#define AIC_PHY_BW_80 2u
#define AIC_HT_SUPPORTED 1u   /* ht_cap->ht_supported = true（rwnx_main.c:69）*/
#define AIC_VHT_SUPPORTED 0u  /* vht_cap->vht_supported = false（rwnx_main.c:82）*/
#define AIC_HE_SUPPORTED 0u   /* he_cap->has_he = false（rwnx_main.c:144）*/
#define AIC_HE_UL_ON 0u       /* mod_params->he_ul_on = false（rwnx_mod_params.c:37）*/
#define AIC_PS_ON 1u          /* mod_params->ps_on = true（rwnx_mod_params.c:62）*/
#define AIC_ANT_DIV_ON 1u     /* mod_params->ant_div = true（rwnx_mod_params.c:87）*/
#define AIC_DPSM 0u           /* mod_params->dpsm = false（rwnx_mod_params.c:71）*/

/* ---- struct mm_start_req 的两个标量（rwnx_msg_tx.c:485-487；phy_cfg 由 :485 的 memcpy 来）---- */
#define AIC_UAPSD_TIMEOUT 300u /* mod_params->uapsd_timeout 默认 300（rwnx_mod_params.c:42）*/
#define AIC_LP_CLOCK_ACCURACY 20u /* mod_params->lp_clk_ppm 默认 20（rwnx_mod_params.c:61）*/

/*
 * struct mac_chan_def（lmac_mac.h:109-118）：厂商 5 B 字段 + 1 B 对齐填充 = 6 B/项。
 * 填充显式写出来，数组步长就与厂商一样是 6。
 */
typedef struct {
    UINT16 Frequency; /* +0 MHz */
    UINT8 Band;       /* +2 PHY_BAND_2G4 */
    UINT8 Flags;      /* +3 mac_chan_flags */
    INT8 TxPower;     /* +4 dBm */
    UINT8 TailPad[1]; /* +5 对齐到 2 */
} AIC_MAC_CHAN_DEF;

/* struct me_chan_config_req（lmac_msg.h:1746-1755）：2.4G 14 项 + 5G 28 项 + 两个计数 */
typedef struct {
    AIC_MAC_CHAN_DEF Chan2G4[AIC_CHAN_24G_MAX]; /* +0   */
    AIC_MAC_CHAN_DEF Chan5G[AIC_CHAN_5G_MAX];   /* +84  */
    UINT8 Chan2G4Count;                         /* +252 */
    UINT8 Chan5GCount;                          /* +253 */
} AIC_ME_CHAN_CONFIG;

/*
 * struct me_config_req（lmac_msg.h:1718-1743）。厂商没用 __packed，按自然对齐；
 * 下面把编译器会插的填充**显式写出来**，布局就不再随编译器变化。
 */
typedef struct {
    /* struct mac_htcapability（lmac_mac.h:341-355），占 32 B */
    UINT16 HtCapaInfo;      /* +0  */
    UINT8 AmpduParam;       /* +2  */
    UINT8 McsRate[16];      /* +3  MAX_MCS_LEN = 16，逐字节拷贝*/
    UINT8 HtPad0[1];        /* +19 对齐 u16 */
    UINT16 HtExtendedCapa;  /* +20 */
    UINT8 HtPad1[2];        /* +22 对齐 u32 */
    UINT32 TxBeamforming;   /* +24 */
    UINT8 AselCapa;         /* +28 */
    UINT8 HtPad2[3];        /* +29 尾填充到 32 */
    /* struct mac_vhtcapability（lmac_mac.h:357-367），占 12 B */
    UINT32 VhtCapaInfo;     /* +32 */
    UINT16 VhtRxMcsMap;     /* +36 */
    UINT16 VhtRxHighest;    /* +38 */
    UINT16 VhtTxMcsMap;     /* +40 */
    UINT16 VhtTxHighest;    /* +42 */
    /* struct mac_hecapability（lmac_mac.h:394-403），占 56 B */
    UINT8 HeMacCapInfo[6];  /* +44 MAC_HE_MAC_CAPA_LEN */
    UINT8 HePhyCapInfo[11]; /* +50 MAC_HE_PHY_CAPA_LEN */
    UINT8 HePad0[1];        /* +61 对齐 u16 */
    UINT16 HeRxMcs80;       /* +62 */
    UINT16 HeTxMcs80;       /* +64 */
    UINT16 HeRxMcs160;      /* +66 */
    UINT16 HeTxMcs160;      /* +68 */
    UINT16 HeRxMcs80P80;    /* +70 */
    UINT16 HeTxMcs80P80;    /* +72 */
    UINT8 HePpeThreshold[25]; /* +74 MAC_HE_PPE_THRES_MAX_LEN */
    UINT8 HePad1[1];        /* +99 对齐 u16 */
    /* 尾部标量 */
    UINT16 TxLifetime;      /* +100 */
    UINT8 PhyBandwidthMax;  /* +102 */
    UINT8 HtSupported;      /* +103 */
    UINT8 VhtSupported;     /* +104 */
    UINT8 HeSupported;      /* +105 */
    UINT8 HeUlOn;           /* +106 */
    UINT8 PsOn;             /* +107 */
    UINT8 AntDivOn;         /* +108 */
    UINT8 Dpsm;             /* +109 */
    UINT8 TailPad[2];       /* +110 厂商 sizeof 补齐到 4 的倍数 = 112 */
} AIC_ME_CONFIG;

/* struct mm_start_req（lmac_msg.h:540-547）：phy_cfg(16×u32) + uapsd_timeout + lp_clk_accuracy */
typedef struct {
    UINT32 PhyConfig[16];   /* +0  64 B，全 0（见调研文档 §2：厂商 kzalloc ⇒ 也是 64 个 0）*/
    UINT32 UapsdTimeout;    /* +64 */
    UINT16 LpClockAccuracy; /* +68 */
    UINT8 TailPad[2];       /* +70 厂商 sizeof 补齐到 4 的倍数 = 72（字段和只有 70）*/
} AIC_MM_START;

static void ZeroBytes(UINT8 *Buffer, UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        Buffer[Index] = 0;
    }
}

/* 调它之前，Buffer 必须是 4 B 对齐的（镜像结构体里有 UINT32 成员），调用方保证 */
UINT32 AicMeConfigBuild(UINT8 *Buffer) {
    AIC_ME_CONFIG *Config = (AIC_ME_CONFIG *)Buffer;

    ZeroBytes(Buffer, (UINT32)sizeof(*Config));
    /* ht_cap：厂商从 wiphy 的 ht_cap 抄的是驱动里写死的 RWNX_HT_CAPABILITIES */
    Config->HtCapaInfo = AIC_HT_CAPA_INFO;
    Config->AmpduParam = (UINT8)AIC_HT_AMPDU_PARAM;
    Config->McsRate[AIC_HT_MCS_RX_MASK_OFFSET] = (UINT8)AIC_HT_RX_MASK_FIRST;
    AicPut16(&Config->McsRate[AIC_HT_MCS_RX_HIGHEST_OFFSET], (UINT16)AIC_HT_RX_HIGHEST);
    Config->McsRate[AIC_HT_MCS_TX_PARAMS_OFFSET] = (UINT8)AIC_HT_TX_PARAMS;
    /* ht_extended_capa / tx_beamforming_capa / asel_capa 厂商显式填 0（rwnx_msg_tx.c:1938-1940），
       上面 ZeroBytes 已给；vht 整段也是 0 —— 因为 vht_supp=0，厂商那段 if 根本不进来 */
    Config->HeRxMcs80 = (UINT16)AIC_HE_MCS_80;
    Config->HeTxMcs80 = (UINT16)AIC_HE_MCS_80;
    Config->HeRxMcs160 = (UINT16)AIC_HE_MCS_NONE;
    Config->HeTxMcs160 = (UINT16)AIC_HE_MCS_NONE;
    Config->HeRxMcs80P80 = (UINT16)AIC_HE_MCS_NONE;
    Config->HeTxMcs80P80 = (UINT16)AIC_HE_MCS_NONE;
    Config->HePpeThreshold[0] = (UINT8)AIC_HE_PPE_0;
    Config->HePpeThreshold[1] = (UINT8)AIC_HE_PPE_1;
    Config->HePpeThreshold[2] = (UINT8)AIC_HE_PPE_2;
    Config->TxLifetime = (UINT16)AIC_TX_LIFETIME_MS;
    Config->PhyBandwidthMax = (UINT8)AIC_PHY_BW_80;
    Config->HtSupported = (UINT8)AIC_HT_SUPPORTED;
    Config->VhtSupported = (UINT8)AIC_VHT_SUPPORTED;
    Config->HeSupported = (UINT8)AIC_HE_SUPPORTED;
    Config->HeUlOn = (UINT8)AIC_HE_UL_ON;
    Config->PsOn = (UINT8)AIC_PS_ON;
    Config->AntDivOn = (UINT8)AIC_ANT_DIV_ON;
    Config->Dpsm = (UINT8)AIC_DPSM;
    return (UINT32)sizeof(*Config);
}

UINT32 AicChanConfigBuild(UINT8 *Buffer) {
    AIC_ME_CHAN_CONFIG *Config = (AIC_ME_CHAN_CONFIG *)Buffer;
    UINT32 Index;

    ZeroBytes(Buffer, (UINT32)sizeof(*Config));
    for (Index = 0; Index < AIC_CHAN_24G_USED; Index++) {
        Config->Chan2G4[Index].Frequency =
            (UINT16)(AIC_CHAN_24G_FIRST_FREQ + AIC_CHAN_24G_STEP * Index);
        Config->Chan2G4[Index].Band = (UINT8)AIC_CHAN_BAND_24G;
        Config->Chan2G4[Index].Flags = (UINT8)AIC_CHAN_FLAGS_OPEN;
        Config->Chan2G4[Index].TxPower = (INT8)AIC_CHAN_TXPOWER_DBM;
    }
    /* 第 14 项与整个 5G 段保持 0、两个计数分别是 13 和 0：厂商是按 regdb 填的，我们不搬 regdb */
    Config->Chan2G4Count = (UINT8)AIC_CHAN_24G_USED;
    Config->Chan5GCount = 0u;
    return (UINT32)sizeof(*Config);
}

UINT32 AicMmStartBuild(UINT8 *Buffer) {
    AIC_MM_START *Config = (AIC_MM_START *)Buffer;

    /* phy_cfg 64 B 全 0 是核对过的结论（Archive/Duo-WiFi-MM-START调研.md §2）*/
    ZeroBytes(Buffer, (UINT32)sizeof(*Config));
    Config->UapsdTimeout = AIC_UAPSD_TIMEOUT;
    Config->LpClockAccuracy = (UINT16)AIC_LP_CLOCK_ACCURACY;
    return (UINT32)sizeof(*Config);
}

/* 一条不匹配就停：长度对不上说明镜像结构体被编译器排歪了，发出去只会是垃圾 */
static int LayoutCheckOne(const char *Name, UINT32 Actual, UINT32 Expected) {
    if (Actual == Expected) {
        return 0;
    }
    SdhciMark("WiFi: struct layout BAD: ");
    SdhciMark(Name);
    SdhciMarkHex(" size=", Actual, 4);
    SdhciMarkHex(" want=", Expected, 4);
    SdhciMark("\n");
    return -1;
}

int AicBringupLayoutCheck(void) {
    int Bad = 0;

    Bad |= LayoutCheckOne("me_config_req", (UINT32)sizeof(AIC_ME_CONFIG), AIC_ME_CONFIG_LENGTH);
    Bad |= LayoutCheckOne("me_chan_config_req", (UINT32)sizeof(AIC_ME_CHAN_CONFIG),
                          AIC_ME_CHAN_CONFIG_LENGTH);
    Bad |= LayoutCheckOne("mm_start_req", (UINT32)sizeof(AIC_MM_START), AIC_MM_START_LENGTH);
    if (Bad != 0) {
        SdhciMark("WiFi: struct layout check failed, refuse to send (see report table)\n");
        return -1;
    }
    return 0;
}
