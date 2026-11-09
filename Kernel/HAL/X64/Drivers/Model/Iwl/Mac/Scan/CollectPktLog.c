/*
 * CollectPktLog.c — 扫描收包早期诊断日志（PR-NORM-12）
 */
#include "Private.h"
#include "HalSerial.h"

void IwlScanCollectLogEarlyPacket(IWL_RX_PKT *Packet, UINTN Length, int IsNotification) {
    const UINT8 *Raw = (const UINT8 *)&Packet->Hdr;
    UINT8 Code = Packet->Hdr.Code;
    UINT8 Flags = Packet->Hdr.Flags;

    if (gIwlScanCount <= 8) {
        char Line[32];
        char Hex[12];
        int LengthOut = 0;
        const char *Prefix = IsNotification ? "rxntf c=" : "rxack c=";
        while (*Prefix) {
            Line[LengthOut++] = *Prefix++;
        }
        HalSerialFormatHex(Hex, Code, 2);
        Line[LengthOut++] = Hex[2];
        Line[LengthOut++] = Hex[3];
        Line[LengthOut++] = ' ';
        Line[LengthOut++] = 'i';
        Line[LengthOut++] = '=';
        HalSerialFormatHex(Hex, Packet->Hdr.Idx, 2);
        Line[LengthOut++] = Hex[2];
        Line[LengthOut++] = Hex[3];
        Line[LengthOut] = 0;
        IwlLogVerb(Line);
    }
    if (gIwlScanCount <= 4) {
        char Line[56];
        char Hex[12];
        int LengthOut = 0;
        UINT32 Index;
        const char *Prefix = "rxraw L=";
        while (*Prefix) {
            Line[LengthOut++] = *Prefix++;
        }
        HalSerialFormatHex(Hex, (UINT32)Length, 4);
        Line[LengthOut++] = Hex[2];
        Line[LengthOut++] = Hex[3];
        Line[LengthOut++] = Hex[4];
        Line[LengthOut++] = Hex[5];
        Line[LengthOut++] = ' ';
        Line[LengthOut++] = 'f';
        Line[LengthOut++] = '=';
        HalSerialFormatHex(Hex, Flags, 2);
        Line[LengthOut++] = Hex[2];
        Line[LengthOut++] = Hex[3];
        Line[LengthOut++] = ' ';
        for (Index = 0; Index < 16 && LengthOut < 52; Index++) {
            HalSerialFormatHex(Hex, Raw[Index], 2);
            Line[LengthOut++] = Hex[2];
            Line[LengthOut++] = Hex[3];
        }
        Line[LengthOut] = 0;
        IwlLogVerb(Line);
    }
}

void IwlScanCollectLogScanAcknowledge(const UINT8 *Raw, UINTN Length) {
    UINT32 Status = 0;

    if (Length >= 4) {
        Status = (UINT32)Raw[Length - 4] | ((UINT32)Raw[Length - 3] << 8) |
                 ((UINT32)Raw[Length - 2] << 16) | ((UINT32)Raw[Length - 1] << 24);
    }
    if (Status != 0) {
        char Line[24];
        char Hex[12];
        int LengthOut = 0;
        const char *Prefix = "scan=st ";
        while (*Prefix) {
            Line[LengthOut++] = *Prefix++;
        }
        HalSerialFormatHex(Hex, Status, 8);
        Line[LengthOut++] = Hex[2];
        Line[LengthOut++] = Hex[3];
        Line[LengthOut++] = Hex[4];
        Line[LengthOut++] = Hex[5];
        Line[LengthOut++] = Hex[6];
        Line[LengthOut++] = Hex[7];
        Line[LengthOut++] = Hex[8];
        Line[LengthOut++] = Hex[9];
        Line[LengthOut] = 0;
        IwlLogStage(Line);
    } else {
        IwlLogStage("scan=ack");
    }
}

void IwlScanCollectLogScanError(const UINT8 *Payload, UINTN PayloadLength) {
    char Line[48];
    char Hex[12];
    int LengthOut = 0;
    const char *Prefix = "scerr e=";
    UINT32 ErrorType;

    (void)PayloadLength;
    ErrorType = (UINT32)Payload[0] | ((UINT32)Payload[1] << 8) |
                ((UINT32)Payload[2] << 16) | ((UINT32)Payload[3] << 24);
    while (*Prefix) {
        Line[LengthOut++] = *Prefix++;
    }
    HalSerialFormatHex(Hex, ErrorType, 8);
    Line[LengthOut++] = Hex[2];
    Line[LengthOut++] = Hex[3];
    Line[LengthOut++] = Hex[4];
    Line[LengthOut++] = Hex[5];
    Line[LengthOut++] = Hex[6];
    Line[LengthOut++] = Hex[7];
    Line[LengthOut++] = Hex[8];
    Line[LengthOut++] = Hex[9];
    Line[LengthOut++] = ' ';
    Line[LengthOut++] = 'o';
    Line[LengthOut++] = '=';
    HalSerialFormatHex(Hex, Payload[4], 2);
    Line[LengthOut++] = Hex[2];
    Line[LengthOut++] = Hex[3];
    Line[LengthOut] = 0;
    IwlLogStage(Line);
    IwlCmdqSnap();
}
