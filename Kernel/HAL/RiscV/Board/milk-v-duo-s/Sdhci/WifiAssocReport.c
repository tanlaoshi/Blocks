/*
 * WifiAssocReport.c — `wififw join` 的目标/IND 解析与打印（只打印，不改状态）
 *
 * 用户眼里：join 打出来的每一行都在这里 —— 目标是谁、包长多少、RSN IE 是什么、
 *           IND 报了什么。
 * 从哪读：AicJoinResolve / AicJoinPrintTarget / AicJoinDumpRsn / AicJoinHandleInd。
 * 别改：输出文案就是验收判据（Archive/Duo-WiFi-关联与WPA2调研.md §8 贴了原文）；
 *       改文案等于改判据，要一起改。这里不发消息、不碰 SDIO。
 * 判据：IND.status_code == 0 才算关联成功；非 0 按 802.11-2016 Table 9-46 打人话。
 */
#include "WifiAssocPrivate.h"
#include "CString.h"
#include "Compiler.h" /* BLOCKS_OFFSETOF：dry 要在真 buffer 里定位 ie_buf */

/* 2.4G：freq = 2412 + 5*(n-1) ⇒ n = (freq-2412)/5 + 1；范围外 0（与扫描同一套常量） */
static UINT32 JoinFrequencyChannel(UINT16 Frequency) {
    if (Frequency < AIC_SCAN_CHANNEL_2G4_FIRST || Frequency > AIC_SCAN_CHANNEL_2G4_LAST) {
        return 0u;
    }
    return (UINT32)(Frequency - AIC_SCAN_CHANNEL_2G4_FIRST) / AIC_SCAN_CHANNEL_2G4_STEP + 1u;
}

/*
 * 只列"关联阶段真的会碰上"的几个：0/1/10-13/17/18（802.11-2016 Table 9-46）。
 * 其余不猜，打印成 unknown，原始值照样在 hex 里。
 */
static const char *JoinStatusText(UINT16 Code) {
    switch (Code) {
    case 0u:
        return "success";
    case 1u:
        return "unspecified failure";
    case 10u:
        return "AP cannot support all requested capabilities";
    case 11u:
        return "reassociation denied (AP cannot confirm the association)";
    case 12u:
        return "association denied (reason outside this standard)";
    case 13u:
        return "AP does not support all data rates in the request";
    case 17u:
        return "AP cannot handle more associated STAs";
    case 18u:
        return "STA does not support all data rates required by the BSS";
    default:
        return 0;
    }
}

void AicJoinPrintTx(UINT32 Length) {
    SdhciMark("WiFi: sm_connect_req tx len=");
    SdhciMarkDecimal("", Length);
    SdhciMark(" (auth=wpa2-psk(WLAN_AUTH_OPEN=");
    SdhciMarkDecimal("", AIC_JOIN_AUTH_WPA2_PSK);
    SdhciMark(") flags=");
    SdhciMarkHex("0x", AIC_JOIN_FLAGS_WPA2, 8);
    SdhciMark(" eapol=");
    SdhciMarkHex("0x", AIC_JOIN_CTRL_PORT_EAPOL, 4);
    SdhciMark(" ie=");
    SdhciMarkDecimal("", AIC_JOIN_RSN_IE_LENGTH);
    SdhciMark(")\n");
}

/* dry 用：前 64 B 足够看出 ssid/bssid/chan/flags 排歪没有（与 bringup/scan 同宽） */
void AicJoinDump(const UINT8 *Data, UINT32 Length) {
    UINT32 Count = Length > AIC_BRINGUP_DUMP_BYTES ? AIC_BRINGUP_DUMP_BYTES : Length;
    UINT32 Index;

    SdhciMark("WiFi:  dump[");
    SdhciMarkDecimal("", Count);
    SdhciMark("] of ");
    SdhciMarkDecimal("", Length);
    for (Index = 0; Index < Count; Index++) {
        SdhciMarkHex(" ", (UINT32)Data[Index], 2);
    }
    SdhciMark("\n");
}

/*
 * dry 专供：把**已拼好的 buffer**里 ie_buf（+64 起 22 B）按十六进制打出来，并逐字节
 * 与源表对一次（copy=ok/BAD）。为什么不并进 AicJoinDump：那个 dump 只打前 64 B，
 * 而 ie_buf 从 +64 才开始 —— RSN IE 是本刀唯一改过的字段，必须能单独核对。
 */
