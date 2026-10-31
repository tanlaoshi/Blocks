/*
 * WifiKeyAdd.c — 把 PTK / GTK 交给固件：MM_KEY_ADD_REQ(0x24) → CFM(0x25)
 *
 * 用户眼里：`WiFi: key add: ptk sta=0 cipher=ccmp(2) rc=0 cfm=0` 与
 *           `WiFi: key add: gtk sta=255 keyid=1 cipher=tkip(1) rc=0 cfm=0`。
 *           这两行之后，固件才有钥匙去解 AP 发来的加密帧。
 * 从哪读：AicKeyAddPairwise / AicKeyAddGroup（对外的两个入口）
 *         → KeyAddExchange（拼 → 发 → 等 CFM）→ KeyAddWaitCfm（走 EAPOL 轮询）
 *         → AicKeyAddBuild（44 B 请求体）。
 * 别改：字段顺序/宽度/偏移是厂商 lmac_msg.h:797-812 + lmac_mac.h:292-297 的字节契约，
 *       AicKeyAddLayoutCheck 在发送前用编译出来的 sizeof/offsetof 核对，排歪就拒发。
 *       **cipher_suite 两个钥匙不同**：单播 CCMP(2)、组播 TKIP(1) ——
 *       这个 AP 是 WPA/WPA2 混合，组播套件 TKIP（真机空口实测），填错的话固件
 *       解不了组播帧，现象是"握手 ok 但数据帧 decr=0"。
 * 为什么等 CFM 也走 EAPOL 轮询：装钥匙的那一刻正是 AP 开始送加密数据帧的时候，
 *       AicRecvMessageTimeout 会把数据帧当"坏 pattern"读掉丢掉（WifiMessage.c），
 *       而 AicEapolPollRun 会照常解析它们（CFM 的识别见 WifiEapol.c 的配置帧分支）。
 */
#include "WifiHandshakePrivate.h"
#include "CString.h"
#include "Compiler.h" /* BLOCKS_OFFSETOF：与 join/eapol 同一套布局自检 */

/* 44 B 请求体；UINT32 数组保证 4 B 对齐（请求里 key.array 是 u32） */
static UINT32 sAicKeyAdd[(AIC_KEY_ADD_LENGTH + 3u) / 4u];

/* 发送结果：rc=0/1（有没有写进 SDIO FIFO）+ cfm（CFM.status；没等到 = NONE） */
#define AIC_KEY_ADD_SENT_OK 0u
#define AIC_KEY_ADD_SENT_FAILED 1u
#define AIC_KEY_ADD_CFM_NONE 0xFFFFFFFFu

typedef struct {
    UINT32 Sent;      /* 0 = 写进了 FIFO；1 = 没写进去 */
    UINT32 CfmStatus; /* 0 = 固件装好了；非 0 = 拒绝；NONE = 没等到 CFM */
    UINT32 HwIndex;   /* CFM 的 hw_key_idx（固件分配的硬件钥匙号） */
} AIC_KEY_ADD_RESULT;

typedef struct {
    const char *Name;
    UINT32 Actual;
    UINT32 Expected;
} AIC_KEY_ADD_LAYOUT_ITEM;

/* 期望值全部来自宿主 gcc include 厂商 lmac_msg.h 的编译输出（2026-10-31 实测：44/36/2） */
static const AIC_KEY_ADD_LAYOUT_ITEM sAicKeyAddLayout[] = {
    {"mm_key_add_req", (UINT32)sizeof(AIC_MM_KEY_ADD), AIC_KEY_ADD_LENGTH},
    {"mm_key_add_req.key_idx", (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, KeyIndex), 0u},
    {"mm_key_add_req.sta_idx", (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, StaIndex), 1u},
    {"mm_key_add_req.key", (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, Key), 4u},
    {"mm_key_add_req.key.length",
     (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, Key) +
         (UINT32)BLOCKS_OFFSETOF(AIC_MAC_SEC_KEY, Length),
     4u},
    {"mm_key_add_req.key.array",
     (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, Key) +
         (UINT32)BLOCKS_OFFSETOF(AIC_MAC_SEC_KEY, Array),
     8u},
    {"mm_key_add_req.cipher_suite", (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, CipherSuite), 40u},
    {"mm_key_add_req.inst_nbr", (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, InstanceNumber), 41u},
    {"mm_key_add_req.spp", (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, Spp), 42u},
    {"mm_key_add_req.pairwise", (UINT32)BLOCKS_OFFSETOF(AIC_MM_KEY_ADD, Pairwise), 43u},
    {"mac_sec_key", (UINT32)sizeof(AIC_MAC_SEC_KEY), 36u},
    {"mm_key_add_cfm", (UINT32)sizeof(AIC_MM_KEY_ADD_CONFIRM), AIC_KEY_CFM_LENGTH},
};

#define AIC_KEY_ADD_LAYOUT_COUNT \
    ((UINT32)(sizeof(sAicKeyAddLayout) / sizeof(sAicKeyAddLayout[0])))

