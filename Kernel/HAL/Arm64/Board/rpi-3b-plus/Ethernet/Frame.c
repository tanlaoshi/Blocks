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
    gSmscTransmitBuffer[0] = (UINT8)(CmdA);
    gSmscTransmitBuffer[1] = (UINT8)(CmdA >> 8);
    gSmscTransmitBuffer[2] = (UINT8)(CmdA >> 16);
    gSmscTransmitBuffer[3] = (UINT8)(CmdA >> 24);
    gSmscTransmitBuffer[4] = (UINT8)(CmdB);
    gSmscTransmitBuffer[5] = (UINT8)(CmdB >> 8);
    gSmscTransmitBuffer[6] = (UINT8)(CmdB >> 16);
    gSmscTransmitBuffer[7] = (UINT8)(CmdB >> 24);
    {
        UINTN i;
        for (i = 0; i < FrameLength; i++) {
            gSmscTransmitBuffer[8u + i] = Frame[i];
        }
    }
    SmscSelectDevice();
    Ret = BoardUsbHostControllerBulk(gSmscAddress, gSmscEndpointOut, 0, gSmscTransmitBuffer, (int)(8u + FrameLength),
                        gSmscBulkMaxPacketSize, &gSmscTransmitToggle);
    if (Ret < 0) {
        return -1;
    }
    gSmscTransmitDone++;
    return 0;
}

void SmscHandleReceive(const UINT8 *Buffer, int Length) {
    int Offset = 0;

    while (Offset + 4 <= Length) {
        UINT32 Sts;
        UINT32 PktLen;
        const UINT8 *Frame;

        Sts = (UINT32)Buffer[Offset] | ((UINT32)Buffer[Offset + 1] << 8) |
              ((UINT32)Buffer[Offset + 2] << 16) | ((UINT32)Buffer[Offset + 3] << 24);
        Offset += 4;
        if (Sts & RX_STS_ES) {
            break;
        }
        PktLen = (Sts & RX_STS_FL) >> 16;
        if (PktLen < 4u || Offset + (int)PktLen > Length) {
            break;
        }
        Frame = Buffer + Offset;
        /* U-Boot：上报长度减 4（FCS） */
        {
            UINTN Fl = PktLen - 4u;
            if (Fl >= 14u) {
                gSmscReceiveFrames++;
#ifdef LWIP_ENABLE
                if (gSmscLwIpReceive) {
                    LwIpNetifInput(Frame, Fl);
                }
#else
                (void)Frame;
#endif
            }
        }
        Offset += (int)PktLen;
        if (Offset & 3) {
            Offset += 4 - (Offset & 3);
        }
    }
}
