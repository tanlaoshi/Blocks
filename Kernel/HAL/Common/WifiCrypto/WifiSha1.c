/*
 * WifiSha1.c — SHA-1 与 HMAC-SHA1（自 Intel 驱动原样搬出，无 libc、无硬件依赖）。
 *
 * 从哪读：WifiSha1Digest → WifiSha1Hmac。
 * 别改：算法逐字取自驱动原 Sha1.c；常量含义见各处 FIPS 出处注释。
 */
#include "WifiCrypto/WifiSha1.h"

#define WIFI_SHA1_WORD_BITS           32u  /* FIPS 180-4 §2：SHA-1 字宽 32-bit */
#define WIFI_SHA1_WORD_LENGTH         4u   /* 32-bit = 4 字节 */
#define WIFI_SHA1_STATE_WORDS         5u   /* FIPS 180-4 §6.1.1：H0..H4 */
#define WIFI_SHA1_BLOCK_WORDS         16u  /* 64B 分组 = 16 个字 */
#define WIFI_SHA1_SCHEDULE_WORDS      80u  /* FIPS 180-4 §6.1.2：W[0..79] */
#define WIFI_SHA1_ROUNDS              80u  /* FIPS 180-4 §6.1.3：80 轮 */
#define WIFI_SHA1_PHASE1_END          20u  /* 第 1 段轮界（FIPS 180-4 §6.1.3） */
#define WIFI_SHA1_PHASE2_END          40u  /* 第 2 段轮界 */
#define WIFI_SHA1_PHASE3_END          60u  /* 第 3 段轮界 */
#define WIFI_SHA1_ROTATE_SCHEDULE     1u   /* 消息扩展左旋 1 */
#define WIFI_SHA1_ROTATE_A            5u   /* 轮函数左旋 5 */
#define WIFI_SHA1_ROTATE_B            30u  /* 轮函数左旋 30 */
#define WIFI_SHA1_PAD_BYTE            0x80u /* FIPS 180-4 §5.1.1：填充首字节 */
#define WIFI_SHA1_LENGTH_FIELD_LENGTH 8u   /* §5.1.1：末尾 64-bit 长度域 */
#define WIFI_SHA1_LENGTH_FIELD_OFFSET 56u  /* 64 - 8 */
#define WIFI_SHA1_IPAD                0x36u /* FIPS 198-1 §4：ipad */
#define WIFI_SHA1_OPAD                0x5cu /* FIPS 198-1 §4：opad */

/* FIPS 180-4 §5.3.1 初始散列值 */
#define WIFI_SHA1_INIT_0 0x67452301u
#define WIFI_SHA1_INIT_1 0xEFCDAB89u
#define WIFI_SHA1_INIT_2 0x98BADCFEu
#define WIFI_SHA1_INIT_3 0x10325476u
#define WIFI_SHA1_INIT_4 0xC3D2E1F0u

/* FIPS 180-4 §4.2.1 轮常量 */
#define WIFI_SHA1_K0 0x5A827999u
#define WIFI_SHA1_K1 0x6ED9EBA1u
#define WIFI_SHA1_K2 0x8F1BBCDCu
#define WIFI_SHA1_K3 0xCA62C1D6u

static UINT32 Rol(UINT32 X, UINTN N)
{
    return (X << N) | (X >> (WIFI_SHA1_WORD_BITS - N));
}

