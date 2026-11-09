/*
 * HandMessage3.c — WPA2 等 M3（MIC 校验）（PR-NORM-12）
 */
#include "Private.h"
#include "HalSerial.h"
#include "WifiCrypto/WifiEapol.h"

int IwlEapolHandWaitMessage3(UINT8 *Eapol, UINTN *EapolLengthInOut, UINT8 *Replay,
                             UINT8 KeyVersion, UINT8 *KeyConfirmationKey, UINT8 *Message2,
                             UINTN Message2Length) {
    UINT32 Index;
    int GotMessage3 = 0;
    UINTN EapolLength = *EapolLengthInOut;
    UINT32 EapolCount = 0;
    int SawKeyInfo = 0;
    int MessageIntegrityLogged = 0;
    UINT32 ReceiveMpdu = 0;
    UINT32 ReceiveData = 0;
    UINT32 ReceiveUnicast = 0;
    UINT32 ReceiveBeacon = 0;

    gIwlTransmitResponse = 0;
    gIwlReceiveCode = 0;

    for (Index = 0; Index < 4000 && !GotMessage3; Index++) {
        if (Index == 1000u || Index == 2000u) {
            (void)IwlSendEapol(Message2, Message2Length);
        }
        IwlReceivePoll();
        while (!GotMessage3 &&
               IwlReceiveDrainForEapol(Eapol, &EapolLength, 256u, &ReceiveMpdu,
                                       &ReceiveData, &ReceiveUnicast, &ReceiveBeacon)) {
            UINT8 Calculated[16];
            UINT8 Saved[16];
            UINTN ByteIndex;
            int Difference;
            UINT16 KeyInfo;

            EapolCount++;
            if (!SawKeyInfo) {
                char Line[28];
                char Hex[12];
                int Length = 0;
                const char *Prefix = "wpa2=ki d=";
                UINT16 KeyInfoValue = IwlBe16(Eapol + 5);
                while (*Prefix) {
                    Line[Length++] = *Prefix++;
                }
                HalSerialFormatHex(Hex, Eapol[4], 2);
                Line[Length++] = Hex[2];
                Line[Length++] = Hex[3];
                Line[Length++] = ' ';
                Line[Length++] = 'k';
                Line[Length++] = '=';
                HalSerialFormatHex(Hex, (KeyInfoValue >> 8) & 0xffu, 2);
                Line[Length++] = Hex[2];
                Line[Length++] = Hex[3];
                HalSerialFormatHex(Hex, KeyInfoValue & 0xffu, 2);
                Line[Length++] = Hex[2];
                Line[Length++] = Hex[3];
                Line[Length] = 0;
                IwlLogStage(Line);
                SawKeyInfo = 1;
            }
            if (Eapol[4] != 2 && Eapol[4] != 254) {
                continue;
            }
            KeyInfo = IwlBe16(Eapol + 5);
            /* M1 重传只有 ACK、没有 MIC。MIC 位置位就验，不再要求 ACK。 */
            if ((KeyInfo & 0x0100u) == 0) {
                continue;
            }
            IwlEapolCopyN(Saved, Eapol + 81, 16);
            WifiEapolMic(KeyVersion, KeyConfirmationKey, Eapol, EapolLength, Calculated);
            Difference = 0;
            for (ByteIndex = 0; ByteIndex < 16; ByteIndex++) {
                Difference |= (int)(Calculated[ByteIndex] ^ Saved[ByteIndex]);
            }
            if (Difference != 0) {
                if (!MessageIntegrityLogged) {
                    char Line[20];
                    char Hex[12];
                    int Length = 0;
                    const char *Prefix = "wpa2=mic k=";
                    while (*Prefix) {
                        Line[Length++] = *Prefix++;
                    }
                    HalSerialFormatHex(Hex, (KeyInfo >> 8) & 0xffu, 2);
                    Line[Length++] = Hex[2];
                    Line[Length++] = Hex[3];
                    HalSerialFormatHex(Hex, KeyInfo & 0xffu, 2);
                    Line[Length++] = Hex[2];
                    Line[Length++] = Hex[3];
                    Line[Length] = 0;
                    IwlLogStage(Line);
                    MessageIntegrityLogged = 1;
                }
                continue;
            }
            IwlEapolCopyN(Replay, Eapol + 9, 8);
            GotMessage3 = 1;
        }
        if (!GotMessage3) {
            IwlStallMilliseconds(1);
        }
    }
    if (!GotMessage3) {
        char Line[48];
        char Hex[12];
        int Length = 0;
        const char *Prefix = "wpa2=m3to e=";
        if (!gIwlTransmitResponse) {
            IwlLogApQ();
            IwlLogStage("txa=none");
        }
        while (*Prefix) {
            Line[Length++] = *Prefix++;
        }
        HalSerialFormatHex(Hex, EapolCount & 0xffu, 2);
        Line[Length++] = Hex[2];
        Line[Length++] = Hex[3];
        Line[Length++] = ' ';
        Line[Length++] = 'n';
        Line[Length++] = '=';
        HalSerialFormatHex(Hex, ReceiveMpdu & 0xffu, 2);
        Line[Length++] = Hex[2];
        Line[Length++] = Hex[3];
        Line[Length++] = ' ';
        Line[Length++] = 'd';
        Line[Length++] = '=';
        HalSerialFormatHex(Hex, ReceiveData & 0xffu, 2);
        Line[Length++] = Hex[2];
        Line[Length++] = Hex[3];
        Line[Length++] = ' ';
        Line[Length++] = 'u';
        Line[Length++] = '=';
        HalSerialFormatHex(Hex, ReceiveUnicast & 0xffu, 2);
        Line[Length++] = Hex[2];
        Line[Length++] = Hex[3];
        Line[Length++] = ' ';
        Line[Length++] = 'b';
        Line[Length++] = '=';
        HalSerialFormatHex(Hex, ReceiveBeacon & 0xffu, 2);
        Line[Length++] = Hex[2];
        Line[Length++] = Hex[3];
        Line[Length] = 0;
        IwlLogStage(Line);
        return 0;
    }

    *EapolLengthInOut = EapolLength;
    return 1;
}
