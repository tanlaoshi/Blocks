/*
 * WifiHandshakeKey.c — PR-34：PSK→PMK→PTK、SNonce、M2 组帧、MIC 校验
 *
 * 用户眼里：`pbkdf2 N ms`、`snonce=…`、`ptk: ptk/kck/kek/tk…` 这几行，以及
 *           M2 的 121 B 里每个字段从哪来。
 * 从哪读：AicHandshakeDerive（PMK → SNonce → PTK）→ AicHandshakeM2Build（组帧 + MIC）
 *         → AicHandshakeMicCheck（M3 的 MIC 验不验得过）。
 * 别改：算法本体全在 HAL/Common/WifiCrypto（本刀只调用、不复制）；
 *       M2 的字段偏移是 802.11-2016 §12.7.2 图 12-25 的字节契约。
 * 为什么 M2 要带 22 B RSN IE：X64 真机经验（Mac/Eapol/Hand.c:80-92 的刀 #174）——
 *       Apple 对"Key Data 里没有与 assoc 同一份 RSN IE"的 M2 会空口 ACK 但不回 M3。
 *       所以这里直接复用 AicJoinRsnIe()（等于 assoc 请求里那份），不另拼一份。
 */
#include "WifiHandshakePrivate.h"
#include "CString.h"
#include "WifiCrypto/WifiSha1.h"

/* 25 MHz 时基（BoardConfig.h BOARD_TIMEBASE_HZ）→ 毫秒；只用来量 PBKDF2 耗时 */
#define AIC_HANDSHAKE_TIMEBASE_PER_MS (BOARD_TIMEBASE_HZ / 1000u)

/* SNonce 熵材料的内部偏移（总长 AIC_HANDSHAKE_SEED_LENGTH = 28） */
#define AIC_HANDSHAKE_SEED_TIME0_OFFSET 0u   /* 第一次 rdtime 的低 8 B */
#define AIC_HANDSHAKE_SEED_TIME1_OFFSET 8u   /* 第二次 rdtime 的低 8 B */
#define AIC_HANDSHAKE_SEED_BSSID_OFFSET 16u  /* 6 B 目标 BSSID */
#define AIC_HANDSHAKE_SEED_RSSI_OFFSET 22u   /* 1 B 扫描缓存里的 RSSI */
#define AIC_HANDSHAKE_SEED_CHANNEL_OFFSET 23u /* 1 B 信道 */
#define AIC_HANDSHAKE_SEED_COUNTER_OFFSET 24u /* 4 B 同一次上电内的序号 */

/*
 * 同一次上电内每算一个 SNonce 就 +1。这一位**保证唯一性**：即使 rdtime 的两次读数
 * 完全一样（模拟器/极快路径），两次 join 也不会算出同一个 SNonce。
 * 初值是 0，不加时间种子 —— 它唯一性就够，随机性由 rdtime + SHA-1 提供。
 */
static UINT32 sAicSnonceCounter;

static void HandshakePutBigEndian16(UINT8 *At, UINT16 Value) {
    At[0] = (UINT8)((Value >> 8) & 0xFFu);
    At[1] = (UINT8)(Value & 0xFFu);
}

/*
 * SNonce：32 B，不能用固定值（固定 = 每次 PTK 一样，且给重放留门）。
 * 熵源与取舍（本板没有可读的真随机源：C906 没有暴露 TRNG 寄存器，AIC8800 也不通过
 * SDIO 消息给随机数，所以用"抖动 + 上下文 + 混合"三代材料）：
 *   1) 两次紧邻的 rdtime（25 MHz 裸计数，BoardConfig.h）：低 20 位随流水线、
 *      中断、SDIO 事务抖动；两次读数之差本身就是抖动，取两次的绝对值再 SHA-1。
 *   2) 关联上下文：目标 BSSID + 扫描 RSSI + 信道 —— 换一个 AP/位置就换一份材料。
 *   3) 同一次上电内的序号：保证唯一性（见 sAicSnonceCounter 的注释）。
 * 三代材料用 SHA-1 分两个域（域名+轮次+种子）各压 20 B，取前 32 B。
 * 坦白：这不是密码学意义的 CSPRNG，强度低于硬件 TRNG；但 SNonce 的作用是"每次不同、
 * 不可预知到能防重放"，且它会明文出现在 M2 里（不是秘密）。真正的强度边界是 PSK。
 */
