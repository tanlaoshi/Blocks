/*
 * WifiHandshakeInstall.c — 四次握手的收尾：装 PTK/GTK → 回 M4 → 看数据帧
 *
 * 用户眼里：`key add: ptk …`、`gtk: kd=… keyid=…`、`key add: gtk …`、
 *           `eapol tx m4 len=99 replay=…`，然后
 *           `eapol handshake ok (keys installed: ptk+gtk)`；
 *           再往下是 `data frame after keys: len=… decr=…`（数据通路活了的证据）。
 * 从哪读：AicHandshakeInstallKeys（顺序）→ HandshakeGtkExtract（M3 Key Data → GTK）
 *         → AicKeyAddPairwise / AicKeyAddGroup（WifiKeyAdd.c，MM_KEY_ADD_REQ）
 *         → HandshakeSendM4（组帧在 WifiHandshakeKey.c，发送走 WifiEapolTx.c）
 *         → AicHandshakeWatchData（装完钥匙后盯解密帧）。
 * 别改：GTK 的解析一律用公共模块 WifiGtkExtract（MP3 对拍过：KDE 解析 + RFC 3394
 *       + KeyID/长度），本文件不再写第二份；M4 的 99 B 与 Key Information 位见
 *       WifiHandshakePrivate.h。
 * 顺序：现场要求"先装钥匙、再回 M4"（判据行的顺序也是这个）。对照 wpa_supplicant 2.10
 *       wpa.c:1766-1783 是"先发 M4、再 install_ptk"；两者都对 AP 无害，但如果固件把
 *       装钥匙之后的数据帧（含 EAPOL）都加密，先装后发会让 AP 收不到 M4、一直重传 M3。
 *       真机若看到 `msg=M3` 反复出现、且 `data frame after keys` 不出现，把
 *       AIC_HANDSHAKE_M4_BEFORE_KEYS 改成 1 再刷一次即可（只换顺序，不动别处）。
 */
#include "WifiHandshakePrivate.h"
#include "CString.h"

/* 现场默认：装完 PTK/GTK 再回 M4（与判据行的顺序一致）；1 = 先回 M4 再装（见文件头） */
/*
 * 真机判据把它翻成 1：装密钥后固件会把后续发出的帧加密，M4 若在装钥匙之后发就是密文，
 * AP 读不到 → 端口永远不开 → DHCP 的 DISCOVER 被丢（真机现象：data tx 正常、无 OFFER）。
 * 厂商 wpa_supplicant 的顺序是"先发 M4、再 install_ptk"（wpa.c:1766-1783）。
 */
#define AIC_HANDSHAKE_M4_BEFORE_KEYS 1u

/* WifiGtkExtract 的返回码 → 串口上能读的一句话（数值仍照打，便于对拍） */
static const char *HandshakeGtkStatusText(int Status) {
    if (Status == WIFI_GTK_STATUS_SHORT) {
        return "frame shorter than 101 B";
    }
    if (Status == WIFI_GTK_STATUS_RANGE) {
        return "Key Data Length out of range";
    }
    if (Status == WIFI_GTK_STATUS_UNWRAP) {
        return "RFC 3394 unwrap failed: the KEK does not fit (PTK/KEK mismatch)";
    }
    if (Status == WIFI_GTK_STATUS_KEY_SHORT) {
        return "GTK KDE present but its key field is shorter than 16 B";
    }
    if (Status == WIFI_GTK_STATUS_ABSENT) {
        return "no GTK KDE (DD 00-0F-AC-01) in the M3 key data";
    }
    return "ok";
}

/*
 * GTK 从 M3 的 Key Data 里解出来：
 *   EAPOL +99 起是 Key Data（长度在 +97，大端），WPA2 的 Key Data 是 KEK 用
 *   RFC 3394 包过的元素序列；里面那条 KDE 是
 *   `DD | Len | 00-0F-AC-01 | KeyID|Rsvd | GTK`（802.11-2016 §12.7.2 的 Key Data 元素，
 *   公共模块 WifiEapol.c 的 WIFI_GTK_KDE_* 宏就是它的逐字节解释）。
 * 两个参数都交给公共模块：KEK = PTK[16..31]，GroupCipher 用 **RSN 编号** 0x02(TKIP)，
 * 它决定"32 B 的组钥匙要不要再切出一把 16 B 的备选钥匙"。
 */
