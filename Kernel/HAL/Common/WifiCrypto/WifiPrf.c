/*
 * WifiPrf.c — PTK 派生（自 Intel 驱动的 Mac/Eapol/Crypto.c 原样搬出）。
 *
 * 从哪读：WifiPtkDerive（把 MAC/BSSID/两个 nonce 显式传入）→ WifiPrf384。
 * 别改：Min/Max 排序与 A||0||B||counter 的拼法是 802.11 的字节契约。
 */
#include "WifiCrypto/WifiPrf.h"
#include "WifiCrypto/WifiSha1.h"
#include "WifiCrypto/WifiSha256.h"

#define WIFI_PRF_CONTEXT_MAX_LENGTH 128u /* 本实现上限：label ≤ 22 + 0 + context 76 */
#define WIFI_PRF_INPUT_MAX_LENGTH   160u /* ≥ context 上限 + counter 1，留余量 */

static void CopyBytes(UINT8 *Destination, const UINT8 *Source, UINTN Length)
{
    UINTN I;
    for (I = 0; I < Length; I++) {
        Destination[I] = Source[I];
    }
}

/* 与驱动里的 MemCmp 同语义：只在 PTK 的 Min/Max 排序里判大小 */
static int CompareBytes(const UINT8 *Left, const UINT8 *Right, UINTN Length)
{
    UINTN I;
    for (I = 0; I < Length; I++) {
        if (Left[I] != Right[I]) {
            return (int)Left[I] - (int)Right[I];
        }
    }
    return 0;
}

void WifiPrf384(const UINT8 Pmk[WIFI_PMK_LENGTH], const UINT8 *Label,
                UINTN LabelLength, const UINT8 *Context, UINTN ContextLength,
                UINT8 Output[WIFI_PTK_LENGTH])
{
    UINT8 Buffer[WIFI_PRF_CONTEXT_MAX_LENGTH];
    UINT8 Temporary[WIFI_SHA1_DIGEST_LENGTH];
    UINTN Position = 0;
    UINT8 Count = 0;
    UINTN OutputPosition = 0;
    UINTN I;

    for (I = 0; I < LabelLength && Position < sizeof(Buffer); I++) {
        Buffer[Position++] = Label[I];
    }
    if (Position < sizeof(Buffer)) {
        Buffer[Position++] = 0;
    }
    for (I = 0; I < ContextLength && Position < sizeof(Buffer); I++) {
        Buffer[Position++] = Context[I];
    }
    while (OutputPosition < WIFI_PTK_LENGTH) {
        UINT8 Input[WIFI_PRF_INPUT_MAX_LENGTH];
        UINTN InputLength = 0;
        for (I = 0; I < Position; I++) {
            Input[InputLength++] = Buffer[I];
        }
        Input[InputLength++] = Count;
        WifiSha1Hmac(Pmk, WIFI_PMK_LENGTH, Input, InputLength, Temporary);
        for (I = 0; I < WIFI_SHA1_DIGEST_LENGTH && OutputPosition < WIFI_PTK_LENGTH; I++) {
            Output[OutputPosition++] = Temporary[I];
        }
        Count++;
    }
}

void WifiPtkDerive(const UINT8 Pmk[WIFI_PMK_LENGTH],
                   const UINT8 StationMac[WIFI_MAC_ADDRESS_LENGTH],
                   const UINT8 AccessPointBssid[WIFI_MAC_ADDRESS_LENGTH],
                   const UINT8 Anonce[WIFI_NONCE_LENGTH],
                   const UINT8 Snonce[WIFI_NONCE_LENGTH],
                   UINT8 Ptk[WIFI_PTK_LENGTH], UINT8 KeyVersion)
{
    UINT8 Label[WIFI_PTK_LABEL_LENGTH];
    UINT8 Context[WIFI_PTK_CONTEXT_LENGTH];
    UINTN I;
    const UINT8 *MinMac;
    const UINT8 *MaxMac;
    const UINT8 *MinNonce;
    const UINT8 *MaxNonce;

    for (I = 0; I < WIFI_PTK_LABEL_LENGTH; I++) {
        Label[I] = (UINT8)WIFI_PTK_LABEL[I];
    }
    if (CompareBytes(StationMac, AccessPointBssid, WIFI_MAC_ADDRESS_LENGTH) < 0) {
        MinMac = StationMac;
        MaxMac = AccessPointBssid;
    } else {
        MinMac = AccessPointBssid;
        MaxMac = StationMac;
    }
    if (CompareBytes(Anonce, Snonce, WIFI_NONCE_LENGTH) < 0) {
        MinNonce = Anonce;
        MaxNonce = Snonce;
    } else {
        MinNonce = Snonce;
        MaxNonce = Anonce;
    }
    CopyBytes(Context, MinMac, WIFI_MAC_ADDRESS_LENGTH);
    CopyBytes(Context + WIFI_MAC_ADDRESS_LENGTH, MaxMac, WIFI_MAC_ADDRESS_LENGTH);
    CopyBytes(Context + 2u * WIFI_MAC_ADDRESS_LENGTH, MinNonce, WIFI_NONCE_LENGTH);
    CopyBytes(Context + 2u * WIFI_MAC_ADDRESS_LENGTH + WIFI_NONCE_LENGTH,
              MaxNonce, WIFI_NONCE_LENGTH);
    if (KeyVersion == WIFI_KEY_DESCRIPTOR_VERSION_AES_CMAC) {
        WifiKdfSha256(Pmk, WIFI_PMK_LENGTH, WIFI_PTK_LABEL, Context,
                      WIFI_PTK_CONTEXT_LENGTH, Ptk, WIFI_PTK_LENGTH);
    } else {
        WifiPrf384(Pmk, Label, WIFI_PTK_LABEL_LENGTH, Context,
                   WIFI_PTK_CONTEXT_LENGTH, Ptk);
    }
}