/* 一条不匹配就停：镜像排歪了，固件会把 44 B 读成别的意思 */
int AicKeyAddLayoutCheck(void) {
    UINT32 Index;
    int Bad = 0;

    for (Index = 0; Index < AIC_KEY_ADD_LAYOUT_COUNT; Index++) {
        const AIC_KEY_ADD_LAYOUT_ITEM *Item = &sAicKeyAddLayout[Index];

        if (Item->Actual == Item->Expected) {
            continue;
        }
        SdhciMark("WiFi: key add struct layout BAD: ");
        SdhciMark(Item->Name);
        SdhciMarkHex(" got=", Item->Actual, 4);
        SdhciMarkHex(" want=", Item->Expected, 4);
        SdhciMark("\n");
        Bad = 1;
    }
    if (Bad != 0) {
        SdhciMark("WiFi: key add struct layout check failed, refuse to send\n");
        return -1;
    }
    return 0;
}

/*
 * 拼请求。厂商的 rwnx_send_key_add（rwnx_msg_tx.c:648-668）只填这些字段：
 * key_idx 仅在默认钥匙时有意义、sta_idx、pairwise、inst_nbr = vif、key.length、
 * key.array（memcpy 整段钥匙）、cipher_suite；其余留在 zalloc 的 0 上。
 */
UINT32 AicKeyAddBuild(UINT8 *Buffer, UINT8 VifIndex, UINT8 StaIndex, UINT8 Pairwise,
                      const UINT8 *Key, UINT8 KeyLength, UINT8 KeyIndex, UINT8 CipherSuite) {
    AIC_MM_KEY_ADD *Request = (AIC_MM_KEY_ADD *)Buffer;
    UINT8 *Material = (UINT8 *)Request->Key.Array;
    UINT32 Index;

    if (KeyLength == 0u || KeyLength > AIC_KEY_SEC_KEY_LENGTH) {
        return 0u;
    }
    memset(Request, 0, sizeof(*Request));
    Request->KeyIndex = KeyIndex;
    Request->StaIndex = StaIndex;
    Request->Key.Length = KeyLength;
    for (Index = 0; Index < (UINT32)KeyLength; Index++) {
        Material[Index] = Key[Index];
    }
    Request->CipherSuite = CipherSuite;
    Request->InstanceNumber = VifIndex;
    Request->Spp = (UINT8)AIC_KEY_ADD_SPP_DISABLED;
    Request->Pairwise = Pairwise;
    return (UINT32)sizeof(*Request);
}

/* 提前退出判据：CFM 一到就停，不等满预算（同控制端口 CFM 的做法） */
static int KeyAddCfmDone(void) {
    return (int)gAicEapolKeyAddCfm;
}

/* 0 = 等到 CFM；-1 = 预算内没有（或 SDIO 读失败，已打印原因） */
static int KeyAddWaitCfm(SDHCI_HOST *Host, UINT32 *OutStatus, UINT32 *OutHwIndex) {
    AIC_EAPOL_COUNTERS Counters;

    gAicEapolKeyAddCfm = 0u;
    gAicEapolKeyAddStatus = 0u;
    gAicEapolKeyAddHwIndex = 0u;
    memset(&Counters, 0, sizeof(Counters));
    if (AicEapolPollRun(Host, AIC_HANDSHAKE_KEY_ADD_MS, &Counters, KeyAddCfmDone) != 0) {
        SdhciMark("WiFi: key add: SDIO read failed while waiting for MM_KEY_ADD_CFM(0x25)\n");
        return -1;
    }
    if (gAicEapolKeyAddCfm == 0u) {
        SdhciMark("WiFi: key add: no MM_KEY_ADD_CFM(0x25) in ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_KEY_ADD_MS);
        SdhciMark("ms (frames=");
        SdhciMarkDecimal("", Counters.Frames);
        SdhciMark(")\n");
        return -1;
    }
    *OutStatus = gAicEapolKeyAddStatus;
    *OutHwIndex = gAicEapolKeyAddHwIndex;
    return 0;
}

/* 拼 → 发 → 等 CFM。打印留给调用方：PTK 与 GTK 的判据行字段不同 */
static AIC_KEY_ADD_RESULT KeyAddExchange(SDHCI_HOST *Host, UINT8 VifIndex, UINT8 StaIndex,
                                         UINT8 Pairwise, const UINT8 *Key, UINT8 KeyLength,
                                         UINT8 KeyIndex, UINT8 CipherSuite) {
    AIC_KEY_ADD_RESULT Result;
    UINT32 Length;

    Result.Sent = AIC_KEY_ADD_SENT_FAILED;
    Result.CfmStatus = AIC_KEY_ADD_CFM_NONE;
    Result.HwIndex = 0u;
    Length = AicKeyAddBuild((UINT8 *)sAicKeyAdd, VifIndex, StaIndex, Pairwise, Key, KeyLength,
                            KeyIndex, CipherSuite);
    if (Length == 0u) {
        SdhciMark("WiFi: key add: bad key length=");
        SdhciMarkDecimal("", KeyLength);
        SdhciMark(" (want 1..");
        SdhciMarkDecimal("", AIC_KEY_SEC_KEY_LENGTH);
        SdhciMark(")\n");
        return Result;
    }
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_KEY_ADD_REQ, (UINT16)AIC_TASK_MM, sAicKeyAdd,
                         (UINT16)Length) != 0) {
        return Result;
    }
    Result.Sent = AIC_KEY_ADD_SENT_OK;
    if (KeyAddWaitCfm(Host, &Result.CfmStatus, &Result.HwIndex) != 0) {
        Result.CfmStatus = AIC_KEY_ADD_CFM_NONE;
    }
    return Result;
}