static int HandshakeGtkExtract(const AIC_HANDSHAKE_M3 *M3, const UINT8 *Ptk, WIFI_GTK *Gtk) {
    int Status;

    if ((M3->KeyInformation & AIC_EAPOL_KEY_INFORMATION_ENCRYPTED) == 0u) {
        SdhciMark("WiFi: gtk: M3 key data is not encrypted (key_info encrypted=0):"
                  " no GTK can be unwrapped\n");
        return -1;
    }
    Status = WifiGtkExtract(Ptk + WIFI_KEK_OFFSET, M3->Frame, M3->Length,
                            (UINT8)AIC_HANDSHAKE_GROUP_CIPHER_TKIP, Gtk);
    SdhciMark("WiFi: gtk: status=");
    SdhciMarkDecimal("", (UINT32)Status);
    if (Status != WIFI_GTK_STATUS_OK) {
        SdhciMark(" kd=");
        SdhciMarkDecimal("", Gtk->KeyDataLength);
        SdhciMark(" (");
        SdhciMark(HandshakeGtkStatusText(Status));
        SdhciMark(")\n");
        return -1;
    }
    SdhciMark(" kd=");
    SdhciMarkDecimal("", Gtk->KeyDataLength);
    SdhciMark(" ln=");
    SdhciMarkDecimal("", Gtk->ElementLength);
    SdhciMark(" keyid=");
    SdhciMarkDecimal("", Gtk->KeyId);
    SdhciMark(" kb=");
    SdhciMarkDecimal("", Gtk->KeyLength);
    SdhciMark(" (DD KDE from M3 key data, unwrapped with the KEK)\n");
    return 0;
}

/*
 * 回 M4：99 B、没有 Key Data（KeyDataLen=0），replay/版本/描述符都抄 M3，
 * MIC 用 KCK 算，发送走数据 TX（与 M2 同一条 txdesc 路径）。
 */
static int HandshakeSendM4(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, UINT8 VifIndex,
                           UINT8 ApIndex) {
    const AIC_HANDSHAKE_M3 *M3 = AicHandshakeM3();
    UINT8 Frame[AIC_HANDSHAKE_EAPOL_MAX];
    UINT8 Mic[WIFI_EAPOL_MIC_LENGTH];
    UINT32 Length = AicHandshakeM4Build(Frame, M3, AicHandshakeKck(), Mic);
    int Status;

    SdhciMark("WiFi: eapol tx m4 len=");
    SdhciMarkDecimal("", Length);
    SdhciMark(" replay=");
    AicEapolMarkBytes("", M3->Replay, AIC_EAPOL_REPLAY_LENGTH);
    SdhciMark(" mic=");
    AicEapolMarkBytes("", Mic, WIFI_EAPOL_MIC_LENGTH);
    SdhciMark("\n");
    Status = AicEapolSend(Host, Frame, Length, Target->Bssid, AicHandshakeStation(), VifIndex,
                          ApIndex);
    memset(Frame, 0, sizeof(Frame));
    memset(Mic, 0, sizeof(Mic));
    if (Status != 0) {
        SdhciMark("WiFi: handshake FAILED at M4 tx: the data TX refused it (see eapol tx above)\n");
        return -1;
    }
    return 0;
}

/*
 * 收尾三步。任何一步失败都只打一行"卡在哪"，不往下假装成功：
 *   1) 装 PTK（sta_idx = ap_idx）—— 之后固件才解得开单播加密帧；
 *   2) 从 M3 解 GTK 并装（sta_idx = 0xFF，key_idx = KeyID，cipher = TKIP）；
 *   3) 回 M4。
 * GTK 解不出来时仍然把 M4 发出去（让四次握手在 AP 那边收尾），最后再报失败 ——
 * 否则 AP 会以为我们没收到 M3、一直重传。
 */
