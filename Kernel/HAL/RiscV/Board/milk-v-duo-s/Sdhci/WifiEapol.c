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

/* 2048 B 整帧；UINT32 数组保证 4 B 对齐（hw_rxhdr 里全是 u32） */
static UINT32 sAicEapolFrame[(AIC_EAPOL_FRAME_MAX + 3u) / 4u];
/* 配置帧 CFM：WifiEapolConfigFrame.c；布局自检：WifiEapolLayout.c；shell：WifiEapolShellPoll.c */
/* PTK 已装进固件（WifiHandshakeInstall.c 置位）：解密帧的文案据此换挡 */
UINT32 gAicEapolKeysInstalled;

/* 25 MHz 时间基（BoardConfig.h BOARD_TIMEBASE_HZ），只用来算预算，不参与协议 */
#define AIC_EAPOL_TIMEBASE_PER_MS (BOARD_TIMEBASE_HZ / 1000u)

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
        AicEapolPrintConfigFrame(Frame, Length, Counters);
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
