/*
 * WifiEapol.h — EAPOL-Key MIC、AES-128-CMAC 与 GTK 解密（AES key unwrap，RFC 3394）。
 *
 * 从哪读：WifiEapolMic（MIC）→ WifiGtkExtract（帧 + KEK → 组密钥）。
 *         CMAC 底座 WifiAesCmac 也在这里（只被 MIC 用）。
 * 别改：MIC 偏移 81、Key Data 偏移 99、DD 组密钥 KDE 的 OUI 都是 802.11/802.1X
 *        的空口字节契约；偏移错一位，真机就只表现成「连不上」。
 */
#ifndef WIFI_EAPOL_H
#define WIFI_EAPOL_H

#include "BootTypes.h"
#include "WifiCrypto/WifiAes.h"

#define WIFI_EAPOL_MIC_LENGTH             16u /* IEEE 802.1X-2020 §11.9：Key MIC 128-bit */
#define WIFI_EAPOL_KCK_LENGTH             16u /* PTK 前 16B：KCK */
#define WIFI_EAPOL_KEK_LENGTH             16u /* PTK 的 KEK：解 GTK 用 */
#define WIFI_EAPOL_MIC_OFFSET             81u /* EAPOL 帧内 Key MIC 起点 */
#define WIFI_EAPOL_KEY_DATA_LENGTH_OFFSET 97u /* Key Data Length 字段起点 */
#define WIFI_EAPOL_KEY_DATA_OFFSET        99u /* Key Data 起点 */
#define WIFI_EAPOL_KEY_MIN_LENGTH         101u /* 原实现下限：99 + 至少 2B Key Data */
#define WIFI_GTK_MAX_LENGTH               32u /* TKIP 的 GTK 可到 32B */
#define WIFI_GTK_ALTERNATE_LENGTH         16u /* CCMP 备选密钥 16B */

typedef struct {
    UINT16 KeyDataLength; /* EAPOL-Key 帧里的 Key Data Length（串口 gtk=kd 用） */
    UINT8  ElementLength; /* 命中的 GTK KDE 长度字段（串口 gtk=ln 用） */
    UINT8  KeyId;
    UINT8  KeyLength;
    UINT8  Key[WIFI_GTK_MAX_LENGTH];
    UINT8  Alternate[WIFI_GTK_ALTERNATE_LENGTH];
    UINT8  AlternateValid;
} WIFI_GTK;

/* WifiGtkExtract 的返回码；驱动据此决定串口文案（gtk=no/bad/ok） */
#define WIFI_GTK_STATUS_OK     0 /* 解出可用 GTK */
#define WIFI_GTK_STATUS_SHORT  1 /* 帧太短，读不到 Key Data Length */
#define WIFI_GTK_STATUS_RANGE  2 /* Key Data Length 越界 */
#define WIFI_GTK_STATUS_UNWRAP 3 /* RFC 3394 校验失败（KEK 不对/密文坏） */
#define WIFI_GTK_STATUS_KEY_SHORT 4 /* 命中 DD KDE，但密钥域不足 16B */
#define WIFI_GTK_STATUS_ABSENT 5 /* 解包成功，但帧里没有 GTK KDE */

void WifiEapolMic(UINT8 KeyVersion, const UINT8 Kck[WIFI_EAPOL_KCK_LENGTH],
                  UINT8 *Eapol, UINTN EapolLength,
                  UINT8 Mic[WIFI_EAPOL_MIC_LENGTH]);

void WifiAesCmac(const UINT8 Key[WIFI_AES_KEY_LENGTH], const UINT8 *Message,
                 UINTN Length, UINT8 Mac[WIFI_AES_BLOCK_LENGTH]);

int WifiGtkUnwrap(const UINT8 Kek[WIFI_EAPOL_KEK_LENGTH], const UINT8 *Wrapped,
                  UINTN WrappedLength, UINT8 *Plain, UINTN PlainCapacity,
                  UINTN *PlainLength);

int WifiGtkExtract(const UINT8 Kek[WIFI_EAPOL_KEK_LENGTH], const UINT8 *Eapol,
                   UINTN EapolLength, UINT8 GroupCipher, WIFI_GTK *Gtk);

#endif /* WIFI_EAPOL_H */
