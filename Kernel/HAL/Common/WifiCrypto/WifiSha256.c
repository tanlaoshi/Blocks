/*
 * WifiSha256.c — SHA-256、HMAC-SHA-256 与 802.11 KDF（自 Intel 驱动原样搬出）。
 *
 * 从哪读：WifiKdfSha256（SHA-256 / HMAC 只在文件内 static）。
 * 别改：KDF 的 counter、label 补 0、末尾 bit 长度是 802.11 的字节契约。
 */
#include "WifiCrypto/WifiSha256.h"

#define WIFI_SHA256_BLOCK_LENGTH       64u  /* FIPS 180-4 §5.2.1：512-bit 分组 */
#define WIFI_SHA256_WORD_BITS          32u
#define WIFI_SHA256_WORD_LENGTH        4u
#define WIFI_SHA256_BLOCK_WORDS        16u
#define WIFI_SHA256_SCHEDULE_WORDS     64u  /* FIPS 180-4 §6.2.2：W[0..63] */
#define WIFI_SHA256_ROUNDS             64u  /* FIPS 180-4 §6.2.2：64 轮 */
#define WIFI_SHA256_STATE_WORDS        8u   /* §6.2.1：H0..H7 */
#define WIFI_SHA256_PAD_BYTE           0x80u
#define WIFI_SHA256_LENGTH_FIELD_OFFSET 56u
#define WIFI_SHA256_IPAD               0x36u /* FIPS 198-1 §4 */
#define WIFI_SHA256_OPAD               0x5cu
#define WIFI_SHA256_ROTATE_A           7u   /* §4.1.2 的 σ0/σ1 与 Σ0/Σ1 旋量 */
#define WIFI_SHA256_ROTATE_B           18u
#define WIFI_SHA256_ROTATE_C           3u
#define WIFI_SHA256_ROTATE_D           17u
#define WIFI_SHA256_ROTATE_E           19u
#define WIFI_SHA256_ROTATE_F           10u
#define WIFI_SHA256_ROTATE_G           6u
#define WIFI_SHA256_ROTATE_H           11u
#define WIFI_SHA256_ROTATE_I           25u
#define WIFI_SHA256_ROTATE_J           2u
#define WIFI_SHA256_ROTATE_K           13u
#define WIFI_SHA256_ROTATE_L           22u

#define WIFI_KDF_COUNTER_LENGTH        2u   /* 802.11-2020 §12.7.1.3：counter 16-bit */
#define WIFI_KDF_LENGTH_FIELD_LENGTH   2u   /* 输出 bit 长度 16-bit */
#define WIFI_KDF_MESSAGE_MAX_LENGTH    160u /* 本实现上限：label ≤ 32 + context ≤ 76 + 头部 */

/* FIPS 180-4 §4.2.2：64 个轮常量 */
static const UINT32 gWifiSha256K[WIFI_SHA256_ROUNDS] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static UINT32 Rotr(UINT32 X, UINTN N)
{
    return (X >> N) | (X << (WIFI_SHA256_WORD_BITS - N));
}

static void Sha256Block(UINT32 S[WIFI_SHA256_STATE_WORDS],
                        const UINT8 Block[WIFI_SHA256_BLOCK_LENGTH])
{
    UINT32 W[WIFI_SHA256_SCHEDULE_WORDS];
    UINT32 A, B, C, D, E, F, G, H;
    UINTN I;

    for (I = 0; I < WIFI_SHA256_BLOCK_WORDS; I++) {
        W[I] = ((UINT32)Block[I * WIFI_SHA256_WORD_LENGTH] << 24)
             | ((UINT32)Block[I * WIFI_SHA256_WORD_LENGTH + 1] << 16)
             | ((UINT32)Block[I * WIFI_SHA256_WORD_LENGTH + 2] << 8)
             | (UINT32)Block[I * WIFI_SHA256_WORD_LENGTH + 3];
    }
    for (I = WIFI_SHA256_BLOCK_WORDS; I < WIFI_SHA256_SCHEDULE_WORDS; I++) {
        UINT32 S0 = Rotr(W[I - 15], WIFI_SHA256_ROTATE_A)
                  ^ Rotr(W[I - 15], WIFI_SHA256_ROTATE_B)
                  ^ (W[I - 15] >> WIFI_SHA256_ROTATE_C);
        UINT32 S1 = Rotr(W[I - 2], WIFI_SHA256_ROTATE_D)
                  ^ Rotr(W[I - 2], WIFI_SHA256_ROTATE_E)
                  ^ (W[I - 2] >> WIFI_SHA256_ROTATE_F);
        W[I] = W[I - 16] + S0 + W[I - 7] + S1;
    }
    A = S[0]; B = S[1]; C = S[2]; D = S[3];
    E = S[4]; F = S[5]; G = S[6]; H = S[7];
    for (I = 0; I < WIFI_SHA256_ROUNDS; I++) {
        UINT32 S1 = Rotr(E, WIFI_SHA256_ROTATE_G)
                  ^ Rotr(E, WIFI_SHA256_ROTATE_H)
                  ^ Rotr(E, WIFI_SHA256_ROTATE_I);
        UINT32 Ch = (E & F) ^ ((~E) & G);
        UINT32 T1 = H + S1 + Ch + gWifiSha256K[I] + W[I];
        UINT32 S0 = Rotr(A, WIFI_SHA256_ROTATE_J)
                  ^ Rotr(A, WIFI_SHA256_ROTATE_K)
                  ^ Rotr(A, WIFI_SHA256_ROTATE_L);
        UINT32 Maj = (A & B) ^ (A & C) ^ (B & C);
        UINT32 T2 = S0 + Maj;
        H = G; G = F; F = E; E = D + T1; D = C; C = B; B = A; A = T1 + T2;
    }
    S[0] += A; S[1] += B; S[2] += C; S[3] += D;
    S[4] += E; S[5] += F; S[6] += G; S[7] += H;
}

