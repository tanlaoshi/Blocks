/*
 * WifiAssocMessage.c — SM_CONNECT_REQ(0x1800) 的逐字段拼字节（只拼，不发）
 *
 * 用户眼里：`wififw join dry <ssid>` 打出来的那个包长什么样、每个字段值从哪来。
 * 从哪读：AicJoinBuild / AicJoinLayoutCheck。
 * 别改：AIC_SM_CONNECT / AIC_SM_CONNECT_CONFIRM / AIC_SM_CONNECT_INDICATION 是厂商
 *       lmac_msg.h 的逐字段镜像，字段顺序/宽度/偏移必须与厂商一致；发送前
 *       AicJoinLayoutCheck 核对 sizeof/offsetof，排歪了就拒绝发送 —— 不猜。
 * 来源：aic8800_fdrv/{lmac_msg.h,lmac_mac.h,rwnx_msg_tx.c:2348-2467}
 */
#include "WifiAssocPrivate.h"
#include "Compiler.h"

/*
 * 布局核对表：每项是"自建镜像的实际值 vs 厂商头文件编译出来的期望值"。
 * 期望值不是手算的：Archive/Duo-WiFi-关联与WPA2调研.md §2 的 probe.c 用
 * `sizeof/offsetof` 把它们打出来，这里逐条抄进去。
 */
typedef struct {
    const char *Name;
    UINT32 Actual;
    UINT32 Expected;
} AIC_JOIN_LAYOUT_ITEM;

static const AIC_JOIN_LAYOUT_ITEM sAicJoinLayout[] = {
    {"sm_connect_req", (UINT32)sizeof(AIC_SM_CONNECT), AIC_SM_CONNECT_LENGTH},
    {"sm_connect_req.ssid", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, Ssid), 1u},
    {"sm_connect_req.ssid.length", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, SsidLength), 0u},
    {"sm_connect_req.bssid", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, Bssid), 34u},
    {"sm_connect_req.chan", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, Chan), 40u},
    {"sm_connect_req.flags", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, Flags), 48u},
    {"sm_connect_req.ctrl_port_ethertype",
     (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, CtrlPortEthertype), 52u},
    {"sm_connect_req.ie_len", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, IeLength), 54u},
    {"sm_connect_req.listen_interval", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, ListenInterval),
     56u},
    {"sm_connect_req.dont_wait_bcmc", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, DontWaitBcmc),
     58u},
    {"sm_connect_req.auth_type", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, AuthType), 59u},
    {"sm_connect_req.uapsd_queues", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, UapsdQueues), 60u},
    {"sm_connect_req.vif_idx", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, VifIndex), 61u},
    {"sm_connect_req.ie_buf", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, IeBuffer), 64u},
    {"sm_connect_req.ie_buf bytes", (UINT32)sizeof(((AIC_SM_CONNECT *)0)->IeBuffer), 256u},
    {"mac_chan_def", (UINT32)sizeof(AIC_SCAN_CHANNEL), 6u},
    {"mac_chan_def.freq", (UINT32)BLOCKS_OFFSETOF(AIC_SCAN_CHANNEL, Frequency), 0u},
    {"sm_connect_cfm", (UINT32)sizeof(AIC_SM_CONNECT_CONFIRM), AIC_SM_CONNECT_CFM_LENGTH},
    {"sm_connect_ind", (UINT32)sizeof(AIC_SM_CONNECT_INDICATION), AIC_SM_CONNECT_IND_LENGTH},
    {"sm_connect_ind.status_code", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, StatusCode),
     0u},
    {"sm_connect_ind.bssid", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, Bssid), 2u},
    {"sm_connect_ind.roamed", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, Roamed), 8u},
    {"sm_connect_ind.vif_idx", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, VifIndex), 9u},
    {"sm_connect_ind.ap_idx", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, ApIndex), 10u},
    {"sm_connect_ind.ch_idx", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, ChIndex), 11u},
    {"sm_connect_ind.qos", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, Qos), 12u},
    {"sm_connect_ind.acm", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, Acm), 13u},
    {"sm_connect_ind.assoc_req_ie_len",
     (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, AssocReqIeLength), 14u},
    {"sm_connect_ind.assoc_rsp_ie_len",
     (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, AssocRspIeLength), 16u},
    {"sm_connect_ind.assoc_ie_buf",
     (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, AssocIeBuffer), 20u},
    {"sm_connect_ind.aid", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, Aid), 820u},
    {"sm_connect_ind.band", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, Band), 822u},
    {"sm_connect_ind.center_freq",
     (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, CenterFrequency), 824u},
    {"sm_connect_ind.width", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, Width), 826u},
    {"sm_connect_ind.center_freq1",
     (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, CenterFreq1), 828u},
    {"sm_connect_ind.center_freq2",
     (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, CenterFreq2), 832u},
    {"sm_connect_ind.ac_param", (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT_INDICATION, AcParam),
     836u},
};

