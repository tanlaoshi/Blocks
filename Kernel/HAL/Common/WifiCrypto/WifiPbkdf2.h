/*
 * WifiPbkdf2.h — PBKDF2-HMAC-SHA1：WPA2-PSK 的 密码 + SSID → PMK。
 *
 * 从哪读：WifiPbkdf2Sha1。
 * 别改：4096 迭代是 WPA2-PSK 的规定值；改迭代数等于换协议。
 */
#ifndef WIFI_PBKDF2_H
#define WIFI_PBKDF2_H

#include "BootTypes.h"

/* IEEE 802.11-2020 §12.7.1.2：WPA2-PSK 固定 4096 次迭代（RFC 8018 §5.2 的 c） */
#define WIFI_PBKDF2_WPA2_ITERATIONS 4096u

int WifiPbkdf2Sha1(const char *Password, const UINT8 *Salt, UINTN SaltLength,
                   UINT32 Iterations, UINT8 *Output, UINTN OutputLength);

#endif /* WIFI_PBKDF2_H */
