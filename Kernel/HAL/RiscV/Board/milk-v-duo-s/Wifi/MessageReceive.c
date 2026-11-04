/*
 * WifiMessageReceive.c — AIC 消息 RX：FIFO 读、status 解码、Recv
 *
 * 自 WifiMessage.c 拆出（纯搬家）；行为不变。
 */
#include "Wifi/Private.h"

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
 * status 解析：严格照 aicwf_sdio_hal_irqhandler 的 D80 分支（aicsdio.c:1530-1548）。
 * 前提：Status 不含 bit7（软中断标志）。
 */
static int AicMessageDecode(SDHCI_HOST *Host, UINT8 Status, UINT32 *OutFn, UINT32 *OutLength,
                            const char **OutMode) {
    UINT8 BmLength = 0;
    UINT32 Masked = (UINT32)Status | 0x08u;

    *OutMode = "block";
    if (Masked > 120u) {
        *OutFn = 2u;
        if (Masked == 127u) {
            if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &BmLength) != 0) {
                return -1;
            }
            *OutLength = (UINT32)BmLength * 4u;
            *OutMode = "byte";
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
            *OutMode = "byte";
        } else {
            *OutLength = (UINT32)(Status & 0x7Fu) * 512u;
        }
    }
    SdhciMarkHex("WiFi: rx masked=", Masked, 2);
    SdhciMarkHex(" status=", Status, 2);
    SdhciMarkHex(" fn=", *OutFn, 2);
    SdhciMarkHex(" len=", *OutLength, 4);
    SdhciMark(" mode=");
    SdhciMark(*OutMode);
    SdhciMark("\n");
    if (*OutLength == 0u) {
        *OutLength = 512u;
    }
    return 0;
}

int AicWaitMessagePending(SDHCI_HOST *Host, UINT32 TimeoutMs, UINT32 *OutFn, UINT32 *OutLength) {
    UINT32 Waited = 0;
    UINT8 Status = 0;
    const char *Mode = "block";

    (void)AicSdioWake(Host);
    while (Waited < TimeoutMs) {
        if (SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Status) != 0) {
            SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
            Waited += AIC_CFM_POLL_MS;
            continue;
        }
        if ((Status & (UINT8)AIC_MISC_OTHER_INTR) != 0u) {
            (void)AicSdioIntrAck(Host, Status);
            if (SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Status) != 0) {
                SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
                Waited += AIC_CFM_POLL_MS;
                continue;
            }
            Status = (UINT8)(Status & (UINT8)~AIC_MISC_OTHER_INTR);
        }
        if (Status == 0u) {
            SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
            Waited += AIC_CFM_POLL_MS;
            continue;
        }
        return AicMessageDecode(Host, Status, OutFn, OutLength, &Mode);
    }
    AicSdioStateDump(Host);
    return -1;
}

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
        SdhciMarkHex(" hdr=",
                     (UINT32)(gAicRx[0] | (gAicRx[1] << 8) | ((UINT32)gAicRx[2] << 16) |
                              ((UINT32)gAicRx[3] << 24)),
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

int AicRecvMessage(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                   UINT32 *OutParamLength) {
    return AicRecvMessageTimeout(Host, WantId, Param, MaxParam, OutParamLength, 4u,
                                 AIC_CFM_TIMEOUT_MS);
}
