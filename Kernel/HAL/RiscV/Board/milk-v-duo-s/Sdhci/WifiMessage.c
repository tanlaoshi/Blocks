/*
 * WifiMessage.c — AIC8800 消息通道：FIFO 读写、帧头/crc8、收发与轮询
 *
 * 从 WifiFw.c 原样搬出（纯搬家，函数体逐字节不动），行为零变化。
 * host→device: hdr4 + dummy4 + lmac_msg(id/dest/src/plen) + param
 * device→host: hdr4 + e2a(id/dest + src/plen + pattern) + param
 */
#include "WifiPrivate.h"

/*
 * crc8，多项式 X8+X2+X+1（0x107），初值 0 —— 照抄 aicsdio.c:2004-2027。
 * PR-34 起不再 static：数据 TX 的 4 B 帧头也用同一个 CRC（aicwf_sdio.c:1540-1546），
 * 两处必须逐位一致，所以只留这一份实现。
 */
UINT8 AicCrc8(const UINT8 *Buffer, UINT32 Size) {
    UINT8 Crc = 0;
    UINT32 i;
    UINT8 Bit;

    for (i = 0; i < Size; i++) {
        for (Bit = 0x80u; Bit != 0u; Bit = (UINT8)(Bit / 2u)) {
            if ((Crc & 0x80u) != 0) {
                Crc = (UINT8)(Crc * 2u);
                Crc ^= 0x07u;
            } else {
                Crc = (UINT8)(Crc * 2u);
            }
            if ((Buffer[i] & Bit) != 0u) {
                Crc ^= 0x07u;
            }
        }
    }
    return Crc;
}

void AicPut16(UINT8 *At, UINT16 Value) {
    At[0] = (UINT8)(Value & 0xFFu);
    At[1] = (UINT8)((Value >> 8) & 0xFFu);
}

UINT16 AicGet16(const UINT8 *At) {
    return (UINT16)((UINT16)At[0] | ((UINT16)At[1] << 8));
}


/*
 * 发消息前的流控（厂商 aicwf_sdio_flow_ctrl_msg，aicwf_sdio.c:101-139）：
 * 读 fn1 0x03（V3 的 FLOW_CTRL_Q1）拿到可用 buffer 数，为 0 就按厂商节奏重试
 * （<30 次 200 µs、<40 次 2 ms、其后 10 ms，共 50 轮），仍为 0 才失败。
 * 没有 credit 就发送 = 芯片 FIFO 没准备好，写进去也是白写。
 */
static int AicFlowCtrl(SDHCI_HOST *Host, UINT32 *OutFc) {
    UINT32 Count = 0;
    UINT8 Fc = 0;

    for (;;) {
        if (SdhciIoRead8(Host, 1u, AIC_FLOW_CTRL_REG_V3, &Fc) != 0) {
            /* 读失败与"读到 0"必须分开：一个是 CMD52 不通，一个是芯片没给 credit */
            AicMarkR5(Host->LastStatus);
            SdhciMark("WiFi: tx fc read failed (cmd52 fn1 0x03)\n");
            return -1;
        }
        if (Fc != 0u) {
            *OutFc = Fc;
            return 0;
        }
        if (Count >= AIC_FC_RETRY_COUNT) {
            *OutFc = 0;
            return -1;
        }
        Count++;
        if (Count < 30u) {
            SdhciDelayMicroseconds(200);
        } else if (Count < 40u) {
            SdhciDelayMilliseconds(2);
        } else {
            SdhciDelayMilliseconds(10);
        }
    }
}

/*
 * 一条消息的线上长度必须补齐到 512 的整数倍（厂商 aicwf_sdio.c:1230-1237）：
 * 先按 4 B 对齐，再补 TAIL_LEN(4) 个 0 并向上取整到 512 —— 不足一个块时整块补零。
 * 补齐后用**块模式**写（不掺字节模式），块数 = 补齐长度 / 512。
 */
