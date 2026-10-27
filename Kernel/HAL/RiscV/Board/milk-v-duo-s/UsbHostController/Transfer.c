/*
 * Transfer.c — ChunkMessage/控制传输（bd-duo-usb）
 */
#include "UsbHostController/Private.h"

int ChunkMessage(UINT8 DeviceAddress, UINT8 EndpointNumber, UINT8 EndpointType,
                 int EndpointIn, UINT8 *PacketId, void *Buffer, int Length,
                 UINT16 MaxPacketSize) {
    int BytesDone = 0;
    int CompleteSplit = 0;
    UINT32 StartSplitFrame = 0;
    int TransactionTries = 0;
    int NyetTries = 0;

    if (MaxPacketSize == 0) {
        MaxPacketSize = gTransferEndpoint0MaxPacketSize;
    }

    for (;;) {
        UINT32 TransferBytes;
        UINT32 PacketCount;
        UINT32 ActualLength = 0;
        int Result;
        int OddFrame = 0;
        UINT32 HostChannelInterrupt;
        UINT32 SplitControl;

        if (Length == 0) {
            TransferBytes = 0;
            PacketCount = 1;
        } else {
            TransferBytes = (UINT32)(Length - BytesDone);
            if (TransferBytes > MaxPacketSize) {
                TransferBytes = MaxPacketSize;
            }
            PacketCount = 1;
        }

        /* EDK2：每包重装 HCCHAR/HCSPLT（含 CSPLIT 切换） */
        HostChannelInitialize(DeviceAddress, EndpointNumber, EndpointIn, EndpointType,
                              MaxPacketSize, gTransferLowSpeed, gTransferDoSplit);
        if (gTransferDoSplit) {
            SplitControl = *Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL));
            if (CompleteSplit) {
                SplitControl |= DWC2_HCSPLT_COMPSPLT;
                /*
                 * USB2 §11.18：CSPLIT 须在 SSPLIT 之后的微帧。
                 * LS 经 TT 更敏感；同微帧立刻 CSPLIT → 真机 DATATGLERR(HCINT bit10)。
                 */
                {
                    UINT32 GuardCount = 0;
                    while (((*Dwc2Register(DWC2_HFNUM) & 0x3FFFu) == StartSplitFrame) &&
                           GuardCount < 20000u) {
                        Dwc2Spin(50);
                        GuardCount++;
                    }
                }
            } else {
                SplitControl &= ~DWC2_HCSPLT_COMPSPLT;
            }
            *Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL)) = SplitControl;
        }

        if (EndpointType == DWC2_HCCHAR_EPTYPE_INTR) {
            OddFrame = ((*Dwc2Register(DWC2_HFNUM) & 1u) == 0) ? 1 : 0;
        }

        Dwc2Spin(gTransferLowSpeed ? 16000u : 8000u); /* LS 多 settle */
        Result = TransferChunk(
            PacketId, EndpointIn,
            Length ? (void *)((UINT8 *)Buffer + BytesDone) : gStatusBuffer,
            PacketCount, TransferBytes, &ActualLength, OddFrame);
        HostChannelInterrupt = *Dwc2Register(DWC2_HCINT(DWC2_HC_CHANNEL));

        if (CompleteSplit) {
            if (Result == -3 ||
                (HostChannelInterrupt & DWC2_HCINT_NYET) != 0) {
                UINT32 FrameNumber = *Dwc2Register(DWC2_HFNUM) & 0x3FFFu;
                if (((FrameNumber - StartSplitFrame) & 0x3FFFu) > 4u) {
                    return -2;
                }
                if (++NyetTries > 3) {
                    CompleteSplit = 0;
                    NyetTries = 0;
                    continue; /* EDK2 RestartXfer */
                }
                Result = 0;
                ActualLength = 0;
            } else if (Result != 0) {
                /* fall through to error / xact retry */
            } else {
                CompleteSplit = 0;
                NyetTries = 0;
            }
        } else if (gTransferDoSplit &&
                   (HostChannelInterrupt & DWC2_HCINT_ACK) != 0) {
            StartSplitFrame = *Dwc2Register(DWC2_HFNUM) & 0x3FFFu;
            CompleteSplit = 1;
            NyetTries = 0;
            ActualLength = 0;
            Result = 0;
        }

        if (Result == -2) {
            return -2;
        }
        if (Result != 0) {
            /* XACTERR / DATATGLERR：整段从 SSPLIT 重来（LS getdesc18 真机见 0x402） */
            if (gTransferDoSplit &&
                (HostChannelInterrupt &
                 (DWC2_HCINT_XACTERR | DWC2_HCINT_DATATGLERR)) != 0 &&
                TransactionTries < 3) {
                TransactionTries++;
                CompleteSplit = 0;
                NyetTries = 0;
                Dwc2DelayMilliseconds(2);
                continue;
            }
            /*
             * 枚举期打一次 HCINT 便于对表；已绑 HID 后 Poll 掉线常见 HCINT=0，
             * 勿每轮 BootMark（带电拔键真机刷屏）。
             */
            if (!gMouseOk && !gKeyboardOk) {
                HalSerialBootMark("Boot: DWC2 HCINT=");
                Dwc2Hex32(HostChannelInterrupt);
                HalSerialBootMark(" spl=");
                Dwc2Hex32(*Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL)));
                HalSerialBootMark(" hub=");
                Dwc2Hex16(gTransferHub);
                HalSerialBootMark("+");
                Dwc2Hex16(gTransferPort);
                HalSerialBootMark("\n");
            }
            return Result;
        }
        TransactionTries = 0;
        BytesDone += (int)ActualLength;
        if (CompleteSplit) {
            continue;
        }
        if (Length == 0) {
            break;
        }
        if (ActualLength < TransferBytes) {
            break;
        }
        if (BytesDone >= Length) {
            break;
        }
        /* LS SPLIT：包与包之间跨微帧，减轻第 2 包起 DATATGLERR（cfg short=0x0b） */
        if (gTransferDoSplit && gTransferLowSpeed) {
            UINT32 FrameZero = *Dwc2Register(DWC2_HFNUM) & 0x3FFFu;
            UINT32 GuardCount = 0;
            while ((((*Dwc2Register(DWC2_HFNUM) & 0x3FFFu) - FrameZero) &
                    0x3FFFu) < 2u &&
                   GuardCount < 40000u) {
                Dwc2Spin(50);
                GuardCount++;
            }
        }
    }
    *Dwc2Register(DWC2_HCINTMSK(DWC2_HC_CHANNEL)) = 0;
    *Dwc2Register(DWC2_HCINT(DWC2_HC_CHANNEL)) = 0xFFFFFFFFu;
    return BytesDone;
}