static void Sha1Block(UINT32 H[WIFI_SHA1_STATE_WORDS], const UINT8 Block[WIFI_SHA1_BLOCK_LENGTH])
{
    UINT32 W[WIFI_SHA1_SCHEDULE_WORDS];
    UINT32 A, B, C, D, E, F, K, T;
    UINTN I;

    for (I = 0; I < WIFI_SHA1_BLOCK_WORDS; I++) {
        W[I] = ((UINT32)Block[I * WIFI_SHA1_WORD_LENGTH] << 24) |
               ((UINT32)Block[I * WIFI_SHA1_WORD_LENGTH + 1] << 16) |
               ((UINT32)Block[I * WIFI_SHA1_WORD_LENGTH + 2] << 8) |
               (UINT32)Block[I * WIFI_SHA1_WORD_LENGTH + 3];
    }
    for (I = WIFI_SHA1_BLOCK_WORDS; I < WIFI_SHA1_SCHEDULE_WORDS; I++) {
        W[I] = Rol(W[I - 3] ^ W[I - 8] ^ W[I - 14] ^ W[I - 16],
                   WIFI_SHA1_ROTATE_SCHEDULE);
    }

    A = H[0]; B = H[1]; C = H[2]; D = H[3]; E = H[4];
    for (I = 0; I < WIFI_SHA1_ROUNDS; I++) {
        if (I < WIFI_SHA1_PHASE1_END) {
            F = (B & C) | ((~B) & D);
            K = WIFI_SHA1_K0;
        } else if (I < WIFI_SHA1_PHASE2_END) {
            F = B ^ C ^ D;
            K = WIFI_SHA1_K1;
        } else if (I < WIFI_SHA1_PHASE3_END) {
            F = (B & C) | (B & D) | (C & D);
            K = WIFI_SHA1_K2;
        } else {
            F = B ^ C ^ D;
            K = WIFI_SHA1_K3;
        }
        T = Rol(A, WIFI_SHA1_ROTATE_A) + F + E + K + W[I];
        E = D; D = C; C = Rol(B, WIFI_SHA1_ROTATE_B); B = A; A = T;
    }
    H[0] += A; H[1] += B; H[2] += C; H[3] += D; H[4] += E;
}

static void Sha1Init(UINT32 H[WIFI_SHA1_STATE_WORDS])
{
    H[0] = WIFI_SHA1_INIT_0;
    H[1] = WIFI_SHA1_INIT_1;
    H[2] = WIFI_SHA1_INIT_2;
    H[3] = WIFI_SHA1_INIT_3;
    H[4] = WIFI_SHA1_INIT_4;
}

static void Sha1Finish(UINT32 H[WIFI_SHA1_STATE_WORDS], UINT8 Block[WIFI_SHA1_BLOCK_LENGTH],
                       UINTN Used, UINT64 TotalLen)
{
    UINT64 BitLen;
    UINTN I;

    Block[Used++] = WIFI_SHA1_PAD_BYTE;
    if (Used > WIFI_SHA1_LENGTH_FIELD_OFFSET) {
        while (Used < WIFI_SHA1_BLOCK_LENGTH) {
            Block[Used++] = 0;
        }
        Sha1Block(H, Block);
        Used = 0;
    }
    while (Used < WIFI_SHA1_LENGTH_FIELD_OFFSET) {
        Block[Used++] = 0;
    }
    BitLen = TotalLen * 8u;
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET]     = (UINT8)(BitLen >> 56);
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET + 1] = (UINT8)(BitLen >> 48);
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET + 2] = (UINT8)(BitLen >> 40);
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET + 3] = (UINT8)(BitLen >> 32);
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET + 4] = (UINT8)(BitLen >> 24);
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET + 5] = (UINT8)(BitLen >> 16);
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET + 6] = (UINT8)(BitLen >> 8);
    Block[WIFI_SHA1_LENGTH_FIELD_OFFSET + 7] = (UINT8)BitLen;
    Sha1Block(H, Block);
    for (I = 0; I < WIFI_SHA1_STATE_WORDS; I++) {
        Block[I * WIFI_SHA1_WORD_LENGTH]     = (UINT8)(H[I] >> 24);
        Block[I * WIFI_SHA1_WORD_LENGTH + 1] = (UINT8)(H[I] >> 16);
        Block[I * WIFI_SHA1_WORD_LENGTH + 2] = (UINT8)(H[I] >> 8);
        Block[I * WIFI_SHA1_WORD_LENGTH + 3] = (UINT8)H[I];
    }
}

