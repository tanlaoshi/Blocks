/*
 * HandMessage4.c — WPA2 M3 后装 GTK + 发 M4（PR-NORM-12）
 */
#include "Private.h"
#include "WifiCrypto/WifiEapol.h"

int IwlEapolHandSendMessage4(UINT8 *Eapol, UINTN Message3Length, UINT8 *Replay,
                             UINT8 KeyDesc, UINT8 KeyVersion, UINT8 EapolVersion,
                             UINT8 *KeyConfirmationKey, UINT8 *PairwiseTransientKey) {
    IwlLogStage("wpa2=m3");
    IwlInstallGtk(Eapol, Message3Length, PairwiseTransientKey + 16);

    IwlEapolZero(Eapol, 256u);
    Eapol[0] = EapolVersion;
    Eapol[1] = 3;
    IwlPutBe16(Eapol + 2, 95);
    Eapol[4] = KeyDesc;
    IwlPutBe16(Eapol + 5, (UINT16)(0x0308u | KeyVersion));
    IwlPutBe16(Eapol + 7, 16);
    IwlEapolCopyN(Eapol + 9, Replay, 8);
    IwlPutBe16(Eapol + 97, 0);
    {
        UINT8 MessageIntegrityCode[16];
        WifiEapolMic(KeyVersion, KeyConfirmationKey, Eapol, 99, MessageIntegrityCode);
        (void)MessageIntegrityCode;
    }
    if (IwlSendEapol(Eapol, 99) != 0) {
        IwlLogStage("wpa2=m4tx");
        return 0;
    }

    gIwlWpa2Ok = 1;
    IwlLogStage("wpa2=ok");
    /* 刀 #180：握手完再装固件钥；失败软退，主机 CCMP 仍可试 */
    if (!IwlStaKeysInstall()) {
        IwlLogStage("key=soft");
    }
    return 1;
}
