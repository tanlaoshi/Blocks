/*
 * AssocRequest.c — 组 AssocReq payload（PR-NORM-12）
 */
#include "Private.h"
#include "HalSerial.h"

/*
 * 写入 Pay；*PayloadLengthOut 为帧体长度。失败（无 RSN）返回 0。
 */
int IwlAssocBuildRequestPayload(UINT8 *Payload, UINTN PayloadCapacity,
                                UINTN *PayloadLengthOut, int Attempt) {
    UINTN Offset;

    if (PayloadCapacity < 6) {
        return 0;
    }
    Payload[0] = (UINT8)(gIwlTarget.Caps);
    Payload[1] = (UINT8)(gIwlTarget.Caps >> 8);
    Payload[2] = 0x0A;
    Payload[3] = 0x00;
    Payload[4] = 0;
    Payload[5] = gIwlTarget.SsidLen;
    IwlAssocCopyN(Payload + 6, gIwlTarget.Ssid, gIwlTarget.SsidLen);
    Offset = 6 + gIwlTarget.SsidLen;
    {
        UINT8 RatesLength = gIwlTarget.RatesLen;
        UINT8 ExtendedRatesLength = gIwlTarget.ExtRatesLen;
        static const UINT8 DefaultRates[] = {
            0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
        };
        static const UINT8 DefaultExtendedRates[] = {
            0x30, 0x48, 0x60, 0x6c
        };

        if (RatesLength == 0) {
            RatesLength = (UINT8)sizeof(DefaultRates);
            IwlAssocCopyN(Payload + Offset + 2, DefaultRates, RatesLength);
        } else {
            IwlAssocCopyN(Payload + Offset + 2, gIwlTarget.Rates, RatesLength);
        }
        Payload[Offset] = 1;
        Payload[Offset + 1] = RatesLength;
        Offset += 2 + RatesLength;

        if (ExtendedRatesLength == 0 && gIwlTarget.RatesLen == 0) {
            ExtendedRatesLength = (UINT8)sizeof(DefaultExtendedRates);
            IwlAssocCopyN(Payload + Offset + 2, DefaultExtendedRates,
                          ExtendedRatesLength);
            Payload[Offset] = 50;
            Payload[Offset + 1] = ExtendedRatesLength;
            Offset += 2 + ExtendedRatesLength;
        } else if (ExtendedRatesLength > 0) {
            IwlAssocCopyN(Payload + Offset + 2, gIwlTarget.ExtRates,
                          ExtendedRatesLength);
            Payload[Offset] = 50;
            Payload[Offset + 1] = ExtendedRatesLength;
            Offset += 2 + ExtendedRatesLength;
        }

        if (gIwlTarget.HtLen > 0 && Offset + 2 + gIwlTarget.HtLen <= PayloadCapacity) {
            if (Attempt == 0) {
                Payload[Offset] = 45;
                Payload[Offset + 1] = gIwlTarget.HtLen;
                IwlAssocCopyN(Payload + Offset + 2, gIwlTarget.Ht, gIwlTarget.HtLen);
                Offset += 2 + gIwlTarget.HtLen;
            }
        }
        if (gIwlTarget.HasRsn && gIwlPsk[0]) {
            UINTN RobustSecurityNetworkLength =
                IwlBuildStaRsn(Payload + Offset, PayloadCapacity - Offset);
            if (RobustSecurityNetworkLength == 0) {
                IwlLogStage("assoc=norsn");
                return 0;
            }
            Offset += RobustSecurityNetworkLength;
        }
    }
    *PayloadLengthOut = Offset;
    return 1;
}

void IwlAssocLogInformationElements(void) {
    char InformationElements[20];
    int Length = 0;
    const char *Prefix = "assoc=ie h=";

    while (*Prefix) {
        InformationElements[Length++] = *Prefix++;
    }
    InformationElements[Length++] = gIwlTarget.HtLen ? '1' : '0';
    InformationElements[Length++] = ' ';
    InformationElements[Length++] = 'r';
    InformationElements[Length++] = '=';
    InformationElements[Length++] = (gIwlTarget.RsnLen >= 4) ? '1' : '0';
    InformationElements[Length] = 0;
    IwlLogStage(InformationElements);
}