void TransferTargetHub(void) {
    gTransferDoSplit = 0;
    gTransferLowSpeed = 0;
    gTransferHub = 0;
    gTransferPort = 0;
    gTransferEndpoint0MaxPacketSize = gEndpoint0MaxPacketSize;
}

void TransferTargetSplit(UINT8 Hub, UINT8 Port, int Low, UINT16 Endpoint0MaxPacketSize) {
    /* SPLIT 仅 HS 根口后的 hub 下游；Hub==0 为根设备直连 */
    gTransferDoSplit = (Hub != 0 && gRootSpeed == DWC2_HPRT0_PRTSPD_HS) ? 1 : 0;
    gTransferLowSpeed = Low;
    gTransferHub = Hub;
    gTransferPort = Port;
    gTransferEndpoint0MaxPacketSize = Endpoint0MaxPacketSize ? Endpoint0MaxPacketSize : (Low ? 8u : 64u);
}

int ControlMessage(UINT8 DeviceAddress, UsbSetup *Setup, void *Data, int Length) {
    UINT8 PacketId;
    int Result;
    int ActualLength = 0;
    int StatusEndpointIn;
    int Attempt;
    UINT16 MaxPacketSize = gTransferEndpoint0MaxPacketSize;

    for (Attempt = 0; Attempt < (int)DWC2_CTRL_RETRY; Attempt++) {
        UINT32 Index;
        PacketId = DWC2_HC_PID_SETUP;
        for (Index = 0; Index < 8; Index++) {
            gSetupBuffer[Index] = ((UINT8 *)Setup)[Index];
        }
        Result = ChunkMessage(DeviceAddress, 0, DWC2_HCCHAR_EPTYPE_CTRL, 0,
                              &PacketId, gSetupBuffer, 8, MaxPacketSize);
        if (Result == -2) {
            continue;
        }
        if (Result < 0) {
            return Result;
        }
        break;
    }
    if (Attempt >= (int)DWC2_CTRL_RETRY) {
        return -1;
    }

    if (Data != 0 && Length > 0) {
        int EndpointIn = (Setup->BmRequestType & 0x80u) != 0;
        for (Attempt = 0; Attempt < (int)DWC2_CTRL_RETRY; Attempt++) {
            PacketId = DWC2_HC_PID_DATA1;
            Result = ChunkMessage(DeviceAddress, 0, DWC2_HCCHAR_EPTYPE_CTRL,
                                  EndpointIn, &PacketId, Data, Length,
                                  MaxPacketSize);
            if (Result == -2) {
                continue;
            }
            if (Result < 0) {
                return Result;
            }
            ActualLength = Result;
            break;
        }
        if (Attempt >= (int)DWC2_CTRL_RETRY) {
            return -1;
        }
        StatusEndpointIn = EndpointIn ? 0 : 1;
    } else {
        StatusEndpointIn = 1;
    }

    for (Attempt = 0; Attempt < (int)DWC2_CTRL_RETRY; Attempt++) {
        PacketId = DWC2_HC_PID_DATA1;
        Result = ChunkMessage(DeviceAddress, 0, DWC2_HCCHAR_EPTYPE_CTRL,
                              StatusEndpointIn, &PacketId, gStatusBuffer, 0,
                              MaxPacketSize);
        if (Result == -2) {
            continue;
        }
        if (Result < 0) {
            return Result;
        }
        break;
    }
    if (Attempt >= (int)DWC2_CTRL_RETRY) {
        return -1;
    }
    return ActualLength;
}