void WifiSha1Digest(const UINT8 *Data, UINTN Length,
                    UINT8 Digest[WIFI_SHA1_DIGEST_LENGTH])
{
    UINT32 H[WIFI_SHA1_STATE_WORDS];
    UINT8 Block[WIFI_SHA1_BLOCK_LENGTH];
    UINTN Offset;
    UINTN I;
    UINTN N;

    Sha1Init(H);
    Offset = 0;
    while (Offset + WIFI_SHA1_BLOCK_LENGTH <= Length) {
        Sha1Block(H, Data + Offset);
        Offset += WIFI_SHA1_BLOCK_LENGTH;
    }
    N = Length - Offset;
    for (I = 0; I < WIFI_SHA1_BLOCK_LENGTH; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Offset + I];
    }
    Sha1Finish(H, Block, N, Length);
    for (I = 0; I < WIFI_SHA1_DIGEST_LENGTH; I++) {
        Digest[I] = Block[I];
    }
}

void WifiSha1Hmac(const UINT8 *Key, UINTN KeyLength,
                  const UINT8 *Data, UINTN Length,
                  UINT8 Mac[WIFI_SHA1_DIGEST_LENGTH])
{
    UINT8 KeyPad[WIFI_SHA1_BLOCK_LENGTH];
    UINT8 KeyDigest[WIFI_SHA1_DIGEST_LENGTH];
    UINT8 Inner[WIFI_SHA1_DIGEST_LENGTH];
    UINT8 Outer[WIFI_SHA1_BLOCK_LENGTH + WIFI_SHA1_DIGEST_LENGTH];
    UINT32 H[WIFI_SHA1_STATE_WORDS];
    UINT8 Block[WIFI_SHA1_BLOCK_LENGTH];
    UINTN I;
    UINTN Offset;
    UINTN N;
    const UINT8 *UsedKey;
    UINTN UsedKeyLength;

    if (KeyLength > WIFI_SHA1_BLOCK_LENGTH) {
        WifiSha1Digest(Key, KeyLength, KeyDigest);
        UsedKey = KeyDigest;
        UsedKeyLength = WIFI_SHA1_DIGEST_LENGTH;
    } else {
        UsedKey = Key;
        UsedKeyLength = KeyLength;
    }
    for (I = 0; I < WIFI_SHA1_BLOCK_LENGTH; I++) {
        KeyPad[I] = 0;
    }
    for (I = 0; I < UsedKeyLength; I++) {
        KeyPad[I] = UsedKey[I];
    }

    /* inner = SHA1((K ^ ipad) || Data) */
    Sha1Init(H);
    for (I = 0; I < WIFI_SHA1_BLOCK_LENGTH; I++) {
        Block[I] = (UINT8)(KeyPad[I] ^ WIFI_SHA1_IPAD);
    }
    Sha1Block(H, Block);
    Offset = 0;
    while (Offset + WIFI_SHA1_BLOCK_LENGTH <= Length) {
        Sha1Block(H, Data + Offset);
        Offset += WIFI_SHA1_BLOCK_LENGTH;
    }
    N = Length - Offset;
    for (I = 0; I < WIFI_SHA1_BLOCK_LENGTH; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Offset + I];
    }
    Sha1Finish(H, Block, N, WIFI_SHA1_BLOCK_LENGTH + Length);
    for (I = 0; I < WIFI_SHA1_DIGEST_LENGTH; I++) {
        Inner[I] = Block[I];
    }

    for (I = 0; I < WIFI_SHA1_BLOCK_LENGTH; I++) {
        Outer[I] = (UINT8)(KeyPad[I] ^ WIFI_SHA1_OPAD);
    }
    for (I = 0; I < WIFI_SHA1_DIGEST_LENGTH; I++) {
        Outer[WIFI_SHA1_BLOCK_LENGTH + I] = Inner[I];
    }
    WifiSha1Digest(Outer, WIFI_SHA1_BLOCK_LENGTH + WIFI_SHA1_DIGEST_LENGTH, Mac);
}