static void HandshakeMakeSnonce(UINT8 Snonce[WIFI_NONCE_LENGTH], const AIC_SCAN_AP *Target) {
    UINT8 Seed[AIC_HANDSHAKE_SEED_LENGTH];
    UINT8 Input[AIC_HANDSHAKE_SNONCE_LABEL_LENGTH + 1u + AIC_HANDSHAKE_SEED_LENGTH];
    UINT8 Digest[WIFI_SHA1_DIGEST_LENGTH];
    const UINT32 Half = WIFI_NONCE_LENGTH / AIC_HANDSHAKE_SNONCE_ROUNDS;
    UINT64 T0 = SdhciReadTime();
    UINT64 T1 = SdhciReadTime();
    UINT32 Round;
    UINT32 Index;

    for (Index = 0; Index < 8u; Index++) {
        Seed[AIC_HANDSHAKE_SEED_TIME0_OFFSET + Index] = (UINT8)(T0 >> (Index * 8u));
        Seed[AIC_HANDSHAKE_SEED_TIME1_OFFSET + Index] = (UINT8)(T1 >> (Index * 8u));
    }
    for (Index = 0; Index < WIFI_MAC_ADDRESS_LENGTH; Index++) {
        Seed[AIC_HANDSHAKE_SEED_BSSID_OFFSET + Index] = Target->Bssid[Index];
    }
    Seed[AIC_HANDSHAKE_SEED_RSSI_OFFSET] = (UINT8)Target->Rssi;
    Seed[AIC_HANDSHAKE_SEED_CHANNEL_OFFSET] = Target->Channel;
    sAicSnonceCounter++;
    for (Index = 0; Index < 4u; Index++) {
        Seed[AIC_HANDSHAKE_SEED_COUNTER_OFFSET + Index] =
            (UINT8)(sAicSnonceCounter >> (Index * 8u));
    }
    for (Index = 0; Index < AIC_HANDSHAKE_SNONCE_LABEL_LENGTH; Index++) {
        Input[Index] = (UINT8)AIC_HANDSHAKE_SNONCE_LABEL[Index];
    }
    for (Round = 0; Round < AIC_HANDSHAKE_SNONCE_ROUNDS; Round++) {
        Input[AIC_HANDSHAKE_SNONCE_LABEL_LENGTH] = (UINT8)Round;
        for (Index = 0; Index < AIC_HANDSHAKE_SEED_LENGTH; Index++) {
            Input[AIC_HANDSHAKE_SNONCE_LABEL_LENGTH + 1u + Index] = Seed[Index];
        }
        WifiSha1Digest(Input, sizeof(Input), Digest);
        for (Index = 0; Index < Half; Index++) {
            Snonce[Round * Half + Index] = Digest[Index];
        }
    }
    memset(Seed, 0, sizeof(Seed));
    memset(Input, 0, sizeof(Input));
    memset(Digest, 0, sizeof(Digest));
}

void AicHandshakePrintSnonce(const UINT8 *Snonce) {
    SdhciMark("WiFi: snonce=");
    AicEapolMarkBytes("", Snonce, WIFI_NONCE_LENGTH);
    SdhciMark(" src=rdtime-both+bssid/rssi/chan+seq sha1\n");
}

/* 验收判据那一行：四个 16 B 各取前 16（ptk 的前 16 B 就是 kck，重复打是为了好对拍） */
void AicHandshakePrintPtk(const UINT8 *Ptk) {
    SdhciMark("WiFi: ptk: ptk=");
    AicEapolMarkBytes("", Ptk, WIFI_KCK_LENGTH);
    SdhciMark(" kck=");
    AicEapolMarkBytes("", Ptk, WIFI_KCK_LENGTH);
    SdhciMark(" kek=");
    AicEapolMarkBytes("", Ptk + WIFI_KEK_OFFSET, WIFI_KEK_LENGTH);
    SdhciMark(" tk=");
    AicEapolMarkBytes("", Ptk + WIFI_TK_OFFSET, WIFI_TK_LENGTH);
    SdhciMark("\n");
}

/*
 * PMK = PBKDF2-HMAC-SHA1(密码, SSID, 4096, 32)（IEEE 802.11-2020 §12.7.1.2）；
 * PTK = PRF-384(PMK, Min/Max(MAC), Min/Max(Nonce), 版本)（§12.7.1.3）。
 * 耗时按 rdtime 算并打出来：RISC-V 上的实测值就是这条狗要的数（>500 ms 提示）。
 */
