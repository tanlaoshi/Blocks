/*
 * WifiScanMessage.c — `wififw scan` 的两条请求消息，逐字段拼字节（只拼，不发）
 *
 * 用户眼里：`wififw scan dry` 打出来的 add if / scanu start 两个包长什么样。
 * 从哪读：AicMmAddIfBuild / AicScanuStartBuild / AicScanLayoutCheck。
 * 别改：下面三个结构体是厂商 lmac_msg.h 的逐字段镜像（连编译器会插的填充都显式写出），
 *       字段顺序/宽度/偏移必须与厂商一致；AicScanLayoutCheck 核对 sizeof/offsetof，
 *       排歪了就拒绝发送。长度不是手写数字，是厂商头文件编译出来的 sizeof（报告 §1）。
 * 来源：Source/DUO/sdk/.../aic8800_fdrv/{lmac_mac.h,lmac_msg.h,rwnx_msg_tx.c}
 */
#include "WifiScanPrivate.h"
#include "Compiler.h"

/* 本机 STA 地址：locally administered(bit1=1) + unicast(bit0=0) = 02:..:01。
   厂商用 cfg80211 给的 wdev.address（rwnx_msg_tx.c:524 memcpy 原始 6 B）；
   我们没有 cfg80211，固定一个本地管理地址即可，固件只把它当 VIF 的 MAC。 */
#define AIC_SCAN_MAC_LOCAL_UNICAST 0x02u
#define AIC_SCAN_MAC_LAST_BYTE 0x01u

static const UINT8 sAicScanStationAddress[AIC_SCAN_MAC_LENGTH] = {
    AIC_SCAN_MAC_LOCAL_UNICAST, 0u, 0u, 0u, 0u, AIC_SCAN_MAC_LAST_BYTE};

/* 只读访问口：DHCP/ARP 需要"固件给 vif 的 MAC"，但别处不该改它（PR-36） */
const UINT8 *AicScanStationAddress(void) {
    return sAicScanStationAddress;
}

/* 布局核对表：每项是“自建镜像的实际值 vs 厂商编译出来的期望值” */
typedef struct {
    const char *Name;
    UINT32 Actual;
    UINT32 Expected;
} AIC_SCAN_LAYOUT_ITEM;

static const AIC_SCAN_LAYOUT_ITEM sAicScanLayout[] = {
    {"mm_add_if_req", (UINT32)sizeof(AIC_SCAN_ADD_IF), AIC_MM_ADD_IF_LENGTH},
    {"mm_add_if_req.type", (UINT32)BLOCKS_OFFSETOF(AIC_SCAN_ADD_IF, Type), 0u},
    {"mm_add_if_req.addr", (UINT32)BLOCKS_OFFSETOF(AIC_SCAN_ADD_IF, Address), 2u},
    {"mm_add_if_req.p2p", (UINT32)BLOCKS_OFFSETOF(AIC_SCAN_ADD_IF, P2p), 8u},
    {"mm_add_if_cfm", (UINT32)sizeof(AIC_ADD_IF_CFM), AIC_ADD_IF_CFM_LENGTH},
    {"mm_add_if_cfm.status", (UINT32)BLOCKS_OFFSETOF(AIC_ADD_IF_CFM, Status), 0u},
    {"mm_add_if_cfm.inst_nbr", (UINT32)BLOCKS_OFFSETOF(AIC_ADD_IF_CFM, InstanceNumber), 1u},
    {"scanu_start_req", (UINT32)sizeof(AIC_SCANU_START), AIC_SCANU_START_LENGTH},
    {"scanu_start_req.chan", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, Channel), 0u},
    {"scanu_start_req.ssid", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, Ssid), 252u},
    {"scanu_start_req.bssid", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, Bssid), 352u},
    {"scanu_start_req.add_ies", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, AddIes), 360u},
    {"scanu_start_req.add_ie_len", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, AddIeLength),
     364u},
    {"scanu_start_req.vif_idx", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, VifIndex), 366u},
    {"scanu_start_req.chan_cnt", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, ChannelCount),
     367u},
    {"scanu_start_req.ssid_cnt", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, SsidCount), 368u},
    {"scanu_start_req.no_cck", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, NoCck), 369u},
    {"scanu_start_req.duration", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_START, Duration), 372u},
    {"scanu_start_cfm", (UINT32)sizeof(AIC_SCANU_START_CONFIRM), AIC_SCANU_START_CFM_LENGTH},
    {"scanu_result_ind", (UINT32)sizeof(AIC_SCANU_RESULT), AIC_SCANU_RESULT_HEADER},
    {"scanu_result_ind.payload", (UINT32)BLOCKS_OFFSETOF(AIC_SCANU_RESULT, Payload), 12u},
    {"mac_chan_def", (UINT32)sizeof(AIC_SCAN_CHANNEL), 6u},
    {"mac_ssid", (UINT32)sizeof(AIC_SCAN_SSID), 33u},
};

