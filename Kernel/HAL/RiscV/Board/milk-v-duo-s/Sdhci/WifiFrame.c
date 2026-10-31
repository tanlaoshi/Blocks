/*
 * WifiFrame.c — 收“一整帧”：变长消息（SCANU_RESULT_IND）的读取与有效性校验
 *
 * 从哪读：AicReceiveFrameTimeout（对外的唯一入口）。
 * 为什么单独一个文件：WifiMessage.c 已经 400 行，再塞进来就破 500 行硬线；
 * 而且这是“整帧收包”这一个职责，和 FIFO 写/CFM 收包分开更清楚。
 * 别改：ver/mmreset/bringup 仍走 WifiMessage.c 的 AicRecvMessageTimeout，行为不变。
 */
#include "WifiPrivate.h"

/*
 * 读一条完整的原始帧（不是只拷参数）。
 *
 * 为什么另起一个函数：SCANU_RESULT_IND 的 payload 在结构体之后变长，而
 * AicRecvMessageTimeout 的契约是「把 param 拷进定长缓冲、整帧最多 512 B」。
 * 把变长需求硬塞进去会改掉 ver/mmreset/bringup 已经验证过的行为，所以
 * 三件套继续走原函数，扫描结果走这里。
 */
static int AicFrameReadRaw(SDHCI_HOST *Host, UINT32 TimeoutMs, UINT8 *Frame, UINT32 MaxFrame,
                           UINT32 *OutLength) {
    UINT32 Fn = 0;
    UINT32 Length = 0;
    UINT32 ReadLength;
    UINT32 Done;

    if (AicWaitMessagePending(Host, TimeoutMs, &Fn, &Length) != 0) {
        return -1;
    }
    ReadLength = Length > MaxFrame ? MaxFrame : Length;
    /* 已知能工作的读法是一次 512 B（原收包路径也只读 512），这里按块循环 */
    Done = 0;
    while (Done < ReadLength) {
        UINT32 Chunk = ReadLength - Done;

        if (Chunk > AIC_FIFO_CHUNK) {
            Chunk = AIC_FIFO_CHUNK;
        }
        if (AicFifoRead(Host, Fn, Frame + Done, Chunk) != 0) {
            return -1;
        }
        Done += Chunk;
    }
    if (Length > ReadLength) {
        /* 超长帧：余下的读掉，否则会串到下一条消息 */
        UINT32 Left = Length - ReadLength;

        while (Left > 0u) {
            UINT32 Chunk = Left > AIC_FIFO_CHUNK ? AIC_FIFO_CHUNK : Left;

            if (AicFifoRead(Host, Fn, gAicRx, Chunk) != 0) {
                return -1;
            }
            Left -= Chunk;
        }
        SdhciMarkHex("WiFi: rx frame too big, read first ", ReadLength, 4);
        SdhciMarkHex(" of ", Length, 4);
        SdhciMark("\n");
    }
    *OutLength = ReadLength;
    return 0;
}

/* 帧头/有效性戳校验：不是 e2a 消息（或不是 CFG_CMD_RSP/CFG_DATA_CFM）就丢 */
static int AicFrameCheck(const UINT8 *Frame, UINT32 FrameLength, UINT32 *OutParamLength) {
    UINT32 Pattern;

    if (FrameLength < AIC_E2A_PARAM_OFF) {
        return -1;
    }
    Pattern = (UINT32)Frame[12] | ((UINT32)Frame[13] << 8) | ((UINT32)Frame[14] << 16) |
              ((UINT32)Frame[15] << 24);
    if (Pattern != AIC_E2A_PATTERN) {
        SdhciMarkHex("WiFi: rx bad pattern=", Pattern, 8);
        SdhciMark(" (want addede2a) drop\n");
        return -1;
    }
    if ((Frame[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_CMD_RSP &&
        (Frame[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_DATA_CFM) {
        return -1;
    }
    *OutParamLength = AicGet16(&Frame[10]);
    if (*OutParamLength > FrameLength - AIC_E2A_PARAM_OFF) {
        *OutParamLength = FrameLength - AIC_E2A_PARAM_OFF;
    }
    return 0;
}

/* 默认实现：什么也不做（x64 等无 WiFi 的架构也能编过） */
__attribute__((weak)) void AicFrameUnexpected(const UINT8 *Frame, UINT32 Length) {
    (void)Frame;
    (void)Length;
}

int AicReceiveFrameTimeout(SDHCI_HOST *Host, UINT8 *Frame, UINT32 MaxFrame, UINT16 *OutId,
                             UINT32 *OutFrameLength, UINT32 *OutParamLength, UINT32 Tries,
                             UINT32 TimeoutMs) {
    UINT32 T;

    for (T = 0; T < Tries; T++) {
        UINT32 FrameLength = 0;
        UINT32 ParamLength = 0;

        if (AicFrameReadRaw(Host, TimeoutMs, Frame, MaxFrame, &FrameLength) != 0) {
            return -1;
        }
        if (AicFrameCheck(Frame, FrameLength, &ParamLength) != 0) {
            /* 不是 e2a 消息：可能正是 AP 的 EAPOL 帧，交给钩子，别静默丢 */
            AicFrameUnexpected(Frame, FrameLength);
            continue;
        }
        if (OutId != 0) {
            *OutId = AicGet16(&Frame[4]);
        }
        if (OutFrameLength != 0) {
            *OutFrameLength = FrameLength;
        }
        if (OutParamLength != 0) {
            *OutParamLength = ParamLength;
        }
        return 0;
    }
    return -1;
}
