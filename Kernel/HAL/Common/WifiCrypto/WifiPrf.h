/*
 * WifiPrf.h — WPA2 的 PTK 派生：PRF-384（版本 2）与 KDF-SHA-256（版本 3）。
 *
 * 从哪读：WifiPtkDerive（主）→ WifiPrf384。
 * 别改：PTK 内部布局 KCK|KEK|TK 以及 48/16 字节的切分是 802.11 的字节契约。
 */
#ifndef WIFI_PRF_H
#define WIFI_PRF_H

#include "BootTypes.h"

#define WIFI_PMK_LENGTH         32u /* WPA2-PSK 的 PMK：256-bit（IEEE 802.11-2020 §12.7.1.2） */
#define WIFI_PTK_LENGTH         48u /* CCMP 所需 PTK：KCK+KEK+TK（各 16B） */
#define WIFI_KCK_LENGTH         16u /* Key Confirmation Key（EAPOL MIC 用） */
#define WIFI_KEK_OFFSET         16u /* PTK 内 KEK 起点 */
#define WIFI_KEK_LENGTH         16u /* Key Encryption Key（GTK 解包用） */
#define WIFI_TK_OFFSET          32u /* PTK 内 TK 起点 */
#define WIFI_TK_LENGTH          16u /* CCMP 的 Temporal Key（16B） */
#define WIFI_MAC_ADDRESS_LENGTH 6u  /* MAC / BSSID 长度 */
#define WIFI_NONCE_LENGTH       32u /* ANonce / SNonce 长度 */
#define WIFI_PTK_CONTEXT_LENGTH 76u /* MinMAC|MaxMAC|MinNonce|MaxNonce = 6+6+32+32 */
/* IEEE 802.11-2020 §12.7.1.3：密钥描述符版本 3（AES-CMAC）走 KDF-SHA-256，其余走 PRF-384 */
#define WIFI_KEY_DESCRIPTOR_VERSION_AES_CMAC 3u
#define WIFI_PTK_LABEL "Pairwise key expansion" /* IEEE 802.11-2020 §12.7.1.3 */
#define WIFI_PTK_LABEL_LENGTH   22u /* 上面字符串去掉结尾 NUL */

void WifiPrf384(const UINT8 Pmk[WIFI_PMK_LENGTH], const UINT8 *Label,
                UINTN LabelLength, const UINT8 *Context, UINTN ContextLength,
                UINT8 Output[WIFI_PTK_LENGTH]);

void WifiPtkDerive(const UINT8 Pmk[WIFI_PMK_LENGTH],
                   const UINT8 StationMac[WIFI_MAC_ADDRESS_LENGTH],
                   const UINT8 AccessPointBssid[WIFI_MAC_ADDRESS_LENGTH],
                   const UINT8 Anonce[WIFI_NONCE_LENGTH],
                   const UINT8 Snonce[WIFI_NONCE_LENGTH],
                   UINT8 Ptk[WIFI_PTK_LENGTH], UINT8 KeyVersion);

#endif /* WIFI_PRF_H */