static int AicFifoWriteMessage(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 *InOutLength) {
    UINT32 Length = *InOutLength;
    UINT32 Padded;
    UINT32 Blocks;
    UINT32 Fc = 0;
    UINT32 i;

    /* 4 B 对齐 */
    if ((Length % 4u) != 0u) {
        UINT32 Aligned = (Length + 3u) & ~3u;
        for (i = Length; i < Aligned; i++) {
            Buffer[i] = 0;
        }
        Length = Aligned;
    }
    /* 先算补齐长度并检查上限，再落笔填 0 —— 免得算出来超界了还在写缓冲 */
    if ((Length % AIC_FIFO_CHUNK) != 0u) {
        Padded = ((Length + 4u) / AIC_FIFO_CHUNK + 1u) * AIC_FIFO_CHUNK;
    } else {
        Padded = Length;
    }
    if (Padded > AIC_MSG_MAX) {
        SdhciMark("WiFi: tx msg too long for buffer\n");
        return -1;
    }
    for (i = Length; i < Padded; i++) {
        Buffer[i] = 0;
    }
    Blocks = Padded / AIC_FIFO_CHUNK;

    if (AicFlowCtrl(Host, &Fc) != 0) {
        SdhciMark("WiFi: tx msg fc retry fail: fc=");
        SdhciMarkHex("", Fc, 2);
        SdhciMarkHex(" len=", Padded, 4);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkHex("WiFi: tx fc=", Fc, 2);
    SdhciMarkHex(" padded=", Padded, 4);
    SdhciMarkHex(" blocks=", Blocks, 2);
    SdhciMark("\n");
    /* 厂商的判据：len < fc * 1536 才发 */
    if (Padded >= Fc * AIC_TX_BUFFER_SIZE) {
        SdhciMarkHex("WiFi: tx msg fc retry fail: fc=", Fc, 2);
        SdhciMarkHex(" len=", Padded, 4);
        SdhciMark("\n");
        return -1;
    }
    if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, (UINT16)Blocks, AIC_FIFO_CHUNK, Buffer) !=
        0) {
        AicMarkXferParams(Host, "WiFi: tx block write FAIL", Padded, Blocks);
        (void)SdhciWifiBusRecover(Host);
        /* 卡若 SMB=0 就不吃多块；按同样长度退回字节模式再试一次，
           两次参数都打出来 —— "块失败/字节成功"说明是模式问题而不是门没开 */
        if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, 0, (UINT16)Padded, Buffer) != 0) {
            AicMarkXferParams(Host, "WiFi: tx byte write FAIL", Padded, 0u);
            (void)SdhciWifiBusRecover(Host);
            return -1;
        }
        AicMarkXferParams(Host, "WiFi: tx byte write OK (block refused)", Padded, 0u);
    }
    *InOutLength = Padded;
    return 0;
}


int AicSendMessageEx(SDHCI_HOST *Host, UINT16 Id, UINT16 DestId, const void *Param,
                        UINT16 ParamLength) {
    UINT32 Body = 8u + (UINT32)ParamLength; /* lmac_msg 头 + 参数 */
    UINT32 Total = Body + 8u;            /* 再加 4 B 帧头 + 4 B dummy */
    UINT32 i;

    if (Total > AIC_MSG_MAX) {
        return -1;
    }
    for (i = 0; i < Total; i++) {
        gAicMessage[i] = 0;
    }
    /* 帧头：len 字段 = 总长 - 4（aic_bsp_driver.c:262-266） */
    AicPut16(&gAicMessage[0], (UINT16)((Body + 4u) & 0x0FFFu));
    gAicMessage[2] = (UINT8)AIC_TYPE_CFG_CMD_RSP;
    gAicMessage[3] = AicCrc8(&gAicMessage[0], 3u);
    /* gAicMessage[4..7]：厂商的 dummy word，保持 0 */
    AicPut16(&gAicMessage[8], Id);
    AicPut16(&gAicMessage[10], DestId);
    AicPut16(&gAicMessage[12], (UINT16)AIC_DRV_TASK_ID);
    AicPut16(&gAicMessage[14], ParamLength);
    for (i = 0; i < (UINT32)ParamLength; i++) {
        gAicMessage[16u + i] = ((const UINT8 *)Param)[i];
    }
    {
        UINT32 Wire = Total;
        if (AicFifoWriteMessage(Host, gAicMessage, &Wire) != 0) {
            SdhciMarkHex("WiFi: aic fifo write fail at ", Total, 4);
            SdhciMark("\n");
            return -1;
        }
    }
    return 0;
}

/*
 * 从 fn2 0x0F 收一条消息（厂商消息通道走 func_msg = fn2，aicsdio.c:1530-1545）。
 * 字节模式读取同样要求 fn1 0x07 的允许位已置，所以读之前确认一次。
 */
int AicFifoRead(SDHCI_HOST *Host, UINT32 Fn, UINT8 *Data, UINT32 Length) {
    UINT8 Bm = 0;

    if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, &Bm) == 0 && (Bm & 0x01u) == 0u) {
        (void)SdhciIoWrite8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, 0x01u);
    }
    if (SdhciIoRwExtended(Host, Fn, AIC_RD_FIFO_REG, 0, 0, 0, (UINT16)Length, Data) != 0) {
        AicMarkXferParams(Host, "WiFi: rx read FAIL", Length, 0u);
        return -1;
    }
    return 0;
}