#define AIC_JOIN_LAYOUT_COUNT ((UINT32)(sizeof(sAicJoinLayout) / sizeof(sAicJoinLayout[0])))

static void JoinClearBuffer(UINT8 *Buffer, UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        Buffer[Index] = 0;
    }
}

/* 扫描缓存的 SSID 是 char[33] 且保证有结尾 0；这里仍按 32 字节封顶，防上游漏写 */
static UINT32 JoinSsidLength(const char *Ssid) {
    UINT32 Length = 0;

    while (Length < AIC_JOIN_SSID_MAX && Ssid[Length] != 0) {
        Length++;
    }
    return Length;
}

/*
 * 22 B RSN IE：WPA2-PSK + CCMP。逐行对照 802.11-2016 §9.4.2.24 的元素结构，
 * 每个字节的含义与出处见 WifiAssocPrivate.h 的 AIC_JOIN_RSN_* 注释：
 *   30 14            元素 id=48(RSN) / 长度=20
 *   01 00            版本 1
 *   00 0f ac 04      组播套件 CCMP（00-0F-AC:4）
 *   01 00            单播套件数 1
 *   00 0f ac 04      单播套件 CCMP
 *   01 00            AKM 套件数 1
 *   00 0f ac 02      AKM PSK（00-0F-AC:2）
 *   00 00            RSN 能力位：不做 MFP / 不预认证 / 不要求 PMF
 * 不能逐字节拼错：AP 按这 22 B 决定给不给 M1（不带它 = status_code 12，
 * 真机 PR-32 第二刀已经撞过一次）。
 */
static const UINT8 sAicJoinRsnIe[AIC_JOIN_RSN_IE_LENGTH] = {
    (UINT8)AIC_JOIN_RSN_IE_ID, (UINT8)AIC_JOIN_RSN_IE_BODY_LENGTH,
    (UINT8)AIC_JOIN_RSN_VERSION, 0x00u,
    0x00u, 0x0Fu, 0xACu, (UINT8)AIC_JOIN_RSN_SUITE_CCMP,
    (UINT8)AIC_JOIN_RSN_SUITE_SUBCOUNT, 0x00u,
    0x00u, 0x0Fu, 0xACu, (UINT8)AIC_JOIN_RSN_SUITE_CCMP,
    (UINT8)AIC_JOIN_RSN_SUITE_SUBCOUNT, 0x00u,
    0x00u, 0x0Fu, 0xACu, (UINT8)AIC_JOIN_RSN_AKM_PSK,
    (UINT8)(AIC_JOIN_RSN_CAPABILITIES & 0xFFu), (UINT8)(AIC_JOIN_RSN_CAPABILITIES >> 8)
};

const UINT8 *AicJoinRsnIe(void) {
    return sAicJoinRsnIe;
}

/* 编译期自检：22 B 的 RSN IE 必须塞得进 ie_buf（256 B）。负数数组下标 = 直接编译报错 */
typedef char AicJoinRsnFitsIeBuffer[(AIC_JOIN_RSN_IE_LENGTH <=
                                     sizeof(((AIC_SM_CONNECT *)0)->IeBuffer)) ? 1 : -1];

