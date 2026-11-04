/*
 * AssocReportIndication.c — SM_CONNECT_IND 解析/打印与索引记忆
 *
 * 自 AssocReport.c 拆出（行数债）。IND 是关联成败的唯一判据（CFM 只表示受理）。
 */
#include "Wifi/AssocPrivate.h"

/* 2.4G：freq = 2412 + 5*(n-1) ⇒ n = (freq-2412)/5 + 1；范围外 0 */
static UINT32 JoinFrequencyChannel(UINT16 Frequency) {
    if (Frequency < AIC_SCAN_CHANNEL_2G4_FIRST || Frequency > AIC_SCAN_CHANNEL_2G4_LAST) {
        return 0u;
    }
    return (UINT32)(Frequency - AIC_SCAN_CHANNEL_2G4_FIRST) / AIC_SCAN_CHANNEL_2G4_STEP + 1u;
}

/*
 * 只列关联阶段真会碰上的几个：0/1/10-13/17/18（802.11-2016 Table 9-46）。
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

static void JoinPrintIndicationHead(const AIC_SM_CONNECT_INDICATION *Indication,
                                    UINT32 ParameterLength) {
    const char *Text = JoinStatusText(Indication->StatusCode);
    char Address[AIC_SCAN_MAC_TEXT_MAX];

    AicScanFormatAddress((const UINT8 *)Indication->Bssid, Address);
    SdhciMark("WiFi: SM_CONNECT_IND: status_code=");
    SdhciMarkHex("0x", Indication->StatusCode, 4);
    SdhciMark(" (");
    SdhciMark(Text != 0 ? Text : "unknown 802.11 status");
    SdhciMark(") bssid=");
    SdhciMark(Address);
    if (ParameterLength >= AIC_JOIN_IND_FREQ_END) {
        SdhciMark(" freq=");
        SdhciMarkDecimal("", Indication->CenterFrequency);
        SdhciMark(" ch=");
        SdhciMarkDecimal("", JoinFrequencyChannel(Indication->CenterFrequency));
        SdhciMark(" band=");
        SdhciMarkDecimal("", Indication->Band);
        SdhciMark(" width=");
        SdhciMarkDecimal("", Indication->Width);
    } else {
        SdhciMark(" (params cut before center_freq)");
    }
    SdhciMark("\n");
}

static void JoinPrintIndicationTail(const AIC_SM_CONNECT_INDICATION *Indication,
                                    UINT32 ParameterLength) {
    SdhciMark("WiFi:  ind params: plen=");
    SdhciMarkDecimal("", ParameterLength);
    SdhciMark(" vif=");
    SdhciMarkDecimal("", Indication->VifIndex);
    SdhciMark(" ap=");
    SdhciMarkDecimal("", Indication->ApIndex);
    SdhciMark(" ch_idx=");
    SdhciMarkDecimal("", Indication->ChIndex);
    SdhciMark(" qos=");
    SdhciMarkDecimal("", Indication->Qos);
    SdhciMark(" acm=");
    SdhciMarkDecimal("", Indication->Acm);
    SdhciMark(" roamed=");
    SdhciMarkDecimal("", Indication->Roamed);
    SdhciMark(" req_ie=");
    SdhciMarkDecimal("", Indication->AssocReqIeLength);
    SdhciMark(" rsp_ie=");
    SdhciMarkDecimal("", Indication->AssocRspIeLength);
    if (ParameterLength >= AIC_JOIN_IND_AID_END) {
        SdhciMark(" aid=");
        SdhciMarkHex("0x", Indication->Aid, 4);
    }
    SdhciMark("\n");
}

/*
 * IND 成功后把两个索引留下来：ap_idx 是固件给这个 AP 分配的 sta_idx，
 * vif_idx 是关联所在 vif。只写本文件 static，失败时调用方不会用。
 */
static UINT8 sAicJoinApIndex;
static UINT8 sAicJoinVifIndex;

void AicJoinLastIndexes(UINT8 *VifIndex, UINT8 *ApIndex) {
    *VifIndex = sAicJoinVifIndex;
    *ApIndex = sAicJoinApIndex;
}

void AicJoinIndexesClear(void) {
    sAicJoinApIndex = 0;
    sAicJoinVifIndex = 0;
}

/*
 * IND 是唯一能判断「关联成没成」的消息：CFM 的 status 只表示「受理」。
 *   status_code != 0 → 空口拒绝
 *   IND 太短         → 固件消息本身不对
 */
int AicJoinHandleInd(const UINT8 *Param, UINT32 ParamLength) {
    const AIC_SM_CONNECT_INDICATION *Indication = (const AIC_SM_CONNECT_INDICATION *)Param;
    const char *Text;

    if (ParamLength < AIC_JOIN_IND_HEAD_END) {
        SdhciMarkHex("WiFi: join FAILED at step 2/2: SM_CONNECT_IND too short plen=", ParamLength,
                     4);
        SdhciMark(" (want >= 20; firmware/layout mismatch)\n");
        return -1;
    }
    JoinPrintIndicationHead(Indication, ParamLength);
    JoinPrintIndicationTail(Indication, ParamLength);
    if (Indication->StatusCode != 0u) {
        Text = JoinStatusText(Indication->StatusCode);
        SdhciMarkHex("WiFi: join FAILED at step 2/2: SM_CONNECT_IND status_code=0x",
                     Indication->StatusCode, 4);
        SdhciMark(" (");
        SdhciMark(Text != 0 ? Text : "unknown 802.11 status");
        SdhciMark(")\n");
        return -1;
    }
    sAicJoinApIndex = Indication->ApIndex;
    sAicJoinVifIndex = Indication->VifIndex;
    SdhciMark("WiFi: join ok (associated, wpa2-psk/ccmp; AP should send EAPOL M1 next)\n");
    return 0;
}
