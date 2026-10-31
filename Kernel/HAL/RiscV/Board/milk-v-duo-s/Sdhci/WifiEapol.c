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
/* 控制端口 CFM 的"见过"标志（PR-34）：置位在下面的配置帧分支，清零在 WifiHandshake.c */
UINT32 gAicEapolControlPortCfm;
/* MM_KEY_ADD_CFM(0x25) 的"见过"标志与两个字段（PR-35）：清零在 WifiKeyAdd.c 发请求前 */
UINT32 gAicEapolKeyAddCfm;
UINT32 gAicEapolKeyAddStatus;
UINT32 gAicEapolKeyAddHwIndex;
/* PTK 已装进固件（WifiHandshakeInstall.c 置位）：解密帧的文案据此换挡 */
UINT32 gAicEapolKeysInstalled;

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

/*
 * MM_KEY_ADD_CFM(0x25) 的两个字段：status（0 = 固件装好了）、hw_key_idx（硬件钥匙号）。
 * 只有参数够长（>= 2 B）才采信，否则只置"见过"——宁可在 rc/cfm 行上报 cfm=none，
 * 也不把半截参数当成"装好了"。
 */
static void EapolRecordKeyAddCfm(const UINT8 *Frame, UINT32 Length, UINT16 ParameterLength) {
    gAicEapolKeyAddCfm = 1u;
    if (Length < AIC_E2A_PARAM_OFF + AIC_EAPOL_KEY_ADD_CFM_LENGTH ||
        ParameterLength < AIC_EAPOL_KEY_ADD_CFM_LENGTH) {
        gAicEapolKeyAddStatus = AIC_EAPOL_KEY_ADD_STATUS_SHORT;
        gAicEapolKeyAddHwIndex = 0u;
        return;
    }
    gAicEapolKeyAddStatus =
        (UINT32)Frame[AIC_E2A_PARAM_OFF + AIC_EAPOL_KEY_ADD_STATUS_OFFSET];
    gAicEapolKeyAddHwIndex =
        (UINT32)Frame[AIC_E2A_PARAM_OFF + AIC_EAPOL_KEY_ADD_HW_INDEX_OFFSET];
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
    } else if (MessageId == AIC_ME_SET_CONTROL_PORT_CFM) {
        SdhciMark(" (ME_SET_CONTROL_PORT_CFM: EAPOL control port is open)");
        gAicEapolControlPortCfm = 1u;
    } else if (MessageId == AIC_MM_KEY_ADD_CFM) {
        EapolRecordKeyAddCfm(Frame, Length, ParameterLength);
        SdhciMark(" (MM_KEY_ADD_CFM: status=");
        SdhciMarkDecimal("", gAicEapolKeyAddStatus);
        SdhciMark(" hw_key_idx=");
        SdhciMarkDecimal("", gAicEapolKeyAddHwIndex);
        SdhciMark(")");
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
 * 量一帧在 SDIO 读缓冲里占多少字节。厂商 aicwf_process_rxframes 的同一套算术
 * （aicwf_txrxif.c:290-350）：
 *   数据帧：总长 = 60(pkt_len 之后的硬件头) + pkt_len，对齐到 4
 *   配置帧：总长 = align4(pkt_len) + 4 B 块头
 * pkt_len 就是帧首 2 B（`data[0] | data[1]<<8`，:306）；为 0 表示后面没有帧了
 * （aicwf_another_ptk 的判据，:290-300）。
 * 返回 0 = 到此为止（空帧 / 长度越界）。
 */
static UINT32 EapolFrameSpan(const UINT8 *Frame, UINT32 Available, UINT32 *OutFrameLength) {
    UINT32 PktLength = (UINT32)Frame[0] | ((UINT32)Frame[1] << 8);
    UINT32 Span;

    if (PktLength == 0u) {
        return 0u;
    }
    if ((Frame[AIC_SDIO_TYPE_OFFSET] & AIC_SDIO_TYPE_CFG_MASK) == AIC_SDIO_TYPE_CFG) {
        Span = PktLength + AIC_RX_CFG_HEADER_LENGTH;
    } else {
        Span = PktLength + AIC_RX_HW_HEADER_LENGTH;
    }
    Span = (Span + (AIC_RX_FRAME_ALIGN - 1u)) & ~(AIC_RX_FRAME_ALIGN - 1u);
    if (Span > Available) {
        SdhciMarkHex("WiFi: eapol: aggregate span overruns block, span=", Span, 4);
        SdhciMarkHex(" left=", Available, 4);
        SdhciMark(" (stop walking this block)\n");
        return 0u;
    }
    *OutFrameLength = Span;
    return Span;
}

/*
 * 一块（一次 CMD53 读，512 B 的整数倍）里可能挤着好几帧 —— 厂商 aicwf_process_rxframes
 * 就是 `while (aicwf_another_ptk())` 逐帧走的（aicwf_txrxif.c:290-350）。
 * 以前这里只解**第一帧**：一个 beacon 帧 280 B + M1 180 B 正好同块 ⇒ M1 被整帧丢掉，
 * 日志上只看到 beacon，像是"AP 没送 M1"。现在按厂商的顺序逐帧解。
 */
static void EapolDispatchBlock(const UINT8 *Frame, UINT32 Length, AIC_EAPOL_COUNTERS *Counters) {
    UINT32 Offset = 0;
    UINT32 InBlock = 0;

    while (Offset + AIC_SDIO_MIN_FRAME_LENGTH <= Length) {
        UINT32 FrameLength = 0;

        if (EapolFrameSpan(Frame + Offset, Length - Offset, &FrameLength) == 0u) {
            break;
        }
        EapolDispatchFrame(Frame + Offset, FrameLength, Counters);
        Counters->Frames++;
        Offset += FrameLength;
        InBlock++;
    }
    if (InBlock > 1u) {
        SdhciMarkHex("WiFi: eapol: aggregate in one block=", InBlock, 2);
        SdhciMarkHex(" read=", Length, 4);
        SdhciMark(" (vendor parses every frame; the tail used to be dropped)\n");
    }
}

/*
 * 预算用 rdtime 算，不用"循环次数 × 片长"：AicWaitMessagePending 一次就等到
 * 剩余预算（没有消息时它自己按 1 ms 轮询），所以整段最多出现一条超时诊断行，
 * 不会把串口刷满。
 *
 * Done 是 PR-34 给四次握手加的提前退出：每解完一帧问一次，返回 1 就停。
 * 判据放在"解完一帧之后"，M1 的打印与捕获都已经落定，不会读到半帧就收工。
 */
/*
 * 非消息帧钩子（见 WifiPrivate.h）：任何"等消息"的窗口里读到的非 e2a 帧都会到这里。
 * 用途：AP 的 EAPOL M1 可能在等 CFM/IND 的那一刻到达，以前会被 AicFrameCheck 判失败后丢掉。
 * 现在按数据帧分流，交给逐帧 EAPOL 解析；只认数据帧（type bit4 == 0），配置/未知帧忽略。
 */
void AicFrameUnexpected(const UINT8 *Frame, UINT32 Length) {
    static AIC_EAPOL_COUNTERS Counters;

    if (Frame == 0 || Length < AIC_EAPOL_HEADER_LENGTH) {
        return;
    }
    if ((Frame[2] & 0x10u) == 0x10u) {
        return; /* 配置帧：不属于这里 */
    }
    memset(&Counters, 0, sizeof(Counters));
    SdhciMark("WiFi: window data frame (len=");
    SdhciMarkDecimal("", Length);
    SdhciMark(") — parsing for EAPOL\n");
    EapolDispatchBlock(Frame, Length, &Counters);
}

int AicEapolPollRun(SDHCI_HOST *Host, UINT32 BudgetMs, AIC_EAPOL_COUNTERS *Counters,
                    int (*Done)(void)) {
    UINT8 *Frame = (UINT8 *)sAicEapolFrame;
    UINT64 Start = SdhciReadTime();
    UINT64 Budget = (UINT64)BudgetMs * AIC_EAPOL_TIMEBASE_PER_MS;

    while (Counters->Frames < AIC_EAPOL_POLL_MAX_FRAMES) {
        UINT64 Elapsed = SdhciReadTime() - Start;
        UINT32 LeftMs;
        UINT32 Length = 0;
        int Status;

        if (Done != 0 && Done() != 0) {
            break;
        }
        if (Elapsed >= Budget) {
            break;
        }
        LeftMs = (UINT32)((Budget - Elapsed) / AIC_EAPOL_TIMEBASE_PER_MS);
        if (LeftMs == 0u) {
            LeftMs = 1u;
        }
        Counters->Polls++;
        Status = EapolReadFrame(Host, LeftMs, Frame, AIC_EAPOL_FRAME_MAX, &Length);
        if (Status == -1) {
            Counters->EmptyPolls++;
            continue; /* 超时：下一圈 Elapsed >= Budget 就退出 */
        }
        if (Status != 0) {
            return -1;
        }
        if (Length == 0u) {
            continue;
        }
        EapolDispatchBlock(Frame, Length, Counters);
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
    Status = AicEapolPollRun(Host, Budget, &Counters, 0);
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
    SdhciMark(" polls=");
    SdhciMarkDecimal("", Counters.Polls);
    SdhciMark(" empty=");
    SdhciMarkDecimal("", Counters.EmptyPolls);
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
