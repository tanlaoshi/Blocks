/*
 * WifiPbkdf2.c — PBKDF2-HMAC-SHA1（自 Intel 驱动原样搬出，无 libc、无硬件依赖）。
 *
 * 从哪读：WifiPbkdf2Sha1 → Pbkdf2Block。
 * 别改：U1 = HMAC(P, S || INT(i))、U_{n+1} = HMAC(P, U_n)、T = U1 ^ … ^ Uc。
 */
#include "WifiCrypto/WifiPbkdf2.h"
#include "WifiCrypto/WifiSha1.h"

#define WIFI_PBKDF2_BLOCK_INDEX_LENGTH 4u  /* RFC 8018 §5.2：INT(i) 32-bit 大端 */
#define WIFI_PBKDF2_SALT_MAX_LENGTH    64u /* 本实现上限；WPA2 SSID ≤ 32B，留余量 */

static void MemXor(UINT8 *Destination, const UINT8 *Source, UINTN Length)
{
    UINTN I;
    for (I = 0; I < Length; I++) {
        Destination[I] ^= Source[I];
    }
}

static void MemCopy(UINT8 *Destination, const UINT8 *Source, UINTN Length)
{
    UINTN I;
    for (I = 0; I < Length; I++) {
        Destination[I] = Source[I];
    }
}

static UINTN PasswordLength(const char *Password)
{
    UINTN Length = 0;
    if (Password == NULL) {
        return 0;
    }
    while (Password[Length] != 0) {
        Length++;
    }
    return Length;
}

/*
 * F(P, S, c, i) = U1 ^ U2 ^ ... ^ Uc
 * U1 = HMAC(P, S || INT(i)); U_{n+1} = HMAC(P, U_n)
 */
static void Pbkdf2Block(const UINT8 *Password, UINTN PasswordLength,
                        const UINT8 *Salt, UINTN SaltLength,
                        UINT32 Iterations, UINT32 BlockIndex,
                        UINT8 Output[WIFI_SHA1_DIGEST_LENGTH])
{
    UINT8 Message[WIFI_PBKDF2_SALT_MAX_LENGTH + WIFI_PBKDF2_BLOCK_INDEX_LENGTH];
    UINT8 U[WIFI_SHA1_DIGEST_LENGTH];
    UINT8 T[WIFI_SHA1_DIGEST_LENGTH];
    UINTN MessageLength;
    UINTN I;
    UINT32 Count;

    if (SaltLength > WIFI_PBKDF2_SALT_MAX_LENGTH) {
        SaltLength = WIFI_PBKDF2_SALT_MAX_LENGTH;
    }
    MemCopy(Message, Salt, SaltLength);
    Message[SaltLength]     = (UINT8)(BlockIndex >> 24);
    Message[SaltLength + 1] = (UINT8)(BlockIndex >> 16);
    Message[SaltLength + 2] = (UINT8)(BlockIndex >> 8);
    Message[SaltLength + 3] = (UINT8)BlockIndex;
    MessageLength = SaltLength + WIFI_PBKDF2_BLOCK_INDEX_LENGTH;

    WifiSha1Hmac(Password, PasswordLength, Message, MessageLength, U);
    MemCopy(T, U, WIFI_SHA1_DIGEST_LENGTH);

    for (Count = 1; Count < Iterations; Count++) {
        WifiSha1Hmac(Password, PasswordLength, U, WIFI_SHA1_DIGEST_LENGTH, U);
        MemXor(T, U, WIFI_SHA1_DIGEST_LENGTH);
    }
    for (I = 0; I < WIFI_SHA1_DIGEST_LENGTH; I++) {
        Output[I] = T[I];
    }
}

int WifiPbkdf2Sha1(const char *Password, const UINT8 *Salt, UINTN SaltLength,
                   UINT32 Iterations, UINT8 *Output, UINTN OutputLength)
{
    UINT8 Block[WIFI_SHA1_DIGEST_LENGTH];
    UINTN PasswordBytes;
    UINTN Copied;
    UINT32 Index;

    if (Password == NULL || Salt == NULL || Output == NULL
        || Iterations == 0 || OutputLength == 0) {
        return 0;
    }
    PasswordBytes = PasswordLength(Password);
    if (PasswordBytes == 0) {
        return 0;
    }

    Copied = 0;
    Index = 1;
    while (Copied < OutputLength) {
        UINTN Take;
        UINTN I;
        Pbkdf2Block((const UINT8 *)Password, PasswordBytes, Salt, SaltLength,
                    Iterations, Index, Block);
        Take = OutputLength - Copied;
        if (Take > WIFI_SHA1_DIGEST_LENGTH) {
            Take = WIFI_SHA1_DIGEST_LENGTH;
        }
        for (I = 0; I < Take; I++) {
            Output[Copied + I] = Block[I];
        }
        Copied += Take;
        Index++;
    }
    return 1;
}