/*
 * 芯片有没有消息 —— **严格**照 aicwf_sdio_hal_irqhandler 的 D80 分支
 * （aicsdio.c:1505-1545），三分支一个不漏：
 *
 *   intmaskf2 = status | 0x08
 *   intmaskf2 > 120  → **fn2**（msg 通道）事件：==127 字节模式 len=blen*4，
 *                      否则块模式 len=(status & 0x07)*512
 *   其余             → **fn1**（data 通道）事件：==0x78 字节模式 len=blen*4，
 *                      否则块模式 len=(status & 0x7F)*512
 *   status == 0      → 没消息，继续等
 *
 * 真机 10:13 的 `status=01` 就落在 **fn1 块模式 1 块 = 512 B** 这一支：
 * 芯片早就回了 512 B 在等我们读，是旧判据（只认 >120）把它判成"没消息"。
 */
int AicWaitMessagePending(SDHCI_HOST *Host, UINT32 TimeoutMs, UINT32 *OutFn, UINT32 *OutLength) {
    UINT32 Waited = 0;
    UINT8 Status = 0;
    UINT32 Masked;
    UINT8 BmLength = 0;
    const char *Mode = "block";

    while (Waited < TimeoutMs) {
        if (SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Status) != 0) {
            SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
            Waited += AIC_CFM_POLL_MS;
            continue;
        }
        if (Status == 0u) {
            SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
            Waited += AIC_CFM_POLL_MS;
            continue;
        }
        Masked = (UINT32)Status | 0x08u;
        Mode = "block";
        if (Masked > 120u) {
            *OutFn = 2u;
            if (Masked == 127u) {
                if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &BmLength) != 0) {
                    return -1;
                }
                *OutLength = (UINT32)BmLength * 4u;
                Mode = "byte";
            } else {
                *OutLength = (UINT32)(Status & 0x07u) * 512u;
            }
        } else {
            *OutFn = 1u;
            if (Status == 0x78u) {
                if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &BmLength) != 0) {
                    return -1;
                }
                *OutLength = (UINT32)BmLength * 4u;
                Mode = "byte";
            } else {
                *OutLength = (UINT32)(Status & 0x7Fu) * 512u;
            }
        }
        SdhciMarkHex("WiFi: rx masked=", Masked, 2);
        SdhciMarkHex(" status=", Status, 2);
        SdhciMarkHex(" fn=", *OutFn, 2);
        SdhciMarkHex(" len=", *OutLength, 4);
        SdhciMark(" mode=");
        SdhciMark(Mode);
        SdhciMark("\n");
        if (*OutLength == 0u) {
            *OutLength = 512u; /* 长度算成 0 就按一块读，宁可多读也不空转 */
        }
        return 0;
    }
    {
        UINT8 Pend = 0;
        UINT8 Ctrl = 0;
        UINT8 Blen = 0;
        UINT8 IoEx = 0;
        (void)SdhciIoRead8(Host, 1u, AIC_INTR_PENDING_REG, &Pend);
        (void)SdhciIoRead8(Host, 1u, AIC_MISC_CTRL_REG, &Ctrl);
        (void)SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &Blen);
        (void)SdhciIoRead8(Host, 0u, 0x02u, &IoEx);
        SdhciMarkHex("WiFi: rx poll timeout status=", Status, 2);
        SdhciMarkHex(" pend=", Pend, 2);
        SdhciMarkHex(" ctrl=", Ctrl, 2);
        SdhciMarkHex(" blen=", Blen, 2);
        SdhciMarkHex(" ioex=", IoEx, 2);
        SdhciMark("\n");
    }
    return -1;
}

/* 前 32 字节 hexdump：和厂商 0x400 请求逐字节比对手里就靠它 */
void AicDumpBytes(const UINT8 *Data, UINT32 Length) {
    UINT32 i;

    if (Length > 32u) {
        Length = 32u;
    }
    SdhciMark("WiFi: rx dump");
    for (i = 0; i < Length; i++) {
        SdhciMarkHex(" ", Data[i], 2);
    }
    SdhciMark("\n");
}

/*
 * 收一条确认。RX 帧带 4 B 帧头（hdr[2]&0x7f == 0x11 CFG_CMD_RSP / 0x12 CFG_DATA_CFM，
 * aicsdio_txrxif.c:211-215），正文 ipc_e2a_msg 在 **+4**：
 *   +4 id / +6 dest / +8 src / +10 param_len / +12 参数
 */
