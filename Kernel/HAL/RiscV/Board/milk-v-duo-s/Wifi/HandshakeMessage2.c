/*
 * WifiHandshakeMessage2.c — 派生 PTK 并发送 EAPOL M2（从 WifiHandshake.c 拆出）
 */
#include "Wifi/HandshakePrivate.h"
#include "CString.h"

int AicHandshakeSendM2(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password,
                       UINT8 VifIndex, UINT8 ApIndex) {
    const AIC_HANDSHAKE_M1 *Message1 = AicHandshakeM1();
    UINT8 Pmk[WIFI_PMK_LENGTH];
    UINT8 Snonce[WIFI_NONCE_LENGTH];
    UINT8 Ptk[WIFI_PTK_LENGTH];
    UINT8 Mic[WIFI_EAPOL_MIC_LENGTH];
    UINT8 Frame[AIC_HANDSHAKE_EAPOL_MAX];
    UINT32 Length = 0;
    int Status;

    Status = AicHandshakeDerive(Target, Password, Message1, Pmk, Snonce, Ptk);
    if (Status == 0) {
        AicHandshakeRememberKck(Ptk, Message1->KeyVersion);
        AicHandshakeRememberPtk(Ptk);
        Length = AicHandshakeM2Build(Frame, Message1, Snonce, Ptk, Mic);
        SdhciMark("WiFi: eapol tx m2 len=");
        SdhciMarkDecimal("", Length);
        SdhciMark(" replay=");
        AicEapolMarkBytes("", Message1->Replay, AIC_EAPOL_REPLAY_LENGTH);
        SdhciMark(" mic=");
        AicEapolMarkBytes("", Mic, WIFI_EAPOL_MIC_LENGTH);
        SdhciMark("\n");
        Status = AicEapolSend(Host, Frame, Length, Target->Bssid, Message1->Station, VifIndex,
                              ApIndex);
    }
    if (Status != 0 && Length != 0u) {
        SdhciMark("WiFi: eapol tx m2 hex[");
        SdhciMarkDecimal("", Length);
        SdhciMark("]");
        AicEapolMarkBytes(" ", Frame, Length);
        SdhciMark("\n");
        SdhciMark("WiFi: handshake FAILED at M2 tx: data TX refused (see the tx line above)\n");
        SdhciMark("WiFi: next = compare txdesc bytes with vendor rwnx_tx.c:1497-1530 +"
                  " aicwf_sdio.c:1530-1550 (ethtype byte order, staid/vif_idx, hwq);"
                  " then check whether the firmware wants hostid != 0 for a tx confirm\n");
    }
    memset(Pmk, 0, sizeof(Pmk));
    memset(Ptk, 0, sizeof(Ptk));
    memset(Snonce, 0, sizeof(Snonce));
    memset(Frame, 0, sizeof(Frame));
    return Status;
}
