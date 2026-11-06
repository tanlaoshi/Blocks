/*
 * AssocReport.c — `wififw join` 的目标解析/打印与 dry dump（只打印，不改状态）
 *
 * IND 解析见 AssocReportIndication.c。
 * 别改：输出文案就是验收判据（Archive/Duo-WiFi-关联与WPA2调研.md §8）；
 *       改文案等于改判据。这里不发消息、不碰 SDIO。
 */
#include "Wifi/AssocPrivate.h"
#include "CString.h"
#include "Compiler.h" /* BLOCKS_OFFSETOF：dry 要在真 buffer 里定位 ie_buf */

void AicJoinPrintTransmit(UINT32 Length) {
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

/* dry 用：前 64 B 足够看出 ssid/bssid/chan/flags 排歪没有 */
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
 * dry 专供：已拼好的 buffer 里 ie_buf（+64 起 22 B）按十六进制打出，并与源表对一次。
 * AicJoinDump 只打前 64 B，ie_buf 从 +64 才开始。
 */
void AicJoinDumpRsn(const UINT8 *Request, UINT32 RequestLength) {
    const UINT8 *Expected = AicJoinRsnIe();
    const UINT8 *InformationElement;
    UINT32 Offset = (UINT32)BLOCKS_OFFSETOF(AIC_SM_CONNECT, IeBuffer);
    UINT32 Index;
    int Same = 1;

    if (RequestLength < Offset + AIC_JOIN_RSN_IE_LENGTH) {
        SdhciMark("WiFi: join rsn: request too short, no ie_buf\n");
        return;
    }
    InformationElement = Request + Offset;
    SdhciMark("WiFi: join rsn[");
    SdhciMarkDecimal("", AIC_JOIN_RSN_IE_LENGTH);
    SdhciMark("]@");
    SdhciMarkDecimal("", Offset);
    SdhciMark(" (wpa2-psk ccmp id=");
    SdhciMarkDecimal("", (UINT32)InformationElement[0]);
    SdhciMark(")");
    for (Index = 0; Index < AIC_JOIN_RSN_IE_LENGTH; Index++) {
        SdhciMarkHex(" ", (UINT32)InformationElement[Index], 2);
        if (InformationElement[Index] != Expected[Index]) {
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
 * 从最近一次扫描的合并表里找目标。返回 0 = 找到，1 = 缓存空/没这个 SSID，
 * -1 = bssid 文本非法。不打印。
 */
int AicJoinResolve(const char *Ssid, const char *BssidText, AIC_SCAN_AP *Out) {
    UINT8 Bssid[AIC_SCAN_MAC_LENGTH];
    const UINT8 *Filter = 0;

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