void AicJoinDumpRsn(const UINT8 *Request, UINT32 RequestLength) {
    const UINT8 *Expected = AicJoinRsnIe();
    const UINT8 *Ie;
    UINT32 Offset = (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, IeBuffer);
    UINT32 Index;
    int Same = 1;

    if (RequestLength < Offset + AIC_JOIN_RSN_IE_LENGTH) {
        SdhciMark("WiFi: join rsn: request too short, no ie_buf\n");
        return;
    }
    Ie = Request + Offset;
    SdhciMark("WiFi: join rsn[");
    SdhciMarkDecimal("", AIC_JOIN_RSN_IE_LENGTH);
    SdhciMark("]@");
    SdhciMarkDecimal("", Offset);
    SdhciMark(" (wpa2-psk ccmp id=");
    SdhciMarkDecimal("", (UINT32)Ie[0]);
    SdhciMark(")");
    for (Index = 0; Index < AIC_JOIN_RSN_IE_LENGTH; Index++) {
        SdhciMarkHex(" ", (UINT32)Ie[Index], 2);
        if (Ie[Index] != Expected[Index]) {
            Same = 0;
        }
    }
    SdhciMark(Same != 0 ? " copy=ok\n" : " copy=BAD\n");
}

static int JoinHexNibble(char Text) {
    if (Text >= '0' && Text <= '9') {
        return Text - '0';
    }
    if (Text >= 'a' && Text <= 'f') {
        return Text - 'a' + 10;
    }
    if (Text >= 'A' && Text <= 'F') {
        return Text - 'A' + 10;
    }
    return -1;
}

/* "aa:bb:cc:dd:ee:ff"（也接受不带分隔符的 12 位）；成功 0 */
static int JoinParseMac(const char *Text, UINT8 *Out) {
    UINT32 Index = 0;
    UINT32 Digit = 0;
    int High = -1;

    while (Text[Index] != 0) {
        char One = Text[Index++];
        int Nibble;

        if (One == ':' || One == '-') {
            continue;
        }
        Nibble = JoinHexNibble(One);
        if (Nibble < 0) {
            return -1;
        }
        if (High < 0) {
            High = Nibble;
            continue;
        }
        if (Digit >= AIC_SCAN_MAC_LENGTH) {
            return -1;
        }
        Out[Digit++] = (UINT8)((High << 4) | Nibble);
        High = -1;
    }
    return (Digit == AIC_SCAN_MAC_LENGTH && High < 0) ? 0 : -1;
}

/*
 * 从"最近一次扫描"的合并表里找目标。返回 0 = 找到，1 = 缓存空/没这个 SSID，
 * -1 = bssid 文本非法。**不打印**：dry 要把"没找到"降级成通配符，真跑才当错误。
 */
int AicJoinResolve(const char *Ssid, const char *BssidText, AIC_SCAN_AP *Out) {
    UINT8 Bssid[AIC_SCAN_MAC_LENGTH];
    const UINT8 *Filter = 0;

    /* 先验 bssid 文本：缓存空的时候也要能说"是 bssid 写错了"，而不是"没扫过" */
    if (BssidText != 0) {
        if (JoinParseMac(BssidText, Bssid) != 0) {
            return -1;
        }
        Filter = Bssid;
    }
    if (AicScanReportCount() == 0u) {
        return 1;
    }
    return AicScanReportFind(Ssid, Filter, Out) == 0 ? 0 : 1;
}

/* dry 的兜底目标：bssid 全 FF、freq=0；真跑永远不会走到这里（真跑必须先有扫描缓存） */
void AicJoinPlaceholder(const char *Ssid, AIC_SCAN_AP *Out) {
    UINT32 Index;

    memset(Out, 0, sizeof(*Out));
    StringCopy(Out->Ssid, sizeof(Out->Ssid), Ssid);
    for (Index = 0; Index < AIC_SCAN_MAC_LENGTH; Index++) {
        Out->Bssid[Index] = 0xFFu;
    }
}

void AicJoinPrintTarget(const AIC_SCAN_AP *Target) {
    char Address[AIC_SCAN_MAC_TEXT_MAX];

    AicScanFormatAddress(Target->Bssid, Address);
    SdhciMark("WiFi: join target: ssid=\"");
    SdhciMark(Target->Ssid);
    SdhciMark("\" bssid=");
    SdhciMark(Address);
    SdhciMark(" freq=");
    SdhciMarkDecimal("", Target->Frequency);
    SdhciMark(" ch=");
    SdhciMarkDecimal("", Target->Channel);
    SdhciMark(" rssi=");
    AicScanMarkSigned((INT32)Target->Rssi);
    SdhciMark("\n");
}