int AicHandshakeInstallKeys(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, UINT8 VifIndex,
                            UINT8 ApIndex) {
    const UINT8 *Ptk = AicHandshakePtk();
    const AIC_HANDSHAKE_M3 *M3 = AicHandshakeM3();
    WIFI_GTK Gtk;
    int GtkReady;
    int M4Sent = 0;

    if (AicKeyAddLayoutCheck() != 0) {
        return -1;
    }
    if (AIC_HANDSHAKE_M4_BEFORE_KEYS != 0u) {
        if (HandshakeSendM4(Host, Target, VifIndex, ApIndex) != 0) {
            return -1;
        }
        M4Sent = 1;
    }
    if (AicKeyAddPairwise(Host, VifIndex, ApIndex, Ptk + WIFI_TK_OFFSET) != 0) {
        return -1;
    }
    /* PTK 已经在固件里：从这里起，固件能把发给我们的加密帧解开（decr != 0） */
    gAicEapolKeysInstalled = 1u;
    memset(&Gtk, 0, sizeof(Gtk));
    GtkReady = (HandshakeGtkExtract(M3, Ptk, &Gtk) == 0) ? 1 : 0;
    if (GtkReady != 0 && AicKeyAddGroup(Host, VifIndex, &Gtk) != 0) {
        return -1;
    }
    memset(&Gtk, 0, sizeof(Gtk));
    if (M4Sent == 0 && HandshakeSendM4(Host, Target, VifIndex, ApIndex) != 0) {
        return -1;
    }
    if (GtkReady == 0) {
        SdhciMark("WiFi: handshake: M4 is out, but the GTK is missing: group/broadcast frames"
                  " stay undecryptable (see the gtk: line above)\n");
        return -1;
    }
    /*
     * 厂商在装完钥匙后**再发一次** ME_SET_CONTROL_PORT（rwnx_main.c:4606-4610 的 AUTHORIZED 分支）。
     * 之前我们只在握手前开过；真机现象是 DHCP DISCOVER 发得出去却永远没有 OFFER，
     * 与"受控口未随授权状态重新打开、数据帧被固件拦下"相符。这一发失败不判死。
     */
    if (AicControlPortSend(Host, ApIndex) == 0) {
        SdhciMark("WiFi: control port reopened after keys (vendor order: add_key -> set_control_port)\n");
    }
    return 0;
}

/*
 * 更强的判据：装完钥匙再盯 3 s。AP 的 ARP/DHCP 广播、组播帧一旦被固件解开
 * （hw_rxhdr.status 的 decr != 0），WifiEapolParse.c 会逐帧打
 * `data frame after keys: len=… decr=…`；这里只负责收工时的汇总。
 */
void AicHandshakeWatchData(SDHCI_HOST *Host) {
    AIC_EAPOL_COUNTERS Counters;

    memset(&Counters, 0, sizeof(Counters));
    SdhciMark("WiFi: keys installed: watching data frames (budget=");
    SdhciMarkDecimal("", AIC_HANDSHAKE_DATA_MS);
    SdhciMark("ms)\n");
    if (AicEapolPollRun(Host, AIC_HANDSHAKE_DATA_MS, &Counters, 0) != 0) {
        SdhciMark("WiFi: data watch: SDIO read failed (FIFO may be out of sync)\n");
        return;
    }
    SdhciMark("WiFi: data after keys: frames=");
    SdhciMarkDecimal("", Counters.Frames);
    SdhciMark(" data=");
    SdhciMarkDecimal("", Counters.DataFrames);
    SdhciMark(" decrypted=");
    SdhciMarkDecimal("", Counters.EncryptedFrames);
    SdhciMark("\n");
    if (Counters.EncryptedFrames == 0u) {
        SdhciMark("WiFi: no decrypted data frame in ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_DATA_MS);
        SdhciMark("ms (keys are in the firmware, but the AP sent nothing to decrypt yet;"
                  " a DHCP/ARP broadcast is what usually shows up first)\n");
        return;
    }
    SdhciMark("WiFi: data path is alive (decr != 0 means the firmware decrypted with the key"
              " it got from us) -> next step: DHCP\n");
}
