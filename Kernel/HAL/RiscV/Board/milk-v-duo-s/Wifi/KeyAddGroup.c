/*
 * WifiKeyAddGroup.c — GTK MM_KEY_ADD（从 WifiKeyAdd.c 拆出）
 *
 * mode0 = 厂商默认钥 sta=0xFF pairwise=0；mode6 = 旧实验盖 PTK 槽。
 */
#include "Wifi/KeyAddPrivate.h"

UINT32 gAicGtkProbeMode;
/* 0=厂商：装钥后才 ME_SET_CONTROL_PORT；1=旧早开对照（probe.cfg portearly=1） */
UINT32 gAicControlPortEarlyOpen;
/* 1=跳过 GTK MM_KEY_ADD（probe.cfg nogtk=1；证伪 GTK 是否挡单播 PTK） */
UINT32 gAicSkipGroupKeyInstall;
/* 1=跳过装钥后 ME_SET_CONTROL_PORT（probe.cfg noauthz=1） */
UINT32 gAicSkipAuthorizedAfterKeys;
/* 1=先 MM_KEY_ADD GTK 再 PTK（probe.cfg gtkfirst=1；Docs/21 §1a） */
UINT32 gAicGroupKeyBeforePairwise;
/* 1=跳过 PTK MM_KEY_ADD（probe.cfg noptk=1；Docs/21 §1c 组播选钥） */
UINT32 gAicSkipPairwiseKeyInstall;
/* 1=两钥装完后再 MM_KEY_ADD 一次 GTK（probe.cfg gtkreadd=1；Docs/21 §1d） */
UINT32 gAicGroupKeyAddAgainAfterBoth;

int AicKeyAddGroup(SDHCI_HOST *Host, UINT8 VifIndex, const WIFI_GTK *Gtk) {
    AIC_KEY_ADD_RESULT Result;
    UINT8 KeyId;
    UINT8 StaIndex;
    UINT8 Pairwise;
    UINT8 Cipher;
    UINT32 Length;

    if (Gtk->KeyLength == 0u || Gtk->KeyLength > AIC_KEY_SEC_KEY_LENGTH) {
        SdhciMark("WiFi: key add: gtk length ");
        SdhciMarkDecimal("", Gtk->KeyLength);
        SdhciMark(" out of range (1..32), the KDE did not carry a usable key\n");
        return -1;
    }
    KeyId = Gtk->KeyId;
    StaIndex = (UINT8)AIC_KEY_ADD_DEFAULT_STA_INDEX;
    Pairwise = (UINT8)AIC_KEY_ADD_NOT_PAIRWISE;
    Cipher = (UINT8)AIC_KEY_CIPHER_TKIP;
    Length = Gtk->KeyLength;
    switch (gAicGtkProbeMode) {
    case 1u:
        KeyId = 0u;
        break;
    case 2u:
        Pairwise = 1u;
        break;
    case 3u:
        Cipher = (UINT8)AIC_KEY_CIPHER_CCMP;
        break;
    case 4u:
        Length = 16u;
        break;
    case 5u:
        KeyId = 1u;
        break;
    case 6u:
        StaIndex = 0u;
        Pairwise = 1u;
        break;
    default:
        break;
    }
    if (Length > Gtk->KeyLength) {
        Length = Gtk->KeyLength;
    }
    SdhciMark("WiFi: gtk probe mode=");
    SdhciMarkDecimal("", gAicGtkProbeMode);
    SdhciMarkHex(" sta=", (UINT32)StaIndex, 2);
    SdhciMarkHex(" pairwise=", (UINT32)Pairwise, 2);
    SdhciMark("\n");
    Result =
        AicKeyAddExchange(Host, VifIndex, StaIndex, Pairwise, Gtk->Key, Length, KeyId, Cipher);
    SdhciMark("WiFi: key add: gtk sta=");
    SdhciMarkDecimal("", StaIndex);
    SdhciMark(" keyid=");
    SdhciMarkDecimal("", KeyId);
    SdhciMark(" cipher=");
    SdhciMarkDecimal("", Cipher);
    SdhciMark(" pairwise=");
    SdhciMarkDecimal("", Pairwise);
    AicKeyAddMarkResult(&Result);
    if (AicKeyAddIsInstalled(&Result) == 0) {
        SdhciMark("WiFi: handshake FAILED at GTK install: the firmware did not install the default"
                  " key (rc/cfm above; inst_nbr must be the join vif)\n");
        return -1;
    }
    return 0;
}