static void JoinPrintIndHead(const AIC_SM_CONNECT_INDICATION *Ind, UINT32 ParamLength) {
    const char *Text = JoinStatusText(Ind->StatusCode);
    char Address[AIC_SCAN_MAC_TEXT_MAX];

    AicScanFormatAddress((const UINT8 *)Ind->Bssid, Address);
    SdhciMark("WiFi: SM_CONNECT_IND: status_code=");
    SdhciMarkHex("0x", Ind->StatusCode, 4);
    SdhciMark(" (");
    SdhciMark(Text != 0 ? Text : "unknown 802.11 status");
    SdhciMark(") bssid=");
    SdhciMark(Address);
    if (ParamLength >= AIC_JOIN_IND_FREQ_END) {
        SdhciMark(" freq=");
        SdhciMarkDecimal("", Ind->CenterFrequency);
        SdhciMark(" ch=");
        SdhciMarkDecimal("", JoinFrequencyChannel(Ind->CenterFrequency));
        SdhciMark(" band=");
        SdhciMarkDecimal("", Ind->Band);
        SdhciMark(" width=");
        SdhciMarkDecimal("", Ind->Width);
    } else {
        /* 固件把 IND 截短了：多半是它只发到 assoc_ie_buf 开头，如实说，不猜信道 */
        SdhciMark(" (params cut before center_freq)");
    }
    SdhciMark("\n");
}

static void JoinPrintIndTail(const AIC_SM_CONNECT_INDICATION *Ind, UINT32 ParamLength) {
    SdhciMark("WiFi:  ind params: plen=");
    SdhciMarkDecimal("", ParamLength);
    SdhciMark(" vif=");
    SdhciMarkDecimal("", Ind->VifIndex);
    SdhciMark(" ap=");
    SdhciMarkDecimal("", Ind->ApIndex);
    SdhciMark(" ch_idx=");
    SdhciMarkDecimal("", Ind->ChIndex);
    SdhciMark(" qos=");
    SdhciMarkDecimal("", Ind->Qos);
    SdhciMark(" acm=");
    SdhciMarkDecimal("", Ind->Acm);
    SdhciMark(" roamed=");
    SdhciMarkDecimal("", Ind->Roamed);
    SdhciMark(" req_ie=");
    SdhciMarkDecimal("", Ind->AssocReqIeLength);
    SdhciMark(" rsp_ie=");
    SdhciMarkDecimal("", Ind->AssocRspIeLength);
    if (ParamLength >= AIC_JOIN_IND_AID_END) {
        SdhciMark(" aid=");
        SdhciMarkHex("0x", Ind->Aid, 4);
    }
    SdhciMark("\n");
}

/*
 * IND 成功后把两个索引留下来：ap_idx 是固件给这个 AP 分配的 sta_idx（0x1404 的 sta_idx、
 * 下一刀 0x0024 的 sta_idx 都用它），vif_idx 是关联所在 vif（与扫描同一个）。
 * 只写本文件的 static，不在失败时清零 —— 失败时调用方根本不会用。
 */
static UINT8 sAicJoinApIndex;
static UINT8 sAicJoinVifIndex;
void AicJoinLastIndexes(UINT8 *VifIndex, UINT8 *ApIndex) {
    *VifIndex = sAicJoinVifIndex;
    *ApIndex = sAicJoinApIndex;
}

/*
 * IND 是**唯一**能判断"关联成没成"的消息：CFM 的 status 只表示"受理"。
 * 失败原因分两类，文案不同：
 *   status_code != 0 → 空口那边明确拒绝（AP 的 (Re)Assoc Response 带回来的状态码）
 *   IND 太短          → 固件消息本身不对，不是关联失败
 */
int AicJoinHandleInd(const UINT8 *Param, UINT32 ParamLength) {
    const AIC_SM_CONNECT_INDICATION *Ind = (const AIC_SM_CONNECT_INDICATION *)Param;
    const char *Text;

    if (ParamLength < AIC_JOIN_IND_HEAD_END) {
        SdhciMarkHex("WiFi: join FAILED at step 2/2: SM_CONNECT_IND too short plen=", ParamLength,
                     4);
        SdhciMark(" (want >= 20; firmware/layout mismatch)\n");
        return -1;
    }
    JoinPrintIndHead(Ind, ParamLength);
    JoinPrintIndTail(Ind, ParamLength);
    if (Ind->StatusCode != 0u) {
        Text = JoinStatusText(Ind->StatusCode);
        SdhciMarkHex("WiFi: join FAILED at step 2/2: SM_CONNECT_IND status_code=0x",
                     Ind->StatusCode, 4);
        SdhciMark(" (");
        SdhciMark(Text != 0 ? Text : "unknown 802.11 status");
        SdhciMark(")\n");
        return -1;
    }
    sAicJoinApIndex = Ind->ApIndex;
    sAicJoinVifIndex = Ind->VifIndex;
    SdhciMark("WiFi: join ok (associated, wpa2-psk/ccmp; AP should send EAPOL M1 next)\n");
    return 0;
}
