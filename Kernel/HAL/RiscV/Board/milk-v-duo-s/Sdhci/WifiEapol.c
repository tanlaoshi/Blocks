/*
 * WifiEapol.c — PR-33：`wififw eapol poll` 的收包/分流/轮询入口
 *
 * 用户眼里：`wififw join` 成功之后，AP 会立刻发 EAPOL-Key M1；这条命令在给定的
 *           毫秒预算里盯着 SDIO 数据帧，把 EAPOL 挑出来交给 WifiEapolParse.c。
 * 从哪读：AicEapolLayoutCheck（布局自检）→ EapolReadFrame（读一整帧）
 *         → EapolDispatchFrame（数据/配置分流）→ AicEapolParseData（解析打印）
 *         → SdhciWifiEapolPoll（板级入口，`wififw eapol poll [ms]`）。
 * 别改：分流判据必须与厂商一致（aicwf_txrxif.c:308 的 `data[2] & 0x10`）。
 *       本文件**不碰** WifiFrame.c/AicReceiveFrameTimeout 的 CFM/IND 通路 ——
 *       那是 join 已验证过的路径，数据帧在这里单独收，互不影响。
 * 本刀不做：四次握手状态机、装密钥、派生 PMK/PTK、发 EAPOL（下一刀）。
 */
#include "WifiEapolPrivate.h"
#include "CString.h"
#include "Compiler.h" /* BLOCKS_OFFSETOF（与 WifiAssocMessage.c 的布局自检同一套） */

/* 2048 B 整帧；UINT32 数组保证 4 B 对齐（hw_rxhdr 里全是 u32） */
static UINT32 sAicEapolFrame[(AIC_EAPOL_FRAME_MAX + 3u) / 4u];

/* 25 MHz 时间基（BoardConfig.h BOARD_TIMEBASE_HZ），只用来算预算，不参与协议 */
#define AIC_EAPOL_TIMEBASE_PER_MS (BOARD_TIMEBASE_HZ / 1000u)

/* 布局核对表：自建镜像的实际值 vs 厂商头文件编译出来的期望值（同 AicJoinLayoutCheck） */
typedef struct {
    const char *Name;
    UINT32 Actual;
    UINT32 Expected;
} AIC_EAPOL_LAYOUT_ITEM;

static const AIC_EAPOL_LAYOUT_ITEM sAicEapolLayout[] = {
    {"hw_rxhdr", (UINT32)sizeof(AIC_RX_HW_HEADER), AIC_RX_HW_STRUCT_LENGTH},
    {"hw_rxhdr.rx_vect1", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Vector1), 12u},
    {"hw_rxhdr.rx_vect2", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Vector2), 28u},
    {"hw_rxhdr.status", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Status),
     AIC_RX_HW_STATUS_OFFSET},
    {"hw_rxhdr.flags", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Flags), AIC_RX_HW_FLAGS_OFFSET},
    {"hw_rxhdr.pattern", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Pattern),
     AIC_RX_HW_PATTERN_OFFSET},
    /* 交叉核对：EAPOL-Key 固定头 99 B 必须等于公共模块的 Key Data 起点 */
    {"eapol.key_data_offset", (UINT32)AIC_EAPOL_KEY_FIXED_LENGTH,
     (UINT32)WIFI_EAPOL_KEY_DATA_OFFSET},
};

#define AIC_EAPOL_LAYOUT_COUNT ((UINT32)(sizeof(sAicEapolLayout) / sizeof(sAicEapolLayout[0])))

/* 一条不匹配就停：布局对不上说明编译器把镜像排歪了，解出来的字段全是垃圾 */
int AicEapolLayoutCheck(void) {
    UINT32 Index;
    int Bad = 0;

    for (Index = 0; Index < AIC_EAPOL_LAYOUT_COUNT; Index++) {
        const AIC_EAPOL_LAYOUT_ITEM *Item = &sAicEapolLayout[Index];

        if (Item->Actual == Item->Expected) {
            continue;
        }
        SdhciMark("WiFi: eapol struct layout BAD: ");
        SdhciMark(Item->Name);
        SdhciMarkHex(" got=", Item->Actual, 4);
        SdhciMarkHex(" want=", Item->Expected, 4);
        SdhciMark("\n");
        Bad = 1;
    }
    if (Bad != 0) {
        SdhciMark("WiFi: eapol struct layout check failed, refuse to poll\n");
        return -1;
    }
    return 0;
}

