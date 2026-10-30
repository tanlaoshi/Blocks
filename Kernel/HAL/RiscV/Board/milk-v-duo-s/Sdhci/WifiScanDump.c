/*
 * WifiScanDump.c — `wififw scan verbose` 的原始字节取证
 *
 * 用户眼里：每条 scan result 后面多出的那几行 hex、802.11 帧头字段和 IE 走链；
 *           非 0x1004 的消息（真机上的 0x004f）也会多打一行身份与字段。
 * 从哪读：AicScanDumpVerbose（单条结果）/ AicScanReportIgnored（旁路消息）。
 * 别改：这里**只读不判** —— 不纠正偏移、不猜 SSID、不吞帧；解析结论仍归
 *       AicScanResultPrint。串口文案是对外判据（报告 §1 贴了原文），只写 ASCII。
 *       判据必须留：`payload_fc … : match`、`ie@36` 首行、`left=`；
 *       其余压到最小：payload 头 32 B、IE 只列前 6 条、不再并排打 24 起点。
 * 为什么单独一个文件：WifiScanResult.c 只管“解出 SSID/ch”，这里是“把字节原样摊开”，
 *       两件事；混一起既过 60 行也说不清职责。
 * 来源：payload 起点 = AIC_E2A_PARAM_OFF(16) + scanu_result_ind 头(12)；
 *       IE 起点 = 36（rwnx_msg_rx.c:662-663，见 WifiScanPrivate.h 的 AIC_DOT11_IE_OFFSET）。
 */
#include "WifiScanPrivate.h"

/* 变长消息里的 u32 字段（厂商 u32_l 是小端、自然对齐） */
static UINT32 ScanGet32(const UINT8 *At) {
    UINT32 Value = 0;
    UINT32 Index;

    for (Index = AIC_SCAN_WORD_BYTES; Index > 0u; Index--) {
        Value = (Value << AIC_SCAN_BYTE_BITS) | (UINT32)At[Index - 1u];
    }
    return Value;
}

/* 一行 16 B：offset + 字节，肉眼对着 Wireshark 比最快 */
static void ScanDumpHex(const char *Tag, const UINT8 *Data, UINT32 Length) {
    UINT32 Offset;

    for (Offset = 0; Offset < Length; Offset += AIC_SCAN_HEX_COLUMNS) {
        UINT32 Limit = Offset + AIC_SCAN_HEX_COLUMNS;
        UINT32 Index;

        if (Limit > Length) {
            Limit = Length;
        }
        SdhciMark(Tag);
        SdhciMarkHex("", Offset, 4);
        SdhciMark(":");
        for (Index = Offset; Index < Limit; Index++) {
            SdhciMarkHex(" ", (UINT32)Data[Index], 2);
        }
        SdhciMark("\n");
    }
}

/* 只认我们会的两种管理子类型；其余的只打数字，不替它编名字 */
static const char *ScanSubtypeName(UINT32 Type, UINT32 Subtype) {
    if (Type != AIC_DOT11_TYPE_MANAGEMENT) {
        return "?";
    }
    if (Subtype == AIC_DOT11_SUBTYPE_BEACON) {
        return "beacon";
    }
    if (Subtype == AIC_DOT11_SUBTYPE_PROBE_RESPONSE) {
        return "probe-rsp";
    }
    return "?";
}

/* 帧头 24 B 逐字段：fc / dur / addr1 / addr2 / addr3 / seq */
static void ScanDumpDot11Header(const UINT8 *Payload, UINT32 Length) {
    UINT32 FrameControl;
    UINT32 Type;
    UINT32 Subtype;
    UINT32 Sequence;
    char Address1[AIC_SCAN_MAC_TEXT_MAX];
    char Address2[AIC_SCAN_MAC_TEXT_MAX];
    char Address3[AIC_SCAN_MAC_TEXT_MAX];

    if (Length < AIC_DOT11_HEADER_LENGTH) {
        SdhciMark("WiFi:   dot11: shorter than 24 B, no header decode\n");
        return;
    }
    FrameControl = AicGet16(&Payload[0]);
    Type = FrameControl & AIC_DOT11_TYPE_MASK;
    Subtype = (FrameControl >> AIC_DOT11_SUBTYPE_SHIFT) & AIC_DOT11_SUBTYPE_MASK;
    Sequence = AicGet16(&Payload[AIC_DOT11_SEQUENCE_OFFSET]);
    AicScanFormatAddress(&Payload[AIC_DOT11_ADDRESS1_OFFSET], Address1);
    AicScanFormatAddress(&Payload[AIC_DOT11_ADDRESS2_OFFSET], Address2);
    AicScanFormatAddress(&Payload[AIC_DOT11_ADDRESS3_OFFSET], Address3);
    SdhciMarkHex("WiFi:   dot11: fc=", FrameControl, 4);
    SdhciMarkDecimal(" ver=", FrameControl & AIC_DOT11_VERSION_MASK);
    SdhciMarkDecimal(" type=", Type);
    SdhciMarkDecimal(" subtype=", Subtype);
    SdhciMark("(");
    SdhciMark(ScanSubtypeName(Type, Subtype));
    SdhciMarkHex(") flags=", (FrameControl >> AIC_DOT11_FLAGS_SHIFT) & AIC_DOT11_FLAGS_MASK, 2);
    SdhciMarkHex(" dur=", AicGet16(&Payload[AIC_DOT11_DURATION_OFFSET]), 4);
    SdhciMarkDecimal(" seq=", Sequence >> AIC_DOT11_SEQUENCE_NUMBER_SHIFT);
    SdhciMarkDecimal(" frag=", Sequence & AIC_DOT11_SEQUENCE_FRAG_MASK);
    SdhciMark("\n");
    SdhciMark("WiFi:   dot11: a1=");
    SdhciMark(Address1);
    SdhciMark(" a2=");
    SdhciMark(Address2);
    SdhciMark(" a3=");
    SdhciMark(Address3);
    SdhciMark("\n");
}