/* `rc=… cfm=…`：rc 是发送结果（0 = 写进 FIFO），cfm 是固件的回答（0 = 装好了） */
static void KeyAddMarkResult(const AIC_KEY_ADD_RESULT *Result) {
    SdhciMark(" rc=");
    SdhciMarkDecimal("", Result->Sent);
    SdhciMark(" cfm=");
    if (Result->CfmStatus == AIC_KEY_ADD_CFM_NONE) {
        SdhciMark("none");
    } else {
        SdhciMarkDecimal("", Result->CfmStatus);
    }
    SdhciMark("\n");
}

static int KeyAddIsInstalled(const AIC_KEY_ADD_RESULT *Result) {
    /* 写进 FIFO（Sent=0）且 CFM.status 明确为 0；NONE(0xFFFFFFFF) 与非 0 都不算 */
    return (int)(Result->Sent == AIC_KEY_ADD_SENT_OK && Result->CfmStatus == 0u);
}

/*
 * PTK：钥匙材料是 PTK 的 TK（PTK 偏移 32，16 B），sta_idx 用 SM_CONNECT_IND.ap_idx
 * （固件给这个 AP 分配的 sta 索引），cipher 用 CCMP —— 单播是 CCMP，
 * 与组播的 TKIP 不是同一把（见文件头）。
 */
int AicKeyAddPairwise(SDHCI_HOST *Host, UINT8 VifIndex, UINT8 StaIndex, const UINT8 *Tk) {
    AIC_KEY_ADD_RESULT Result =
        KeyAddExchange(Host, VifIndex, StaIndex, (UINT8)AIC_KEY_ADD_PAIRWISE, Tk,
                       (UINT8)WIFI_TK_LENGTH, (UINT8)AIC_KEY_ADD_PAIRWISE_INDEX,
                       (UINT8)AIC_KEY_CIPHER_CCMP);

    SdhciMark("WiFi: key add: ptk sta=");
    SdhciMarkDecimal("", StaIndex);
    SdhciMark(" cipher=ccmp(");
    SdhciMarkDecimal("", AIC_KEY_CIPHER_CCMP);
    SdhciMark(")");
    KeyAddMarkResult(&Result);
    if (KeyAddIsInstalled(&Result) == 0) {
        SdhciMark("WiFi: handshake FAILED at PTK install: the firmware did not install the"
                  " pairwise key (rc/cfm above; sta_idx must be SM_CONNECT_IND.ap_idx)\n");
        return -1;
    }
    return 0;
}

/*
 * GTK：默认钥匙（sta_idx = 0xFF），key_idx 用 M3 的 KDE 给的 KeyID，
 * cipher 用 TKIP —— **组播**是 TKIP（这个 AP 的 RSN 组播套件，真机空口实测），
 * 长度照 KDE（TKIP 是 32 B：16 B 密钥 + 16 B MIC 密钥）。
 */
int AicKeyAddGroup(SDHCI_HOST *Host, UINT8 VifIndex, const WIFI_GTK *Gtk) {
    AIC_KEY_ADD_RESULT Result;

    if (Gtk->KeyLength == 0u || Gtk->KeyLength > AIC_KEY_SEC_KEY_LENGTH) {
        SdhciMark("WiFi: key add: gtk length ");
        SdhciMarkDecimal("", Gtk->KeyLength);
        SdhciMark(" out of range (1..32), the KDE did not carry a usable key\n");
        return -1;
    }
    Result = KeyAddExchange(Host, VifIndex, (UINT8)AIC_KEY_ADD_DEFAULT_STA_INDEX,
                            (UINT8)AIC_KEY_ADD_NOT_PAIRWISE, Gtk->Key, Gtk->KeyLength,
                            Gtk->KeyId, (UINT8)AIC_KEY_CIPHER_TKIP);
    SdhciMark("WiFi: key add: gtk sta=");
    SdhciMarkDecimal("", AIC_KEY_ADD_DEFAULT_STA_INDEX);
    SdhciMark(" keyid=");
    SdhciMarkDecimal("", Gtk->KeyId);
    SdhciMark(" cipher=tkip(");
    SdhciMarkDecimal("", AIC_KEY_CIPHER_TKIP);
    SdhciMark(")");
    KeyAddMarkResult(&Result);
    if (KeyAddIsInstalled(&Result) == 0) {
        SdhciMark("WiFi: handshake FAILED at GTK install: the firmware did not install the default"
                  " key (rc/cfm above; inst_nbr must be the join vif)\n");
        return -1;
    }
    return 0;
}