#define AIC_SCAN_LAYOUT_COUNT ((UINT32)(sizeof(sAicScanLayout) / sizeof(sAicScanLayout[0])))

static void ScanClearBuffer(UINT8 *Buffer, UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        Buffer[Index] = 0;
    }
}

/* struct mm_add_if_req（lmac_msg.h:622）：type=MM_STA(0)、addr=本机 STA 地址、p2p=0 */
UINT32 AicMmAddIfBuild(UINT8 *Buffer) {
    AIC_SCAN_ADD_IF *Config = (AIC_SCAN_ADD_IF *)Buffer;
    UINT8 *Address = (UINT8 *)Config->Address;
    UINT32 Index;

    ScanClearBuffer(Buffer, (UINT32)sizeof(*Config));
    Config->Type = (UINT8)AIC_SCAN_IF_TYPE_STATION;
    for (Index = 0; Index < AIC_SCAN_MAC_LENGTH; Index++) {
        Address[Index] = sAicScanStationAddress[Index];
    }
    Config->P2p = (UINT8)AIC_SCAN_P2P_OFF;
    return (UINT32)sizeof(*Config);
}

/*
 * struct scanu_start_req（lmac_msg.h:1586）：
 *   2.4G 的 1..13 信道（band=0、flags=0、tx_power=20）
 *   bssid = 全 FF（厂商 req->bssid = mac_addr_bcst，rwnx_msg_tx.c:2715）
 *   ssid_cnt = 0（广播扫描，不指定 SSID）、chan_cnt = 13、vif_idx 由 ADD_IF_CFM 给
 *   add_ies/add_ie_len/duration/no_cck 全 0（厂商同样只在有 IE 时填）
 */
UINT32 AicScanuStartBuild(UINT8 *Buffer, UINT8 VifIndex) {
    AIC_SCANU_START *Config = (AIC_SCANU_START *)Buffer;
    UINT32 Index;

    ScanClearBuffer(Buffer, (UINT32)sizeof(*Config));
    for (Index = 0; Index < AIC_SCAN_CHANNEL_24G; Index++) {
        Config->Channel[Index].Frequency =
            (UINT16)(AIC_SCAN_CHANNEL_2G4_FIRST + AIC_SCAN_CHANNEL_2G4_STEP * Index);
        Config->Channel[Index].Band = (UINT8)AIC_SCAN_BAND_2G4;
        Config->Channel[Index].Flags = (UINT8)AIC_SCAN_CHANNEL_FLAGS_OPEN;
        Config->Channel[Index].TxPower = (INT8)AIC_SCAN_TXPOWER_DBM;
    }
    /* 第 14 项与整个 5G 段保持 0：本刀只扫 2.4G 的 1..13 */
    for (Index = 0; Index < AIC_SCAN_MAC_LENGTH / 2u; Index++) {
        Config->Bssid[Index] = (UINT16)AIC_SCAN_BSSID_WILDCARD;
    }
    Config->VifIndex = VifIndex;
    Config->ChannelCount = (UINT8)AIC_SCAN_CHANNEL_24G;
    Config->SsidCount = 0u;
    Config->NoCck = 0u;
    Config->AddIes = 0u;
    Config->AddIeLength = 0u;
    Config->Duration = 0u;
    return (UINT32)sizeof(*Config);
}

/* 一条不匹配就停：布局对不上说明镜像被编译器排歪了，发出去只会是垃圾 */
int AicScanLayoutCheck(void) {
    UINT32 Index;
    int Bad = 0;

    for (Index = 0; Index < AIC_SCAN_LAYOUT_COUNT; Index++) {
        const AIC_SCAN_LAYOUT_ITEM *Item = &sAicScanLayout[Index];

        if (Item->Actual == Item->Expected) {
            continue;
        }
        SdhciMark("WiFi: scan struct layout BAD: ");
        SdhciMark(Item->Name);
        SdhciMarkHex(" got=", Item->Actual, 4);
        SdhciMarkHex(" want=", Item->Expected, 4);
        SdhciMark("\n");
        Bad = 1;
    }
    if (Bad != 0) {
        SdhciMark("WiFi: scan struct layout check failed, refuse to send\n");
        return -1;
    }
    return 0;
}
