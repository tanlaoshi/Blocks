/*
 * Hand.c — WPA2-PSK 四次握手编排（PR-NORM-12：M2/M3/M4 分文件）
 */
#include "Private.h"

int IwlEapolFinishHandshake(UINT8 *Eapol, UINTN EapLenIn, UINT8 *Anonce, UINT8 *Replay,
                            UINT8 KeyDesc) {
    UINT8 PairwiseTransientKey[48];
    UINT8 KeyConfirmationKey[16];
    UINT8 Message2[160];
    UINTN Message2Length = 0;
    UINT8 KeyVersion = 2;
    UINT8 EapolVersion = 1;
    UINTN EapolLength = EapLenIn;

    if (!IwlEapolHandSendMessage2(Eapol, Anonce, Replay, KeyDesc, &KeyVersion,
                                  &EapolVersion, KeyConfirmationKey, PairwiseTransientKey,
                                  Message2, &Message2Length)) {
        return 0;
    }
    if (!IwlEapolHandWaitMessage3(Eapol, &EapolLength, Replay, KeyVersion,
                                  KeyConfirmationKey, Message2, Message2Length)) {
        return 0;
    }
    return IwlEapolHandSendMessage4(Eapol, EapolLength, Replay, KeyDesc, KeyVersion,
                                    EapolVersion, KeyConfirmationKey,
                                    PairwiseTransientKey);
}
