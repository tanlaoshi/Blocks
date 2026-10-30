/*
 * WifiEapol.c — EAPOL-Key MIC、AES-128-CMAC 与 GTK 解密（自 Intel 驱动原样搬出）。
 *
 * 从哪读：WifiEapolMic（版本 2 = HMAC-SHA1-128，版本 3 = AES-128-CMAC）
 *         → WifiGtkUnwrap（RFC 3394）→ WifiGtkExtract（取 GTK KDE）。
 * 别改：MIC 偏移 81、Key Data 偏移 99、DD|00-0F-AC-01 的解析顺序都是空口契约。
 */
#include "WifiCrypto/WifiEapol.h"
#include "WifiCrypto/WifiPrf.h"
#include "WifiCrypto/WifiSha1.h"

#define WIFI_EAPOL_DIGEST_MAX_LENGTH 32u /* 工作缓冲：SHA1 20B / CMAC 16B 都放得下 */

/* RFC 4493：AES-128-CMAC（M1 密钥描述符版本 3 的 EAPOL MIC） */
#define WIFI_AES_CMAC_SHIFT_BITS   7u   /* RFC 4493 §2.3：取最高位判 Rb */
#define WIFI_AES_CMAC_RB           0x87u /* RFC 4493 §2.3：128-bit 的 Rb */
#define WIFI_AES_CMAC_BLOCK_LENGTH 16u
#define WIFI_AES_CMAC_PAD_BYTE     0x80u /* RFC 4493 §2.4：末块不足补 0x80 */

#define WIFI_AES_WRAP_SEMIBLOCK_LENGTH 8u  /* RFC 3394 §2：n 个 64-bit 半块 */
#define WIFI_AES_WRAP_ROUNDS           6u  /* RFC 3394 §2.2.2：j = 5..0 */
#define WIFI_AES_WRAP_IV               0xa6u /* RFC 3394 §2.2.3.1：A 初值字节 */
#define WIFI_AES_WRAP_T_OFFSET         4u   /* RFC 3394 §2.2.2：t 异或进 A 的后 4 字节 */
#define WIFI_AES_WRAP_MAX_BLOCKS       24u /* 本实现上限：明文 192B（刀 #177 扩过） */
#define WIFI_AES_WRAP_MIN_LENGTH       16u
#define WIFI_AES_WRAP_MAX_LENGTH       (WIFI_AES_WRAP_SEMIBLOCK_LENGTH * (WIFI_AES_WRAP_MAX_BLOCKS + 1u))
#define WIFI_GTK_PLAIN_MAX_LENGTH      192u

/* IEEE 802.11-2020 §12.7.2：组密钥 KDE = DD | Len | 00-0F-AC-01 | KeyID | Rsvd | GTK */
#define WIFI_GTK_KDE_ID                0xddu
#define WIFI_GTK_KDE_OUI_0             0x00u
#define WIFI_GTK_KDE_OUI_1             0x0fu
#define WIFI_GTK_KDE_OUI_2             0xacu
#define WIFI_GTK_KDE_TYPE              0x01u
#define WIFI_GTK_KDE_HEADER_LENGTH     6u  /* Id+Len+OUI+Type 不算密钥域 */
#define WIFI_GTK_KDE_OUI_OFFSET        2u  /* 相对 KDE 起点：OUI 4 字节 */
#define WIFI_GTK_KDE_KEY_ID_OFFSET     6u  /* 相对 KDE 起点：KeyID | Rsvd */
#define WIFI_GTK_KDE_MIN_LENGTH        (WIFI_GTK_KDE_HEADER_LENGTH + 16u)
#define WIFI_GTK_DATA_OFFSET           8u  /* 相对 KDE 起点：KeyID/Rsvd 之后 */
#define WIFI_GTK_KEY_ID_MASK           0x03u
#define WIFI_GTK_ALTERNATE_OFFSET      16u /* 相对 KDE 起点；TKIP 32B 密钥的后 16B 给 CCMP 备选 */
#define WIFI_GTK_ALTERNATE_MIN_LENGTH  24u
#define WIFI_GROUP_CIPHER_TKIP         0x02u /* RSN group cipher suite 末字节 */

