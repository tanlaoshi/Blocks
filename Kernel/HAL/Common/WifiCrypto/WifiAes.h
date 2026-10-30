/*
 * WifiAes.h — AES-128 分组加解密（WPA2 的 CMAC / GTK 解包底座）。
 *
 * 从哪读：WifiAesEncrypt / WifiAesDecrypt。
 * 别改：块序与字节序都是 FIPS 197 的字节契约。
 */
#ifndef WIFI_AES_H
#define WIFI_AES_H

#include "BootTypes.h"

#define WIFI_AES_BLOCK_LENGTH     16u  /* FIPS 197 §3：128-bit 分组 */
#define WIFI_AES_KEY_LENGTH       16u  /* AES-128 */
#define WIFI_AES_ROUND_KEY_LENGTH 176u /* (10 + 1) 轮 × 16B */

int WifiAesEncrypt(const UINT8 Key[WIFI_AES_KEY_LENGTH],
                   const UINT8 Input[WIFI_AES_BLOCK_LENGTH],
                   UINT8 Output[WIFI_AES_BLOCK_LENGTH]);
int WifiAesDecrypt(const UINT8 Key[WIFI_AES_KEY_LENGTH],
                   const UINT8 Input[WIFI_AES_BLOCK_LENGTH],
                   UINT8 Output[WIFI_AES_BLOCK_LENGTH]);

#endif /* WIFI_AES_H */