static void Sha256Init(UINT32 S[WIFI_SHA256_STATE_WORDS])
{
    S[0] = 0x6a09e667u; S[1] = 0xbb67ae85u; S[2] = 0x3c6ef372u; S[3] = 0xa54ff53au;
    S[4] = 0x510e527fu; S[5] = 0x9b05688cu; S[6] = 0x1f83d9abu; S[7] = 0x5be0cd19u;
}

static void Sha256Finish(UINT32 S[WIFI_SHA256_STATE_WORDS],
                         UINT8 Block[WIFI_SHA256_BLOCK_LENGTH],
                         UINTN Used, UINT64 TotalLength)
{
    UINT64 Bits;
    UINTN I;

    Block[Used++] = WIFI_SHA256_PAD_BYTE;
    if (Used > WIFI_SHA256_LENGTH_FIELD_OFFSET) {
        while (Used < WIFI_SHA256_BLOCK_LENGTH) {
            Block[Used++] = 0;
        }
        Sha256Block(S, Block);
        Used = 0;
    }
    while (Used < WIFI_SHA256_LENGTH_FIELD_OFFSET) {
        Block[Used++] = 0;
    }
    Bits = TotalLength * 8u;
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET]     = (UINT8)(Bits >> 56);
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET + 1] = (UINT8)(Bits >> 48);
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET + 2] = (UINT8)(Bits >> 40);
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET + 3] = (UINT8)(Bits >> 32);
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET + 4] = (UINT8)(Bits >> 24);
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET + 5] = (UINT8)(Bits >> 16);
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET + 6] = (UINT8)(Bits >> 8);
    Block[WIFI_SHA256_LENGTH_FIELD_OFFSET + 7] = (UINT8)Bits;
    Sha256Block(S, Block);
    for (I = 0; I < WIFI_SHA256_STATE_WORDS; I++) {
        Block[I * WIFI_SHA256_WORD_LENGTH]     = (UINT8)(S[I] >> 24);
        Block[I * WIFI_SHA256_WORD_LENGTH + 1] = (UINT8)(S[I] >> 16);
        Block[I * WIFI_SHA256_WORD_LENGTH + 2] = (UINT8)(S[I] >> 8);
        Block[I * WIFI_SHA256_WORD_LENGTH + 3] = (UINT8)S[I];
    }
}

static void Sha256Digest(const UINT8 *Data, UINTN Length,
                         UINT8 Digest[WIFI_SHA256_DIGEST_LENGTH])
{
    UINT32 S[WIFI_SHA256_STATE_WORDS];
    UINT8 Block[WIFI_SHA256_BLOCK_LENGTH];
    UINTN Offset;
    UINTN I;
    UINTN N;

    Sha256Init(S);
    Offset = 0;
    while (Offset + WIFI_SHA256_BLOCK_LENGTH <= Length) {
        Sha256Block(S, Data + Offset);
        Offset += WIFI_SHA256_BLOCK_LENGTH;
    }
    N = Length - Offset;
    for (I = 0; I < WIFI_SHA256_BLOCK_LENGTH; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Offset + I];
    }
    Sha256Finish(S, Block, N, Length);
    for (I = 0; I < WIFI_SHA256_DIGEST_LENGTH; I++) {
        Digest[I] = Block[I];
    }
}