int AicHandshakeDerive(const AIC_SCAN_AP *Target, const char *Password,
                       const AIC_HANDSHAKE_M1 *M1, UINT8 Pmk[WIFI_PMK_LENGTH],
                       UINT8 Snonce[WIFI_NONCE_LENGTH], UINT8 Ptk[WIFI_PTK_LENGTH]) {
    UINT32 SsidLength = 0;
    UINT64 Start;
    UINT32 ElapsedMs;

    while (SsidLength < AIC_JOIN_SSID_MAX && Target->Ssid[SsidLength] != 0) {
        SsidLength++;
    }
    Start = SdhciReadTime();
    if (WifiPbkdf2Sha1(Password, (const UINT8 *)Target->Ssid, SsidLength,
                       WIFI_PBKDF2_WPA2_ITERATIONS, Pmk, WIFI_PMK_LENGTH) == 0) {
        SdhciMark("WiFi: handshake FAILED at PMK: pbkdf2 failed (empty/too long password?)\n");
        return -1;
    }
    ElapsedMs = (UINT32)((SdhciReadTime() - Start) / AIC_HANDSHAKE_TIMEBASE_PER_MS);
    SdhciMark("WiFi: pbkdf2 ");
    SdhciMarkDecimal("", ElapsedMs);
    SdhciMark(" ms (");
    SdhciMarkDecimal("", WIFI_PBKDF2_WPA2_ITERATIONS);
    SdhciMark(" iters, sha1-hmac, this CPU)\n");
    if (ElapsedMs > AIC_HANDSHAKE_PBKDF2_WARN_MS) {
        SdhciMark("WiFi: pbkdf2 WARNING over ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_PBKDF2_WARN_MS);
        SdhciMark(" ms: the kernel is blocked this long on every join\n");
    }
    HandshakeMakeSnonce(Snonce, Target);
    AicHandshakePrintSnonce(Snonce);
    WifiPtkDerive(Pmk, M1->Station, Target->Bssid, M1->Anonce, Snonce, Ptk, M1->KeyVersion);
    AicHandshakePrintPtk(Ptk);
    return 0;
}

/*
 * M2（802.11-2016 §12.7.6.2 的 Msg 2）：
 *   version ← M1，type=3(Key)，length = 95 + KeyDataLen
 *   descriptor ← M1（2 = RSN），key_info = pairwise|MIC|版本，key_len = 16
 *   replay ← M1，nonce ← SNonce，MIC 先 0，Key Data = 22 B RSN IE
 * 组完再算 MIC（WifiEapolMic 会就地把 +81 清零再写回）。
 */
UINT32 AicHandshakeM2Build(UINT8 *Frame, const AIC_HANDSHAKE_M1 *M1, const UINT8 *Snonce,
                           const UINT8 *Kck, UINT8 Mic[WIFI_EAPOL_MIC_LENGTH]) {
    const UINT32 Total = AIC_EAPOL_KEY_FIXED_LENGTH + AIC_HANDSHAKE_KEY_DATA_LENGTH;
    UINT16 KeyInformation = (UINT16)(AIC_HANDSHAKE_M2_FLAGS | M1->KeyVersion);

    memset(Frame, 0, Total);
    Frame[0] = M1->Frame[0]; /* EAPOL 版本跟 M1（1 = 802.1X-2001，2 = 2004） */
    Frame[1] = (UINT8)AIC_EAPOL_TYPE_KEY;
    HandshakePutBigEndian16(Frame + 2, (UINT16)(Total - AIC_EAPOL_HEADER_LENGTH));
    Frame[AIC_EAPOL_KEY_DESCRIPTOR_OFFSET] = M1->Frame[AIC_EAPOL_KEY_DESCRIPTOR_OFFSET];
    HandshakePutBigEndian16(Frame + AIC_EAPOL_KEY_INFORMATION_OFFSET, KeyInformation);
    HandshakePutBigEndian16(Frame + AIC_EAPOL_KEY_LENGTH_OFFSET, (UINT16)AIC_HANDSHAKE_KEY_LENGTH);
    (void)memcpy(Frame + AIC_EAPOL_REPLAY_OFFSET, M1->Replay, AIC_EAPOL_REPLAY_LENGTH);
    (void)memcpy(Frame + AIC_EAPOL_NONCE_OFFSET, Snonce, WIFI_NONCE_LENGTH);
    HandshakePutBigEndian16(Frame + WIFI_EAPOL_KEY_DATA_LENGTH_OFFSET,
                            (UINT16)AIC_HANDSHAKE_KEY_DATA_LENGTH);
    (void)memcpy(Frame + WIFI_EAPOL_KEY_DATA_OFFSET, AicJoinRsnIe(), AIC_HANDSHAKE_KEY_DATA_LENGTH);
    WifiEapolMic(M1->KeyVersion, Kck, Frame, Total, Mic);
    return Total;
}

/*
 * M4（802.11-2016 §12.7.6.2 的 Msg 4；对拍对象 wpa_supplicant 2.10 wpa.c:1615-1653）：
 *   version ← M3，type = 3(Key)，length = 95（没有 Key Data ⇒ 整帧 99 B）
 *   descriptor ← M3，key_info = 版本 | pairwise | install | MIC | secure
 *   key_length = 0（wpa.c:1641-1642 对 RSN 写 0），replay ← M3，nonce 全 0
 * 组完再算 MIC（WifiEapolMic 会就地把 +81 清零再写回）。
 */
UINT32 AicHandshakeM4Build(UINT8 *Frame, const AIC_HANDSHAKE_M3 *M3, const UINT8 *Kck,
                           UINT8 Mic[WIFI_EAPOL_MIC_LENGTH]) {
    const UINT32 Total = AIC_EAPOL_KEY_FIXED_LENGTH;
    UINT16 KeyInformation = (UINT16)(AIC_HANDSHAKE_M4_FLAGS | M3->KeyVersion);

    memset(Frame, 0, Total);
    Frame[0] = M3->Frame[0]; /* EAPOL 版本跟 M3 */
    Frame[1] = (UINT8)AIC_EAPOL_TYPE_KEY;
    HandshakePutBigEndian16(Frame + 2, (UINT16)(Total - AIC_EAPOL_HEADER_LENGTH));
    Frame[AIC_EAPOL_KEY_DESCRIPTOR_OFFSET] = M3->Frame[AIC_EAPOL_KEY_DESCRIPTOR_OFFSET];
    HandshakePutBigEndian16(Frame + AIC_EAPOL_KEY_INFORMATION_OFFSET, KeyInformation);
    HandshakePutBigEndian16(Frame + AIC_EAPOL_KEY_LENGTH_OFFSET,
                            (UINT16)AIC_HANDSHAKE_M4_KEY_LENGTH);
    (void)memcpy(Frame + AIC_EAPOL_REPLAY_OFFSET, M3->Replay, AIC_EAPOL_REPLAY_LENGTH);
    WifiEapolMic(M3->KeyVersion, Kck, Frame, Total, Mic);
    return Total;
}

/*
 * MIC 校验：在**副本**上算（WifiEapolMic 会把 +81 清零再写回，不能动收到的帧）。
 * 帧长用 EAPOL 头里的 length 字段（不是"可用字节数"）：AP 可能把帧塞在更长的缓冲里，
 * 多算几个尾部字节 MIC 就永远对不上。长度自相矛盾时返回 0（不猜）。
 */
int AicHandshakeMicCheck(const UINT8 *Body, UINT32 Length, UINT8 KeyVersion, const UINT8 *Kck) {
    UINT8 Copy[AIC_HANDSHAKE_EAPOL_MAX];
    UINT8 Mic[WIFI_EAPOL_MIC_LENGTH];
    UINT32 Total = (UINT32)AicEapolGetBigEndian16(Body + 2) + AIC_EAPOL_HEADER_LENGTH;
    UINT32 Index;
    int Same = 1;

    if (Total < AIC_EAPOL_KEY_FIXED_LENGTH || Total > Length || Total > sizeof(Copy)) {
        return 0;
    }
    (void)memcpy(Copy, Body, Total);
    WifiEapolMic(KeyVersion, Kck, Copy, Total, Mic);
    for (Index = 0; Index < WIFI_EAPOL_MIC_LENGTH; Index++) {
        if (Mic[Index] != Body[WIFI_EAPOL_MIC_OFFSET + Index]) {
            Same = 0;
        }
    }
    memset(Copy, 0, sizeof(Copy));
    memset(Mic, 0, sizeof(Mic));
    return Same;
}