int AicRecvMessageTimeout(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                             UINT32 *OutParamLength, UINT32 Tries, UINT32 TimeoutMs) {
    UINT32 Length = 0;
    UINT32 Fn = 0;
    UINT32 ParamLength;
    UINT32 i;
    UINT32 T;

    for (T = 0; T < Tries; T++) {
        if (AicWaitMessagePending(Host, TimeoutMs, &Fn, &Length) != 0) {
            SdhciMark("WiFi: aic rx: no pending message (fn1 0x04)\n");
            return -1;
        }
        if (Length > 512u) {
            SdhciMarkHex("WiFi: rx len>512, read first block only, len=", Length, 4);
            SdhciMark("\n");
            Length = 512u;
        }
        if (AicFifoRead(Host, Fn, gAicRx, Length) != 0) {
            return -1;
        }
        SdhciMarkHex("WiFi: rx len=", Length, 4);
        SdhciMarkHex(" hdr=", (UINT32)(gAicRx[0] | (gAicRx[1] << 8) |
                                       ((UINT32)gAicRx[2] << 16) | ((UINT32)gAicRx[3] << 24)),
                     8);
        SdhciMarkHex(" type=", (UINT32)(gAicRx[2] & 0x7Fu), 2);
        SdhciMarkHex(" id=", AicGet16(&gAicRx[4]), 4);
        SdhciMarkHex(" plen=", AicGet16(&gAicRx[10]), 4);
        SdhciMarkHex(" pattern=",
                     (UINT32)(gAicRx[12] | (gAicRx[13] << 8) | ((UINT32)gAicRx[14] << 16) |
                              ((UINT32)gAicRx[15] << 24)),
                     8);
        SdhciMark("\n");
        AicDumpBytes(gAicRx, Length);
        /* 硬判据：不是 e2a 有效性戳就丢帧（评审：比 type 判断可靠） */
        {
            UINT32 Pat = (UINT32)(gAicRx[12] | (gAicRx[13] << 8) | ((UINT32)gAicRx[14] << 16) |
                                  ((UINT32)gAicRx[15] << 24));
            if (Pat != AIC_E2A_PATTERN) {
                SdhciMarkHex("WiFi: rx bad pattern=", Pat, 8);
                SdhciMark(" (want addede2a) drop\n");
                continue;
            }
        }
        SdhciMark("WiFi: rx pattern=addede2a ok\n");
        if ((gAicRx[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_CMD_RSP &&
            (gAicRx[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_DATA_CFM) {
            SdhciMarkHex("WiFi: rx skip type=", gAicRx[2], 2);
            SdhciMark("\n");
            continue;
        }
        if (AicGet16(&gAicRx[4]) != WantId) {
            SdhciMarkHex("WiFi: rx id=", AicGet16(&gAicRx[4]), 4);
            SdhciMarkHex(" want=", WantId, 4);
            SdhciMark("\n");
            continue;
        }
        ParamLength = AicGet16(&gAicRx[10]);
        if (ParamLength > MaxParam) {
            ParamLength = MaxParam;
        }
        /* params 从 +16 起：可拷长度 = Length - 16（不是 Length - 12） */
        if (Length < AIC_E2A_PARAM_OFF) {
            SdhciMarkHex("WiFi: rx frame too short len=", Length, 4);
            SdhciMark("\n");
            continue;
        }
        if (ParamLength > Length - AIC_E2A_PARAM_OFF) {
            ParamLength = Length - AIC_E2A_PARAM_OFF;
        }
        if (OutParamLength != 0) {
            *OutParamLength = ParamLength;
        }
        if (Param != 0 && ParamLength > 0u) {
            for (i = 0; i < ParamLength; i++) {
                Param[i] = gAicRx[AIC_E2A_PARAM_OFF + i];
            }
        }
        return 0;
    }
    return -1;
}

/* 常用写法：4 次 × 500 ms = 2 s 预算 */
int AicRecvMessage(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                      UINT32 *OutParamLength) {
    return AicRecvMessageTimeout(Host, WantId, Param, MaxParam, OutParamLength, 4u, AIC_CFM_TIMEOUT_MS);
}

/* 固定 dest=TASK_DBG 的常用写法（DBG 系列消息） */
int AicSendMessage(SDHCI_HOST *Host, UINT16 Id, const void *Param, UINT16 ParamLength) {
    return AicSendMessageEx(Host, Id, (UINT16)AIC_TASK_DBG, Param, ParamLength);
}