static void HmacSha256(const UINT8 *Key, UINTN KeyLength,
                       const UINT8 *Data, UINTN Length,
                       UINT8 Mac[WIFI_SHA256_DIGEST_LENGTH])
{
    UINT8 KeyPad[WIFI_SHA256_BLOCK_LENGTH];
    UINT8 KeyDigest[WIFI_SHA256_DIGEST_LENGTH];
    UINT8 Inner[WIFI_SHA256_DIGEST_LENGTH];
    UINT8 Outer[WIFI_SHA256_BLOCK_LENGTH + WIFI_SHA256_DIGEST_LENGTH];
    UINT32 S[WIFI_SHA256_STATE_WORDS];
    UINT8 Block[WIFI_SHA256_BLOCK_LENGTH];
    UINTN I;
    UINTN Offset;
    UINTN N;
    const UINT8 *UsedKey;
    UINTN UsedKeyLength;

    if (KeyLength > WIFI_SHA256_BLOCK_LENGTH) {
        Sha256Digest(Key, KeyLength, KeyDigest);
        UsedKey = KeyDigest;
        UsedKeyLength = WIFI_SHA256_DIGEST_LENGTH;
    } else {
        UsedKey = Key;
        UsedKeyLength = KeyLength;
    }
    for (I = 0; I < WIFI_SHA256_BLOCK_LENGTH; I++) {
        KeyPad[I] = 0;
    }
    for (I = 0; I < UsedKeyLength; I++) {
        KeyPad[I] = UsedKey[I];
    }
    Sha256Init(S);
    for (I = 0; I < WIFI_SHA256_BLOCK_LENGTH; I++) {
        Block[I] = (UINT8)(KeyPad[I] ^ WIFI_SHA256_IPAD);
    }
    Sha256Block(S, Block);
    Offset = 0;
    while (Offset + WIFI_SHA256_BLOCK_LENGTH <= Length) {
        Sha256Block(S, Data + Offset);
        Offset += WIFI_SHA256_BLOCK_LENGTH;
    }
    N = Length - Offset;
    for (I = 0; I < WIFI_SHA256_BLOCK_LENGTH; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Offset + I];
    }
    Sha256Finish(S, Block, N, WIFI_SHA256_BLOCK_LENGTH + Length);
    for (I = 0; I < WIFI_SHA256_DIGEST_LENGTH; I++) {
        Inner[I] = Block[I];
    }
    for (I = 0; I < WIFI_SHA256_BLOCK_LENGTH; I++) {
        Outer[I] = (UINT8)(KeyPad[I] ^ WIFI_SHA256_OPAD);
    }
    for (I = 0; I < WIFI_SHA256_DIGEST_LENGTH; I++) {
        Outer[WIFI_SHA256_BLOCK_LENGTH + I] = Inner[I];
    }
    Sha256Digest(Outer, WIFI_SHA256_BLOCK_LENGTH + WIFI_SHA256_DIGEST_LENGTH,
                 Mac);
}

/*
 * 802.11 KDF-SHA-256：counter(2, 从 1) || label || 0x00 || context || bitlen(2)
 */
void WifiKdfSha256(const UINT8 *Key, UINTN KeyLength, const char *Label,
                   const UINT8 *Context, UINTN ContextLength,
                   UINT8 *Output, UINTN OutputLength)
{
    UINT8 Message[WIFI_KDF_MESSAGE_MAX_LENGTH];
    UINT8 Hash[WIFI_SHA256_DIGEST_LENGTH];
    UINTN LabelLength = 0;
    UINTN Copied = 0;
    UINT16 Counter = 1;
    UINT16 Bits = (UINT16)(OutputLength * 8u);

    if (Label == NULL || Output == NULL || OutputLength == 0) {
        return;
    }
    while (Label[LabelLength] != 0) {
        LabelLength++;
    }
    while (Copied < OutputLength) {
        UINTN MessageLength = 0;
        UINTN I;
        UINTN Take;
        Message[MessageLength++] = (UINT8)(Counter >> 8);
        Message[MessageLength++] = (UINT8)Counter;
        for (I = 0; I < LabelLength && MessageLength < sizeof(Message); I++) {
            Message[MessageLength++] = (UINT8)Label[I];
        }
        if (MessageLength < sizeof(Message)) {
            Message[MessageLength++] = 0;
        }
        for (I = 0; I < ContextLength && MessageLength < sizeof(Message); I++) {
            Message[MessageLength++] = Context[I];
        }
        if (MessageLength + WIFI_KDF_LENGTH_FIELD_LENGTH <= sizeof(Message)) {
            Message[MessageLength++] = (UINT8)(Bits >> 8);
            Message[MessageLength++] = (UINT8)Bits;
        }
        HmacSha256(Key, KeyLength, Message, MessageLength, Hash);
        Take = OutputLength - Copied;
        if (Take > WIFI_SHA256_DIGEST_LENGTH) {
            Take = WIFI_SHA256_DIGEST_LENGTH;
        }
        for (I = 0; I < Take; I++) {
            Output[Copied + I] = Hash[I];
        }
        Copied += Take;
        Counter++;
    }
}
