/*
 * HandMessage2.c — WPA2 M2：派生 PMK/PTK + 发 M2（PR-NORM-12）
 */
#include "Private.h"
#include "HalSerial.h"
#include "WifiCrypto/WifiEapol.h"
#include "WifiCrypto/WifiPbkdf2.h"
#include "WifiCrypto/WifiPrf.h"

int IwlEapolHandSendMessage2(UINT8 *Eapol, UINT8 *Anonce, UINT8 *Replay, UINT8 KeyDesc,
                             UINT8 *KeyVersionOut, UINT8 *EapolVersionOut,
                             UINT8 *KeyConfirmationKey, UINT8 *PairwiseTransientKey,
                             UINT8 *Message2Out, UINTN *Message2LengthOut) {
    UINT8 PairwiseMasterKey[32];
    UINT8 PairwiseTransientKeyLocal[48];
    UINT8 SupplicantNonce[32];
    UINT32 Index;
    UINTN ServiceSetIdentifierLength;
    UINT16 KeyInfo;
    UINT8 KeyVersion = 2;
    UINT8 EapolVersion = 1;

    IwlEapolZero(PairwiseMasterKey, 32);
    IwlEapolZero(PairwiseTransientKeyLocal, 48);
    IwlEapolZero(SupplicantNonce, 32);

    {
        char Line[20];
        char Hex[12];
        int Length = 0;
        const char *Prefix = "wpa2=m1 k=";
        KeyInfo = IwlBe16(Eapol + 5);
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
        KeyVersion = (UINT8)(KeyInfo & 7u);
        if (KeyVersion != 3) {
            KeyVersion = 2;
        }
        EapolVersion = Eapol[0];
        if (EapolVersion < 1 || EapolVersion > 2) {
            EapolVersion = 1;
        }
    }

    if (gIwlPmkOk) {
        IwlEapolCopyN(PairwiseMasterKey, gIwlPmk, 32);
    } else {
        ServiceSetIdentifierLength = 0;
        while (gIwlSsid[ServiceSetIdentifierLength]) {
            ServiceSetIdentifierLength++;
        }
        if (!WifiPbkdf2Sha1(gIwlPsk, (const UINT8 *)gIwlSsid, ServiceSetIdentifierLength,
                            WIFI_PBKDF2_WPA2_ITERATIONS, PairwiseMasterKey,
                            WIFI_PMK_LENGTH)) {
            IwlLogStage("wpa2=pmk");
            return 0;
        }
    }

    for (Index = 0; Index < 32; Index++) {
        SupplicantNonce[Index] =
            (UINT8)(gIwlMac[Index % 6] ^ (UINT8)(Index * 17u + 3u));
    }
    WifiPtkDerive(PairwiseMasterKey, gIwlMac, gIwlBssid, Anonce, SupplicantNonce,
                  PairwiseTransientKeyLocal, KeyVersion);
    IwlEapolCopyN(KeyConfirmationKey, PairwiseTransientKeyLocal, 16);
    IwlEapolCopyN(gIwlPtk, PairwiseTransientKeyLocal + 32, 16);
    IwlEapolCopyN(PairwiseTransientKey, PairwiseTransientKeyLocal, 48);

    /*
     * 刀 #146：M2 曾塞 beacon RSN。刀 #173 后 AssocReq 已是自建 PSK RSN；
     * 刀 #174：M2 Key Data 必须同一份（gIwlStaRsn），否则 Apple 空口 ACK 但不回 M3。
     * 刀 #147：KeyInfo 版本跟 M1（2=HMAC-SHA1，3=AES-CMAC）。
     */
    IwlEapolZero(Eapol, 256u);
    Eapol[0] = EapolVersion;
    Eapol[1] = 3;
    Eapol[4] = KeyDesc;
    IwlPutBe16(Eapol + 5, (UINT16)(0x0108u | KeyVersion));
    IwlPutBe16(Eapol + 7, 16);
    IwlEapolCopyN(Eapol + 9, Replay, 8);
    IwlEapolCopyN(Eapol + 17, SupplicantNonce, 32);
    {
        UINTN Total = 99;
        UINT8 KeyDataLength = 0;
        int UsedStation = 0;

        if (gIwlStaRsnLen >= 4 && gIwlStaRsnLen <= 32 &&
            99u + (UINTN)gIwlStaRsnLen <= 256u) {
            KeyDataLength = gIwlStaRsnLen;
            IwlEapolCopyN(Eapol + 99, gIwlStaRsn, KeyDataLength);
            UsedStation = 1;
        } else if (gIwlTarget.RsnLen >= 4 && gIwlTarget.RsnLen <= 48 &&
                   99u + (UINTN)gIwlTarget.RsnLen <= 256u) {
            KeyDataLength = gIwlTarget.RsnLen;
            IwlEapolCopyN(Eapol + 99, gIwlTarget.Rsn, KeyDataLength);
        }
        if (KeyDataLength) {
            IwlPutBe16(Eapol + 97, KeyDataLength);
            IwlPutBe16(Eapol + 2, (UINT16)(95u + KeyDataLength));
            Total = 99u + KeyDataLength;
        } else {
            IwlPutBe16(Eapol + 97, 0);
            IwlPutBe16(Eapol + 2, 95);
        }
        {
            UINT8 MessageIntegrityCode[16];
            WifiEapolMic(KeyVersion, KeyConfirmationKey, Eapol, Total,
                         MessageIntegrityCode);
            (void)MessageIntegrityCode;
        }
        if (IwlSendEapol(Eapol, Total) != 0) {
            IwlLogStage("wpa2=m2tx");
            return 0;
        }
        IwlEapolCopyN(Message2Out, Eapol, Total);
        *Message2LengthOut = Total;
        if (KeyVersion == 3 && KeyDataLength) {
            IwlLogStage("wpa2=m2c");
        } else if (UsedStation) {
            IwlLogStage("wpa2=m2s");
        } else {
            IwlLogStage(KeyDataLength ? "wpa2=m2r" : "wpa2=m2");
        }
    }

    *KeyVersionOut = KeyVersion;
    *EapolVersionOut = EapolVersion;
    return 1;
}