/* 一个 IE：offset / id / len / 前 8 B 体（SSID 就是它自己那几 B 的 ASCII） */
static void ScanDumpOneIe(const UINT8 *Payload, UINT32 Index, UINT8 ElementId,
                          UINT8 ElementLength, const char *Tag) {
    UINT32 Body = Index + AIC_IE_HEADER_LENGTH;
    UINT32 Show = ElementLength > AIC_SCAN_IE_BODY_SHOW ? AIC_SCAN_IE_BODY_SHOW : ElementLength;
    UINT32 k;

    SdhciMark("WiFi:   ");
    SdhciMark(Tag);
    SdhciMarkHex(": off=", Index, 3);
    SdhciMarkHex(" id=", (UINT32)ElementId, 2);
    SdhciMarkHex(" len=", (UINT32)ElementLength, 2);
    for (k = 0; k < Show; k++) {
        SdhciMarkHex(" ", (UINT32)Payload[Body + k], 2);
    }
    if (ElementLength > Show) {
        SdhciMark(" ..");
    }
    SdhciMark("\n");
}

/*
 * 从 Start 走一遍 IE 链并逐条打印，返回走到的终点。
 * 终点是判 FCS 的凭据：end 与 declared 相等 ⇒ 帧里没 FCS；差 4 ⇒ 帧尾带 4 B FCS。
 */
static UINT32 ScanDumpIes(const UINT8 *Payload, UINT32 Length, UINT32 Start, const char *Tag) {
    UINT32 Index = Start;
    UINT32 Shown = 0;

    while (Index + AIC_IE_HEADER_LENGTH <= Length) {
        UINT8 ElementId = Payload[Index];
        UINT8 ElementLength = Payload[Index + 1u];
        UINT32 Body = Index + AIC_IE_HEADER_LENGTH;

        if (Body + ElementLength > Length) {
            SdhciMark("WiFi:   ");
            SdhciMark(Tag);
            SdhciMarkHex(": off=", Index, 3);
            SdhciMarkHex(" id=", (UINT32)ElementId, 2);
            SdhciMarkHex(" len=", (UINT32)ElementLength, 2);
            SdhciMark(" runs past frame, chain stops here\n");
            return Index;
        }
        if (Shown < AIC_SCAN_IE_PRINT_MAX) {
            ScanDumpOneIe(Payload, Index, ElementId, ElementLength, Tag);
        }
        Shown++;
        Index = Body + ElementLength;
    }
    if (Shown > AIC_SCAN_IE_PRINT_MAX) {
        SdhciMark("WiFi:   ");
        SdhciMark(Tag);
        SdhciMarkDecimal(": ", Shown - AIC_SCAN_IE_PRINT_MAX);
        SdhciMark(" more IEs not printed\n");
    }
    return Index;
}

/* 太短/非 802.11：只摊字节，不下结论 */
static void ScanDumpRawOnly(const UINT8 *Frame, UINT32 FrameLength) {
    UINT32 Want = AIC_E2A_PARAM_OFF + AIC_SCAN_HEX_HEAD_BYTES;
    UINT32 Show = FrameLength < Want ? FrameLength : Want;

    SdhciMark("WiFi:   raw frame head (16 B e2a header included):\n");
    ScanDumpHex("WiFi:     ", Frame, Show);
}

/*
 * 帧体取证：payload 前 32 B、24 B 帧头逐字段、IE 走链（只列前 6 条）。
 * 只走 36 这一条链：IE 起点 36 已被真机证明（SSID="Apple"/"CMCC-cURu"），
 * 不再并排打 24 起点 —— 那条纯粹是上一刀的历史对照，现在只占屏。
 */
static void ScanDumpPayload(const UINT8 *Payload, UINT32 Length, UINT32 Declared) {
    UINT32 Show = Length < AIC_SCAN_HEX_HEAD_BYTES ? Length : AIC_SCAN_HEX_HEAD_BYTES;
    UINT32 End;

    SdhciMarkDecimal("WiFi:   payload head ", Show);
    SdhciMarkDecimal(" B of ", Length);
    SdhciMark(":\n");
    ScanDumpHex("WiFi:     ", Payload, Show);
    ScanDumpDot11Header(Payload, Length);
    End = ScanDumpIes(Payload, Length, AIC_DOT11_IE_OFFSET, "ie@36");
    SdhciMarkHex("WiFi:   ie@36 end=", End, 4);
    SdhciMarkHex(" declared=", Declared, 4);
    SdhciMarkDecimal(" left=", Declared > End ? Declared - End : 0u);
    SdhciMark(" (left=4 => 4 B FCS still in frame)\n");
}