void WifiEapolMic(UINT8 KeyVersion, const UINT8 Kck[WIFI_EAPOL_KCK_LENGTH],
                  UINT8 *Eapol, UINTN EapolLength,
                  UINT8 Mic[WIFI_EAPOL_MIC_LENGTH])
{
    UINT8 Digest[WIFI_EAPOL_DIGEST_MAX_LENGTH];
    UINTN I;

    for (I = 0; I < WIFI_EAPOL_MIC_LENGTH; I++) {
        Eapol[WIFI_EAPOL_MIC_OFFSET + I] = 0;
        Digest[I] = 0;
    }
    if (KeyVersion == WIFI_KEY_DESCRIPTOR_VERSION_AES_CMAC) {
        WifiAesCmac(Kck, Eapol, EapolLength, Digest);
    } else {
        WifiSha1Hmac(Kck, WIFI_EAPOL_KCK_LENGTH, Eapol, EapolLength, Digest);
    }
    for (I = 0; I < WIFI_EAPOL_MIC_LENGTH; I++) {
        Mic[I] = Digest[I];
        Eapol[WIFI_EAPOL_MIC_OFFSET + I] = Digest[I];
    }
}

/*
 * RFC 3394。WPA2 M3 的密钥数据用 KEK 包了组密钥。
 * 刀 #177：旧实现明文最多 48B（密文 56）。Apple M3 常带 RSN+GTK(+IGTK) 更长 → gtk=bad。
 * 扩到 24 个 8B 块（明文 192 / 密文 200），盖住 EAPOL Key Data 实际上限。
 */
int WifiGtkUnwrap(const UINT8 Kek[WIFI_EAPOL_KEK_LENGTH], const UINT8 *Wrapped,
                  UINTN WrappedLength, UINT8 *Plain, UINTN PlainCapacity,
                  UINTN *PlainLength)
{
    UINT8 A[WIFI_AES_WRAP_SEMIBLOCK_LENGTH];
    UINT8 R[WIFI_AES_WRAP_MAX_BLOCKS][WIFI_AES_WRAP_SEMIBLOCK_LENGTH];
    UINTN N;
    UINTN K;
    int J;
    int I;

    if (Kek == NULL || Wrapped == NULL || Plain == NULL || PlainLength == NULL) {
        return 0;
    }
    if ((WrappedLength & (WIFI_AES_WRAP_SEMIBLOCK_LENGTH - 1u)) != 0
        || WrappedLength < WIFI_AES_WRAP_MIN_LENGTH
        || WrappedLength > WIFI_AES_WRAP_MAX_LENGTH) {
        return 0;
    }
    N = WrappedLength / WIFI_AES_WRAP_SEMIBLOCK_LENGTH - 1u;
    if (N == 0 || N > WIFI_AES_WRAP_MAX_BLOCKS
        || N * WIFI_AES_WRAP_SEMIBLOCK_LENGTH > PlainCapacity) {
        return 0;
    }
    for (K = 0; K < WIFI_AES_WRAP_SEMIBLOCK_LENGTH; K++) {
        A[K] = Wrapped[K];
    }
    for (K = 0; K < N; K++) {
        UINTN B;
        for (B = 0; B < WIFI_AES_WRAP_SEMIBLOCK_LENGTH; B++) {
            R[K][B] = Wrapped[(K + 1u) * WIFI_AES_WRAP_SEMIBLOCK_LENGTH + B];
        }
    }
    for (J = (int)WIFI_AES_WRAP_ROUNDS - 1; J >= 0; J--) {
        for (I = (int)N; I >= 1; I--) {
            UINT8 Block[WIFI_AES_BLOCK_LENGTH];
            UINT8 RoundPlain[WIFI_AES_BLOCK_LENGTH];
            UINT32 T = (UINT32)(N * (UINTN)J + (UINTN)I);
            for (K = 0; K < WIFI_AES_WRAP_SEMIBLOCK_LENGTH; K++) {
                Block[K] = A[K];
                Block[WIFI_AES_WRAP_SEMIBLOCK_LENGTH + K] = R[I - 1][K];
            }
            Block[WIFI_AES_WRAP_T_OFFSET]     ^= (UINT8)(T >> 24);
            Block[WIFI_AES_WRAP_T_OFFSET + 1] ^= (UINT8)(T >> 16);
            Block[WIFI_AES_WRAP_T_OFFSET + 2] ^= (UINT8)(T >> 8);
            Block[WIFI_AES_WRAP_T_OFFSET + 3] ^= (UINT8)T;
            if (!WifiAesDecrypt(Kek, Block, RoundPlain)) {
                return 0;
            }
            for (K = 0; K < WIFI_AES_WRAP_SEMIBLOCK_LENGTH; K++) {
                A[K] = RoundPlain[K];
                R[I - 1][K] = RoundPlain[WIFI_AES_WRAP_SEMIBLOCK_LENGTH + K];
            }
        }
    }
    for (K = 0; K < WIFI_AES_WRAP_SEMIBLOCK_LENGTH; K++) {
        if (A[K] != WIFI_AES_WRAP_IV) {
            return 0;
        }
    }
    for (K = 0; K < N; K++) {
        UINTN B;
        for (B = 0; B < WIFI_AES_WRAP_SEMIBLOCK_LENGTH; B++) {
            Plain[K * WIFI_AES_WRAP_SEMIBLOCK_LENGTH + B] = R[K][B];
        }
    }
    *PlainLength = N * WIFI_AES_WRAP_SEMIBLOCK_LENGTH;
    return 1;
}

