/*
 * WifiSha1.h — SHA-1 与 HMAC-SHA1：WPA2 四次握手的公共底座。
 *
 * 从哪读：WifiSha1Digest → WifiSha1Hmac。
 * 别改：摘要字节序是 FIPS 180-4 大端；改变它等于改变 PBKDF2/PRF/MIC 全部输出。
 */
#ifndef WIFI_SHA1_H
#define WIFI_SHA1_H

#include "BootTypes.h"

#define WIFI_SHA1_BLOCK_LENGTH  64u /* FIPS 180-4 §5.1.1：512-bit 分组 */
#define WIFI_SHA1_DIGEST_LENGTH 20u /* FIPS 180-4 §1：160-bit 摘要 */

void WifiSha1Digest(const UINT8 *Data, UINTN Length,
                    UINT8 Digest[WIFI_SHA1_DIGEST_LENGTH]);
void WifiSha1Hmac(const UINT8 *Key, UINTN KeyLength,
                  const UINT8 *Data, UINTN Length,
                  UINT8 Mac[WIFI_SHA1_DIGEST_LENGTH]);

#endif /* WIFI_SHA1_H */