void AicScanDumpVerbose(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength, UINT32 Index) {
    const AIC_SCANU_RESULT *Result;
    const UINT8 *Payload;
    UINT32 Available;
    UINT32 Declared;
    UINT32 Length;
    UINT32 PayloadOffset = AIC_E2A_PARAM_OFF + AIC_SCANU_RESULT_HEADER;

    (void)Index; /* 结果汇总行已经带 #N，dump 不再重复 */
    SdhciMarkHex("WiFi:   ind: flen=", FrameLength, 4);
    SdhciMarkHex(" plen=", ParamLength, 4);
    SdhciMarkHex(" off=", PayloadOffset, 4);
    if (FrameLength < PayloadOffset || ParamLength < AIC_SCANU_RESULT_HEADER) {
        SdhciMark("\nWiFi:   frame/param too short, payload not locatable\n");
        ScanDumpRawOnly(Frame, FrameLength);
        return;
    }
    Result = (const AIC_SCANU_RESULT *)(Frame + AIC_E2A_PARAM_OFF);
    Payload = (const UINT8 *)Result->Payload;
    Available = ParamLength - AIC_SCANU_RESULT_HEADER;
    Declared = Result->Length;
    Length = Declared < Available ? Declared : Available;
    SdhciMarkHex(" len=", Declared, 4);
    SdhciMarkHex(" fc=", Result->FrameControl, 4);
    SdhciMarkDecimal(" freq=", Result->CenterFrequency);
    SdhciMarkHex(" band=", Result->Band, 2);
    SdhciMarkHex(" sta=", Result->StationIndex, 2);
    SdhciMarkHex(" vif=", Result->InstanceNumber, 2);
    SdhciMark(" rssi=");
    AicScanMarkSigned((INT32)Result->Rssi);
    SdhciMark("\n");
    /* 帧头前 2 B 与 ind.framectrl 必须相等：不等就说明 payload 起点不在帧头第一字节 */
    SdhciMarkHex("WiFi:   payload_fc=", AicGet16(Payload), 4);
    SdhciMarkHex(" ind_framectrl=", Result->FrameControl, 4);
    SdhciMarkHex(" bytes_after_header=", Available, 4);
    SdhciMark(AicGet16(Payload) == Result->FrameControl ? ": match\n" : ": MISMATCH\n");
    ScanDumpHex("WiFi:   e2a:  ", Frame, AIC_E2A_PARAM_OFF);
    ScanDumpPayload(Payload, Length, Declared);
}

/* 0x004f：扫描期间每个信道的测量，不是错误；只有 verbose 才解字段 */
static void ScanReportSurvey(const UINT8 *Param) {
    SdhciMark(" (MM_CHANNEL_SURVEY_IND)");
    SdhciMarkDecimal(" freq=", (UINT32)AicGet16(&Param[AIC_MM_SURVEY_FREQUENCY_OFFSET]));
    SdhciMark(" noise=");
    AicScanMarkSigned((INT32)(INT8)Param[AIC_MM_SURVEY_NOISE_OFFSET]);
    SdhciMarkDecimal(" time=", ScanGet32(&Param[AIC_MM_SURVEY_TIME_OFFSET]));
    SdhciMarkDecimal("ms busy=", ScanGet32(&Param[AIC_MM_SURVEY_BUSY_OFFSET]));
    SdhciMark("ms\n");
}

/*
 * 收包阶段遇到的非 SCANU_RESULT_IND 消息。
 * 正常模式保持原样（一行 hex，验收判据不变）；verbose 才多解一层身份/字段。
 */
void AicScanReportIgnored(UINT16 Id, UINT32 ParamLength, const UINT8 *Frame, int Verbose) {
    const UINT8 *Param = Frame + AIC_E2A_PARAM_OFF;
    UINT32 Show;
    UINT32 Index;

    SdhciMarkHex("WiFi: scan rx ignored id=", (UINT32)Id, 4);
    SdhciMarkHex(" plen=", ParamLength, 4);
    if (Verbose == AIC_SCAN_VERBOSE_OFF) {
        SdhciMark("\n");
        return;
    }
    if (Id == (UINT16)AIC_MM_CHANNEL_SURVEY_IND && ParamLength >= AIC_MM_CHANNEL_SURVEY_LENGTH) {
        ScanReportSurvey(Param);
        return;
    }
    Show = ParamLength < AIC_MM_CHANNEL_SURVEY_LENGTH ? ParamLength : AIC_MM_CHANNEL_SURVEY_LENGTH;
    SdhciMark(" (unknown id) param=");
    for (Index = 0; Index < Show; Index++) {
        SdhciMarkHex(" ", (UINT32)Param[Index], 2);
    }
    SdhciMark("\n");
}
