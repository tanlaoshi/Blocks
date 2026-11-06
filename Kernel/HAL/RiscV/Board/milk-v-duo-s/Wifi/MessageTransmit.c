/*
 * WifiMessageTransmit.c — AIC 消息 TX：流控、FIFO 写、SendMessage
 *
 * 自 WifiMessage.c 拆出（纯搬家）；行为不变。
 */
#include "Wifi/Private.h"

/*
 * 发消息前的流控（厂商 aicwf_sdio_flow_ctrl_msg，aicwf_sdio.c:101-139）：
 * 读 fn1 0x03（V3 的 FLOW_CTRL_Q1）拿到可用 buffer 数，为 0 就按厂商节奏重试
 * （<30 次 200 µs、<40 次 2 ms、其后 10 ms，共 50 轮），仍为 0 才失败。
 */
static int AicFlowCtrl(SDHCI_HOST *Host, UINT32 *OutFc) {
    UINT32 Count = 0;
    UINT8 Fc = 0;

    for (;;) {
        if (SdhciIoRead8(Host, 1u, AIC_FLOW_CTRL_REG_V3, &Fc) != 0) {
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
 * 一条消息的线上长度必须补齐到 512 的整数倍（厂商 aicwf_sdio.c:1230-1237）。
 */
static int AicFifoWriteMessage(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 *InOutLength) {
    UINT32 Length = *InOutLength;
    UINT32 Padded;
    UINT32 Blocks;
    UINT32 Fc = 0;
    UINT32 i;

    if ((Length % 4u) != 0u) {
        UINT32 Aligned = (Length + 3u) & ~3u;
        for (i = Length; i < Aligned; i++) {
            Buffer[i] = 0;
        }
        Length = Aligned;
    }
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
    if (Padded >= Fc * AIC_TX_BUFFER_SIZE) {
        SdhciMarkHex("WiFi: tx msg fc retry fail: fc=", Fc, 2);
        SdhciMarkHex(" len=", Padded, 4);
        SdhciMark("\n");
        return -1;
    }
    if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, (UINT16)Blocks, AIC_FIFO_CHUNK, Buffer) !=
        0) {
        AicMarkTransferParams(Host, "WiFi: tx block write FAIL", Padded, Blocks);
        (void)SdhciWifiBusRecover(Host);
        if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, 0, (UINT16)Padded, Buffer) != 0) {
            AicMarkTransferParams(Host, "WiFi: tx byte write FAIL", Padded, 0u);
            (void)SdhciWifiBusRecover(Host);
            return -1;
        }
        AicMarkTransferParams(Host, "WiFi: tx byte write OK (block refused)", Padded, 0u);
    }
    *InOutLength = Padded;
    return 0;
}

int AicSendMessageEx(SDHCI_HOST *Host, UINT16 Id, UINT16 DestId, const void *Param,
                     UINT16 ParamLength) {
    UINT32 Body = 8u + (UINT32)ParamLength;
    UINT32 Total = Body + 8u;
    UINT32 i;

    if (Total > AIC_MSG_MAX) {
        return -1;
    }
    for (i = 0; i < Total; i++) {
        gAicMessage[i] = 0;
    }
    AicPut16(&gAicMessage[0], (UINT16)((Body + 4u) & 0x0FFFu));
    gAicMessage[2] = (UINT8)AIC_TYPE_CFG_CMD_RSP;
    gAicMessage[3] = AicCrc8(&gAicMessage[0], 3u);
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

int AicSendMessage(SDHCI_HOST *Host, UINT16 Id, const void *Param, UINT16 ParamLength) {
    return AicSendMessageEx(Host, Id, (UINT16)AIC_TASK_DBG, Param, ParamLength);
}