int WifiGtkExtract(const UINT8 Kek[WIFI_EAPOL_KEK_LENGTH], const UINT8 *Eapol,
                   UINTN EapolLength, UINT8 GroupCipher, WIFI_GTK *Gtk)
{
    UINT16 KeyDataLength;
    UINT8 Plain[WIFI_GTK_PLAIN_MAX_LENGTH];
    UINTN PlainLength = 0;
    UINTN I;

    if (Kek == NULL || Eapol == NULL || Gtk == NULL) {
        return WIFI_GTK_STATUS_SHORT;
    }
    Gtk->KeyDataLength = 0;
    Gtk->ElementLength = 0;
    Gtk->KeyId = 0;
    Gtk->KeyLength = 0;
    Gtk->AlternateValid = 0;

    if (EapolLength < WIFI_EAPOL_KEY_MIN_LENGTH) {
        return WIFI_GTK_STATUS_SHORT;
    }
    KeyDataLength = (UINT16)(((UINT16)Eapol[WIFI_EAPOL_KEY_DATA_LENGTH_OFFSET] << 8)
                             | Eapol[WIFI_EAPOL_KEY_DATA_LENGTH_OFFSET + 1u]);
    if (KeyDataLength < WIFI_AES_WRAP_MIN_LENGTH
        || WIFI_EAPOL_KEY_DATA_OFFSET + (UINTN)KeyDataLength > EapolLength) {
        return WIFI_GTK_STATUS_RANGE;
    }
    Gtk->KeyDataLength = KeyDataLength;
    if (!WifiGtkUnwrap(Kek, Eapol + WIFI_EAPOL_KEY_DATA_OFFSET, KeyDataLength,
                       Plain, sizeof(Plain), &PlainLength)) {
        return WIFI_GTK_STATUS_UNWRAP;
    }
    for (I = 0; I + 2u <= PlainLength; ) {
        UINT8 ElementId = Plain[I];
        UINT8 ElementLength = Plain[I + 1];
        UINTN KeyBytes;
        UINTN K;

        if (I + 2u + (UINTN)ElementLength > PlainLength) {
            break;
        }
        if (ElementId == WIFI_GTK_KDE_ID
            && (UINTN)ElementLength >= WIFI_GTK_KDE_MIN_LENGTH
            && Plain[I + WIFI_GTK_KDE_OUI_OFFSET] == WIFI_GTK_KDE_OUI_0
            && Plain[I + WIFI_GTK_KDE_OUI_OFFSET + 1] == WIFI_GTK_KDE_OUI_1
            && Plain[I + WIFI_GTK_KDE_OUI_OFFSET + 2] == WIFI_GTK_KDE_OUI_2
            && Plain[I + WIFI_GTK_KDE_OUI_OFFSET + 3] == WIFI_GTK_KDE_TYPE) {
            KeyBytes = (UINTN)ElementLength - WIFI_GTK_KDE_HEADER_LENGTH;
            Gtk->KeyId = (UINT8)(Plain[I + WIFI_GTK_KDE_KEY_ID_OFFSET] & WIFI_GTK_KEY_ID_MASK);
            Gtk->ElementLength = ElementLength;
            if (KeyBytes > WIFI_GTK_MAX_LENGTH) {
                KeyBytes = WIFI_GTK_MAX_LENGTH;
            }
            if (KeyBytes < WIFI_EAPOL_KEK_LENGTH) {
                return WIFI_GTK_STATUS_KEY_SHORT;
            }
            Gtk->KeyLength = (UINT8)KeyBytes;
            for (K = 0; K < (UINTN)Gtk->KeyLength; K++) {
                Gtk->Key[K] = Plain[I + WIFI_GTK_DATA_OFFSET + K];
            }
            if (GroupCipher != WIFI_GROUP_CIPHER_TKIP
                && KeyBytes >= WIFI_GTK_ALTERNATE_MIN_LENGTH) {
                for (K = 0; K < WIFI_GTK_ALTERNATE_LENGTH; K++) {
                    Gtk->Alternate[K] = Plain[I + WIFI_GTK_ALTERNATE_OFFSET + K];
                }
                Gtk->AlternateValid = 1;
            }
            return WIFI_GTK_STATUS_OK;
        }
        I += 2u + (UINTN)ElementLength;
    }
    return WIFI_GTK_STATUS_ABSENT;
}