/*
 * 读一整帧。返回 0 = 读到；-1 = 预算内没消息（正常，下一圈按表停）；
 * -2 = SDIO 读失败（FIFO 可能已串位，继续读只会把后面的帧一起带歪）。
 */
static int EapolReadFrame(SDHCI_HOST *Host, UINT32 TimeoutMs, UINT8 *Frame, UINT32 MaxLength,
                          UINT32 *OutLength) {
    UINT32 Fn = 0;
    UINT32 Length = 0;
    UINT32 Keep;
    UINT32 Done = 0;

    if (AicWaitMessagePending(Host, TimeoutMs, &Fn, &Length) != 0) {
        return -1;
    }
    Keep = Length > MaxLength ? MaxLength : Length;
    while (Done < Keep) {
        UINT32 Chunk = Keep - Done;

        if (Chunk > AIC_FIFO_CHUNK) {
            Chunk = AIC_FIFO_CHUNK;
        }
        if (AicFifoRead(Host, Fn, Frame + Done, Chunk) != 0) {
            return -2;
        }
        Done += Chunk;
    }
    /* 超长帧：余下的也读掉（读进 gAicRx 丢弃），否则下一条消息会从半截开始 */
    while (Keep < Length) {
        UINT32 Chunk = Length - Keep;

        if (Chunk > AIC_FIFO_CHUNK) {
            Chunk = AIC_FIFO_CHUNK;
        }
        if (AicFifoRead(Host, Fn, gAicRx, Chunk) != 0) {
            return -2;
        }
        Keep += Chunk;
    }
    if (Length > MaxLength) {
        SdhciMarkHex("WiFi: eapol: frame bigger than budget, kept first ", MaxLength, 4);
        SdhciMarkHex(" of ", Length, 4);
        SdhciMark("\n");
        Keep = MaxLength;
    }
    *OutLength = Keep;
    return 0;
}

/* 配置帧（0x11 CMD_RSP / 0x12 DATA_CFM / 0x13 PRINT）：不是 EAPOL，但 id 有用 */
static void EapolPrintConfigFrame(const UINT8 *Frame, UINT32 Length,
                                  AIC_EAPOL_COUNTERS *Counters) {
    UINT16 MessageId = 0;
    UINT16 ParameterLength = 0;

    Counters->ConfigFrames++;
    if (Length >= AIC_E2A_PARAM_OFF) {
        MessageId = AicGet16(Frame + 4);
        ParameterLength = AicGet16(Frame + 10);
    }
    SdhciMarkHex("WiFi: eapol: skip config frame type=0x", (UINT32)Frame[2], 2);
    SdhciMarkHex(" id=0x", MessageId, 4);
    SdhciMarkHex(" plen=", ParameterLength, 4);
    if (MessageId == AIC_SM_DISCONNECT_IND) {
        SdhciMark(" (SM_DISCONNECT_IND: assoc lost before M1)");
    } else if (MessageId == AIC_SM_CONNECT_IND) {
        SdhciMark(" (SM_CONNECT_IND)");
    }
    SdhciMark("\n");
}

/* 分流：bit4 = 0 是数据帧（厂商 aicwf_txrxif.c:308 的判据），其余交给数据帧解析 */
static void EapolDispatchFrame(const UINT8 *Frame, UINT32 Length,
                               AIC_EAPOL_COUNTERS *Counters) {
    if (Length < AIC_SDIO_MIN_FRAME_LENGTH) {
        SdhciMarkHex("WiFi: eapol: sdio frame too short len=", Length, 4);
        SdhciMark("\n");
        Counters->BadFrames++;
        return;
    }
    if (((UINT32)Frame[AIC_SDIO_TYPE_OFFSET] & AIC_SDIO_TYPE_CFG_MASK) == AIC_SDIO_TYPE_CFG) {
        EapolPrintConfigFrame(Frame, Length, Counters);
        return;
    }
    Counters->DataFrames++;
    AicEapolParseData(Frame, Length, Counters);
}

/*
 * 预算用 rdtime 算，不用"循环次数 × 片长"：AicWaitMessagePending 一次就等到
 * 剩余预算（没有消息时它自己按 1 ms 轮询），所以整段最多出现一条超时诊断行，
 * 不会把串口刷满。
 */
