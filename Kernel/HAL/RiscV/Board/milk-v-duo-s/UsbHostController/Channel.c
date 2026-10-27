/*
 * Channel.c — DWC2 host channel 启停与 chunk（bd-duo-usb）
 */
#include "UsbHostController/Private.h"

void HostChannelInitialize(UINT8 DeviceAddress, UINT8 EndpointNumber, int EndpointIn, UINT8 EndpointType,
                   UINT16 MaxPacketSize, int LowSpeed, int DoSplit) {
    UINT32 Hcchar;

    Hcchar = ((UINT32)DeviceAddress << DWC2_HCCHAR_DEVADDR_SHIFT) |
             ((UINT32)EndpointNumber << DWC2_HCCHAR_EPNUM_SHIFT) |
             ((UINT32)EndpointType << DWC2_HCCHAR_EPTYPE_SHIFT) |
             ((UINT32)MaxPacketSize << DWC2_HCCHAR_MPS_SHIFT);
    if (EndpointIn) {
        Hcchar |= DWC2_HCCHAR_EPDIR;
    }
    if (LowSpeed) {
        Hcchar |= DWC2_HCCHAR_LSPDDEV;
    }
    *Dwc2Register(DWC2_HCCHAR(DWC2_HC_CHANNEL)) = Hcchar;
    if (DoSplit) {
        /* Synopsys：控制/批量 SPLIT 的 XACTPOS=ALL；0(MID) 仅周期中间包 */
        *Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL)) =
            DWC2_HCSPLT_SPLTENA | DWC2_HCSPLT_XACTPOS_ALL |
            ((UINT32)gTransferHub << DWC2_HCSPLT_HUBADDR_SHIFT) |
            ((UINT32)gTransferPort << DWC2_HCSPLT_PRTADDR_SHIFT);
    } else {
        *Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL)) = 0;
    }
}

/*
 * 同 U-Boot wait_for_chhltd：仅 XFERCOMP 算成功。
 * SSPLIT 的 ACK 在 ChunkMessage 里收（真机屏曾见 HCINT=0x82=CHHLTD|XACTERR）。
 */
int WaitChannelHalted(UINT32 *BytesLeftOut, UINT8 *PacketIdOut) {
    UINT32 i;
    UINT32 Hcint;
    UINT32 Hctsiz;
    UINT32 Offset = DWC2_HCINT(DWC2_HC_CHANNEL);

    for (i = 0; i < 200000u; i++) {
        if ((*Dwc2Register(Offset) & DWC2_HCINT_CHHLTD) != 0) {
            break;
        }
        Dwc2Spin(50);
    }
    if ((*Dwc2Register(Offset) & DWC2_HCINT_CHHLTD) == 0) {
        return -1;
    }

    Hcint = *Dwc2Register(Offset);
    Hctsiz = *Dwc2Register(DWC2_HCTSIZ(DWC2_HC_CHANNEL));
    *BytesLeftOut = Hctsiz & 0x7FFFFu;
    *PacketIdOut = (UINT8)((Hctsiz >> DWC2_HCTSIZ_PID_SHIFT) & 3u);

    if ((Hcint & DWC2_HCINT_XFERCOMP) != 0) {
        return 0;
    }
    if ((Hcint & DWC2_HCINT_NYET) != 0) {
        return -3; /* CSPLIT NYET */
    }
    if ((Hcint & (DWC2_HCINT_NAK | DWC2_HCINT_FRMOVRUN)) != 0) {
        return -2;
    }
    /* ACK / XACTERR / STALL 等：交 ChunkMessage 看 HCINT（U-Boot 同形） */
    return -1;
}

int TransferChunk(UINT8 *PacketId, int EndpointIn, void *Buffer, UINT32 PacketCount,
                  UINT32 TransferLength, UINT32 *ActualLength, int OddFrame) {
    UINT32 Sub;
    int Ret;

    *Dwc2Register(DWC2_HCTSIZ(DWC2_HC_CHANNEL)) =
        (TransferLength << DWC2_HCTSIZ_XFERSIZE_SHIFT) |
        (PacketCount << DWC2_HCTSIZ_PKTCNT_SHIFT) |
        ((UINT32)(*PacketId) << DWC2_HCTSIZ_PID_SHIFT);

    if (TransferLength != 0) {
        if (!EndpointIn) {
            UINT32 i;
            for (i = 0; i < TransferLength && i < DWC2_DMA_BUF_SIZE; i++) {
                gDmaBuffer[i] = ((UINT8 *)Buffer)[i];
            }
            HalDcacheCleanInvalidateRange(gDmaBuffer, TransferLength);
        } else {
            HalDcacheCleanInvalidateRange(gDmaBuffer, TransferLength);
        }
    }

    *Dwc2Register(DWC2_HCDMA(DWC2_HC_CHANNEL)) = DWC2_PHYS_TO_BUS(gDmaBuffer);
    *Dwc2Register(DWC2_HCINT(DWC2_HC_CHANNEL)) = 0x3FFFu;

    {
        UINT32 Hcchar = *Dwc2Register(DWC2_HCCHAR(DWC2_HC_CHANNEL));
        Hcchar &= ~(DWC2_HCCHAR_CHEN | DWC2_HCCHAR_CHDIS | DWC2_HCCHAR_ODDFRM |
                    (3u << DWC2_HCCHAR_MULTICNT_SHIFT));
        Hcchar |= (1u << DWC2_HCCHAR_MULTICNT_SHIFT) | DWC2_HCCHAR_CHEN;
        if (OddFrame) {
            Hcchar |= DWC2_HCCHAR_ODDFRM;
        }
        *Dwc2Register(DWC2_HCCHAR(DWC2_HC_CHANNEL)) = Hcchar;
    }

    Ret = WaitChannelHalted(&Sub, PacketId);
    if (Ret < 0) {
        return Ret;
    }

    if (EndpointIn && TransferLength != 0) {
        UINT32 Got = TransferLength - Sub;
        UINT32 i;
        if (Got > TransferLength) {
            Got = TransferLength;
        }
        HalDcacheCleanInvalidateRange(gDmaBuffer, Got ? Got : 1u);
        for (i = 0; i < Got; i++) {
            ((UINT8 *)Buffer)[i] = gDmaBuffer[i];
        }
        *ActualLength = Got;
    } else {
        *ActualLength = TransferLength;
    }
    return 0;
}

/*
 * U-Boot chunk_msg + EDK2 DwUsbHost：每包 HostChannelInitialize；SSPLIT ACK→CSPLIT；
 * CSPLIT NYET 同帧内重试；XACTERR 整段重试（屏上曾见 HCINT=0x82）。
 */
