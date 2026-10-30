/*
 * WifiSha256.h — 802.11 KDF-SHA-256：AES-CMAC 版 PTK（密钥描述符版本 3）用。
 *
 * 从哪读：WifiKdfSha256。
 * 别改：counter 从 1 起、label 后补 0、末尾 2 字节 bit 长度，都是 802.11 的字节契约。
 */
#ifndef WIFI_SHA256_H
#define WIFI_SHA256_H

#include "BootTypes.h"

#define WIFI_SHA256_DIGEST_LENGTH 32u /* FIPS 180-4 §1：256-bit 摘要 */

void WifiKdfSha256(const UINT8 *Key, UINTN KeyLength, const char *Label,
                   const UINT8 *Context, UINTN ContextLength,
                   UINT8 *Output, UINTN OutputLength);

#endif /* WIFI_SHA256_H */