static int EapolPollRun(SDHCI_HOST *Host, UINT32 BudgetMs, AIC_EAPOL_COUNTERS *Counters) {
    UINT8 *Frame = (UINT8 *)sAicEapolFrame;
    UINT64 Start = SdhciReadTime();
    UINT64 Budget = (UINT64)BudgetMs * AIC_EAPOL_TIMEBASE_PER_MS;

    while (Counters->Frames < AIC_EAPOL_POLL_MAX_FRAMES) {
        UINT64 Elapsed = SdhciReadTime() - Start;
        UINT32 LeftMs;
        UINT32 Length = 0;
        int Status;

        if (Elapsed >= Budget) {
            break;
        }
        LeftMs = (UINT32)((Budget - Elapsed) / AIC_EAPOL_TIMEBASE_PER_MS);
        if (LeftMs == 0u) {
            LeftMs = 1u;
        }
        Status = EapolReadFrame(Host, LeftMs, Frame, AIC_EAPOL_FRAME_MAX, &Length);
        if (Status == -1) {
            continue; /* 超时：下一圈 Elapsed >= Budget 就退出 */
        }
        if (Status != 0) {
            return -1;
        }
        if (Length == 0u) {
            continue;
        }
        Counters->Frames++;
        EapolDispatchFrame(Frame, Length, Counters);
    }
    return 0;
}

/* 十进制毫秒；0 表示用默认值（由调用方决定）。成功 0 */
static int EapolParseMilliseconds(const char *Text, UINT32 *OutMilliseconds) {
    UINT32 Value = 0;
    UINT32 Index = 0;

    if (Text == 0 || Text[0] == 0) {
        return -1;
    }
    while (Text[Index] != 0) {
        char One = Text[Index];

        if (One < '0' || One > '9') {
            return -1;
        }
        Value = Value * 10u + (UINT32)(One - '0');
        if (Value > AIC_EAPOL_POLL_MAX_MS) {
            return -1;
        }
        Index++;
    }
    if (Value == 0u) {
        return -1;
    }
    *OutMilliseconds = Value;
    return 0;
}

int SdhciWifiEapolPoll(const char *MsText) {
    SDHCI_HOST *Host;
    AIC_EAPOL_COUNTERS Counters;
    UINT32 Budget = AIC_EAPOL_POLL_DEFAULT_MS;
    int Status;

    if (MsText != 0 && EapolParseMilliseconds(MsText, &Budget) != 0) {
        SdhciMark("WiFi: eapol poll: bad ms (decimal 1..60000)\n");
        return -1;
    }
    Host = SdhciHostAt(1);
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: eapol poll needs a probed card (run wifiprobe + wififw join first)\n");
        return -1;
    }
    if (AicEapolLayoutCheck() != 0) {
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: eapol poll: vendor prepare failed\n");
        return -1;
    }
    memset(&Counters, 0, sizeof(Counters));
    SdhciMark("WiFi: eapol poll: budget=");
    SdhciMarkDecimal("", Budget);
    SdhciMark("ms, data frames only (type&0x10==0), looking for EAPOL/0x888e\n");
    Status = EapolPollRun(Host, Budget, &Counters);
    SdhciMark("WiFi: eapol poll: done frames=");
    SdhciMarkDecimal("", Counters.Frames);
    SdhciMark(" data=");
    SdhciMarkDecimal("", Counters.DataFrames);
    SdhciMark(" config=");
    SdhciMarkDecimal("", Counters.ConfigFrames);
    SdhciMark(" eapol=");
    SdhciMarkDecimal("", Counters.EapolFrames);
    SdhciMark(" key=");
    SdhciMarkDecimal("", Counters.KeyFrames);
    SdhciMark(" bad=");
    SdhciMarkDecimal("", Counters.BadFrames);
    SdhciMark("\n");
    if (Status != 0) {
        SdhciMark("WiFi: eapol poll: SDIO read failed (FIFO may be out of sync; re-run join)\n");
        return -1;
    }
    if (Counters.KeyFrames == 0u) {
        SdhciMark("WiFi: eapol poll: no EAPOL-Key seen (joined WPA2 AP? AP still at assoc?)\n");
        return -1;
    }
    return 0;
}