/*
 * struct sm_connect_req 的填法，对照厂商 rwnx_send_sm_connect_req（rwnx_msg_tx.c:2348-2467）：
 *   ssid         ← 扫描缓存里的 SSID（厂商抄 sme->ssid / sme->ssid_len）
 *   bssid        ← 扫描缓存里的 BSSID（厂商抄 sme->bssid；没给才用广播地址）
 *   chan         ← 扫描缓存里的频率；band=2.4G、flags=0、tx_power=0（厂商只填前三个）
 *   flags        ← WPA2：CONTROL_PORT_HOST | WPA_WPA2_IN_USE（:2388-2389 / :2394-2395）
 *   ctrl_port_ethertype ← 0x888e（EAPOL）
 *   ie_len/ie_buf← 22 B RSN IE（厂商抄 sme->ie / sme->ie_len，:2421-2425）
 *   listen_interval / dont_wait_bcmc / uapsd_queues ← 厂商 mod_params 默认值
 *   auth_type    ← WLAN_AUTH_OPEN（WPA2-PSK 就是 OPEN，见 WifiAssocPrivate.h）
 *   vif_idx      ← MM_ADD_IF_CFM 给的 inst_nbr（扫描时建的同一个 vif）
 */
UINT32 AicJoinBuild(UINT8 *Buffer, const AIC_SCAN_AP *Target, UINT8 VifIndex) {
    AIC_SM_CONNECT *Config = (AIC_SM_CONNECT *)Buffer;
    UINT8 *BssidBytes = (UINT8 *)Config->Bssid;
    UINT8 *IeBytes = (UINT8 *)Config->IeBuffer;
    UINT32 Index;

    JoinClearBuffer(Buffer, (UINT32)sizeof(*Config));
    Config->SsidLength = (UINT8)JoinSsidLength(Target->Ssid);
    for (Index = 0; Index < Config->SsidLength; Index++) {
        Config->Ssid[Index] = (UINT8)Target->Ssid[Index];
    }
    /* struct mac_addr 是 u16[3]；按字节写就是厂商 memcpy 6 B 的效果（小端） */
    for (Index = 0; Index < AIC_JOIN_MAC_LENGTH; Index++) {
        BssidBytes[Index] = Target->Bssid[Index];
    }
    Config->Chan.Frequency = Target->Frequency;
    Config->Chan.Band = (UINT8)AIC_JOIN_BAND_2G4;
    Config->Chan.Flags = (UINT8)AIC_SCAN_CHANNEL_FLAGS_OPEN;
    Config->Chan.TxPower = 0; /* 厂商 req 由 rwnx_msg_zalloc 清零，只填 freq/band/flags */
    Config->Flags = AIC_JOIN_FLAGS_WPA2;
    Config->CtrlPortEthertype = (UINT16)AIC_JOIN_CTRL_PORT_EAPOL;
    Config->IeLength = (UINT16)AIC_JOIN_RSN_IE_LENGTH;
    /* ie_buf 是 u32[64]；按字节写，和厂商 memcpy(req->ie_buf, sme->ie, ie_len) 等价 */
    for (Index = 0; Index < AIC_JOIN_RSN_IE_LENGTH; Index++) {
        IeBytes[Index] = sAicJoinRsnIe[Index];
    }
    Config->ListenInterval = (UINT16)AIC_JOIN_LISTEN_INTERVAL;
    Config->DontWaitBcmc = (UINT8)AIC_JOIN_DONT_WAIT_BCMC;
    Config->AuthType = (UINT8)AIC_JOIN_AUTH_WPA2_PSK;
    Config->UapsdQueues = (UINT8)AIC_JOIN_UAPSD_QUEUES;
    Config->VifIndex = VifIndex;
    return (UINT32)sizeof(*Config);
}

/* 一条不匹配就停：布局对不上说明镜像被编译器排歪了，发出去只会是垃圾 */
int AicJoinLayoutCheck(void) {
    UINT32 Index;
    int Bad = 0;

    for (Index = 0; Index < AIC_JOIN_LAYOUT_COUNT; Index++) {
        const AIC_JOIN_LAYOUT_ITEM *Item = &sAicJoinLayout[Index];

        if (Item->Actual == Item->Expected) {
            continue;
        }
        SdhciMark("WiFi: join struct layout BAD: ");
        SdhciMark(Item->Name);
        SdhciMarkHex(" got=", Item->Actual, 4);
        SdhciMarkHex(" want=", Item->Expected, 4);
        SdhciMark("\n");
        Bad = 1;
    }
    if (Bad != 0) {
        SdhciMark("WiFi: join struct layout check failed, refuse to send\n");
        return -1;
    }
    return 0;
}
