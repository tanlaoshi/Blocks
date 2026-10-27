/*
 * Frame.c — SMSC bulk TX / RX 拆帧（bd-pi-eth）
 */
#include "Ethernet/Private.h"

int BoardEthernetSendFrame(const UINT8 *Frame, UINTN FrameLength) {
    UINT32 CmdA;
    UINT32 CmdB;
    int Ret;

    if (!gSmscReady || Frame == 0 || FrameLength < 14u || FrameLength > 1514u) {
        return -1;
    }
    CmdA = (UINT32)FrameLength | TX_CMD_A_FIRST | TX_CMD_A_LAST;
    CmdB = (UINT32)FrameLength;
    gSmscTxBuf[0] = (UINT8)(CmdA);
    gSmscTxBuf[1] = (UINT8)(CmdA >> 8);
    gSmscTxBuf[2] = (UINT8)(CmdA >> 16);
    gSmscTxBuf[3] = (UINT8)(CmdA >> 24);
    gSmscTxBuf[4] = (UINT8)(CmdB);
    gSmscTxBuf[5] = (UINT8)(CmdB >> 8);
    gSmscTxBuf[6] = (UINT8)(CmdB >> 16);
    gSmscTxBuf[7] = (UINT8)(CmdB >> 24);
    {
        UINTN i;
        for (i = 0; i < FrameLength; i++) {
            gSmscTxBuf[8u + i] = Frame[i];
        }
    }
    SmscSelectDev();
    Ret = BoardDwc2Bulk(gSmscAddr, gSmscEpOut, 0, gSmscTxBuf, (int)(8u + FrameLength),
                        gSmscBulkMps, &gSmscTxToggle);
    if (Ret < 0) {
        return -1;
    }
    gSmscTxDone++;
    return 0;
}

void SmscHandleRx(const UINT8 *Buf, int Len) {
    int Off = 0;

    while (Off + 4 <= Len) {
        UINT32 Sts;
        UINT32 PktLen;
        const UINT8 *Frame;

        Sts = (UINT32)Buf[Off] | ((UINT32)Buf[Off + 1] << 8) |
              ((UINT32)Buf[Off + 2] << 16) | ((UINT32)Buf[Off + 3] << 24);
        Off += 4;
        if (Sts & RX_STS_ES) {
            break;
        }
        PktLen = (Sts & RX_STS_FL) >> 16;
        if (PktLen < 4u || Off + (int)PktLen > Len) {
            break;
        }
        Frame = Buf + Off;
        /* U-Boot：上报长度减 4（FCS） */
        {
            UINTN Fl = PktLen - 4u;
            if (Fl >= 14u) {
                gSmscRxFrames++;
#ifdef LWIP_ENABLE
                if (gSmscLwIpRx) {
                    LwIpNetifInput(Frame, Fl);
                }
#else
                (void)Frame;
#endif
            }
        }
        Off += (int)PktLen;
        if (Off & 3) {
            Off += 4 - (Off & 3);
        }
    }
}