/* RFC 4493：128-bit 块左移，最高位为 1 时异或 Rb=0x87 */
static void CmacDouble(UINT8 Block[WIFI_AES_CMAC_BLOCK_LENGTH])
{
    UINT8 Msb = (UINT8)(Block[0] >> WIFI_AES_CMAC_SHIFT_BITS);
    UINTN I;

    for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH - 1u; I++) {
        Block[I] = (UINT8)((Block[I] << 1) | (Block[I + 1] >> WIFI_AES_CMAC_SHIFT_BITS));
    }
    Block[WIFI_AES_CMAC_BLOCK_LENGTH - 1] =
        (UINT8)(Block[WIFI_AES_CMAC_BLOCK_LENGTH - 1] << 1);
    if (Msb) {
        Block[WIFI_AES_CMAC_BLOCK_LENGTH - 1] =
            (UINT8)(Block[WIFI_AES_CMAC_BLOCK_LENGTH - 1] ^ WIFI_AES_CMAC_RB);
    }
}

/* EAPOL MIC 的版本 3：AES-128-CMAC，标签 16 字节 */
void WifiAesCmac(const UINT8 Key[WIFI_AES_KEY_LENGTH],
                 const UINT8 *Message, UINTN Length,
                 UINT8 Mac[WIFI_AES_BLOCK_LENGTH])
{
    UINT8 Zero[WIFI_AES_CMAC_BLOCK_LENGTH];
    UINT8 L[WIFI_AES_CMAC_BLOCK_LENGTH];
    UINT8 K1[WIFI_AES_CMAC_BLOCK_LENGTH];
    UINT8 K2[WIFI_AES_CMAC_BLOCK_LENGTH];
    UINT8 X[WIFI_AES_CMAC_BLOCK_LENGTH];
    UINT8 Y[WIFI_AES_CMAC_BLOCK_LENGTH];
    UINTN BlockCount;
    UINTN Offset;
    UINTN I;
    int Complete;

    for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH; I++) {
        Zero[I] = 0;
        X[I] = 0;
    }
    (void)WifiAesEncrypt(Key, Zero, L);
    for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH; I++) {
        K1[I] = L[I];
    }
    CmacDouble(K1);
    for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH; I++) {
        K2[I] = K1[I];
    }
    CmacDouble(K2);

    if (Length == 0 || (Length % WIFI_AES_CMAC_BLOCK_LENGTH) != 0) {
        BlockCount = Length / WIFI_AES_CMAC_BLOCK_LENGTH + 1u;
        Complete = 0;
    } else {
        BlockCount = Length / WIFI_AES_CMAC_BLOCK_LENGTH;
        Complete = 1;
    }
    Offset = 0;
    if (BlockCount > 1) {
        UINTN B;
        for (B = 0; B < BlockCount - 1u; B++) {
            for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH; I++) {
                Y[I] = (UINT8)(X[I] ^ Message[Offset + I]);
            }
            (void)WifiAesEncrypt(Key, Y, X);
            Offset += WIFI_AES_CMAC_BLOCK_LENGTH;
        }
    }
    for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH; I++) {
        Y[I] = 0;
    }
    if (Complete) {
        for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH; I++) {
            Y[I] = (UINT8)(Message[Offset + I] ^ K1[I] ^ X[I]);
        }
    } else {
        UINTN Remainder = Length - Offset;
        for (I = 0; I < Remainder; I++) {
            Y[I] = Message[Offset + I];
        }
        Y[Remainder] = WIFI_AES_CMAC_PAD_BYTE;
        for (I = 0; I < WIFI_AES_CMAC_BLOCK_LENGTH; I++) {
            Y[I] = (UINT8)(Y[I] ^ K2[I] ^ X[I]);
        }
    }
    (void)WifiAesEncrypt(Key, Y, Mac);
}
