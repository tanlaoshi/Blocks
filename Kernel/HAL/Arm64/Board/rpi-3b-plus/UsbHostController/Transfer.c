/*
 * Transfer.c — ChunkMessage/控制/Bulk 门面（bd-pi-usb）
 */
#include "UsbHostController/Private.h"

int ChunkMessage(UINT8 DeviceAddress, UINT8 EndpointNumber, UINT8 EndpointType, int In,
                    UINT8 *PacketId, void *Buffer, int Length, UINT16 MaxPacketSize) {
    int Done = 0;
    int CompleteSplit = 0;
    UINT32 SsplitFrame = 0;
    int XactTries = 0;
    int NyetTries = 0;

    if (MaxPacketSize == 0) {
        MaxPacketSize = gTransferEndpoint0MaxPacketSize;
    }

    for (;;) {
        UINT32 Xfer;
        UINT32 Npkts;
        UINT32 Actual = 0;
        int Ret;
        int Odd = 0;
        UINT32 Hcint;
        UINT32 Spl;

        if (Length == 0) {
            Xfer = 0;
            Npkts = 1;
        } else {
            Xfer = (UINT32)(Length - Done);
            if (Xfer > MaxPacketSize) {
                Xfer = MaxPacketSize;
            }
            Npkts = 1;
        }

        /* EDK2：每包重装 HCCHAR/HCSPLT（含 CSPLIT 切换） */
        HostChannelInitialize(DeviceAddress, EndpointNumber, In, EndpointType, MaxPacketSize, gTransferLowSpeed, gTransferDoSplit);
        if (gTransferDoSplit) {
            Spl = *Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL));
            if (CompleteSplit) {
                Spl |= DWC2_HCSPLT_COMPSPLT;
                /*
                 * USB2 §11.18：CSPLIT 须在 SSPLIT 之后的微帧。
                 * LS 经 TT 更敏感；同微帧立刻 CSPLIT → 真机 DATATGLERR(HCINT bit10)。
                 */
                {
                    UINT32 Guard = 0;
                    while (((*Dwc2Register(DWC2_HFNUM) & 0x3FFFu) == SsplitFrame) &&
                           Guard < 20000u) {
                        Dwc2Spin(50);
                        Guard++;
                    }
                }
            } else {
                Spl &= ~DWC2_HCSPLT_COMPSPLT;
            }
            *Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL)) = Spl;
        }

        if (EndpointType == DWC2_HCCHAR_EPTYPE_INTR) {
            Odd = ((*Dwc2Register(DWC2_HFNUM) & 1u) == 0) ? 1 : 0;
        }

        Dwc2Spin(gTransferLowSpeed ? 16000u : 8000u); /* LS 多 settle */
        Ret = TransferChunk(PacketId, In,
                            Length ? (void *)((UINT8 *)Buffer + Done) : gStatusBuffer,
                            Npkts, Xfer, &Actual, Odd);
        Hcint = *Dwc2Register(DWC2_HCINT(DWC2_HC_CHANNEL));

        if (CompleteSplit) {
            if (Ret == -3 || (Hcint & DWC2_HCINT_NYET) != 0) {
                UINT32 Fn = *Dwc2Register(DWC2_HFNUM) & 0x3FFFu;
                if (((Fn - SsplitFrame) & 0x3FFFu) > 4u) {
                    return -2;
                }
                if (++NyetTries > 3) {
                    CompleteSplit = 0;
                    NyetTries = 0;
                    continue; /* EDK2 RestartXfer */
                }
                Ret = 0;
                Actual = 0;
            } else if (Ret != 0) {
                /* fall through to error / xact retry */
            } else {
                CompleteSplit = 0;
                NyetTries = 0;
            }
        } else if (gTransferDoSplit && (Hcint & DWC2_HCINT_ACK) != 0) {
            SsplitFrame = *Dwc2Register(DWC2_HFNUM) & 0x3FFFu;
            CompleteSplit = 1;
            NyetTries = 0;
            Actual = 0;
            Ret = 0;
        }

        if (Ret == -2) {
            return -2;
        }
        if (Ret != 0) {
            /* XACTERR / DATATGLERR：整段从 SSPLIT 重来（LS getdesc18 真机见 0x402） */
            if (gTransferDoSplit &&
                (Hcint & (DWC2_HCINT_XACTERR | DWC2_HCINT_DATATGLERR)) != 0 &&
                XactTries < 3) {
                XactTries++;
                CompleteSplit = 0;
                NyetTries = 0;
                Dwc2DelayMilliseconds(2);
                continue;
            }
            HalSerialBootMark("Boot: DWC2 HCINT=");
            Dwc2Hex32(Hcint);
            HalSerialBootMark(" spl=");
            Dwc2Hex32(*Dwc2Register(DWC2_HCSPLT(DWC2_HC_CHANNEL)));
            HalSerialBootMark(" hub=");
            Dwc2Hex16(gTransferHub);
            HalSerialBootMark("+");
            Dwc2Hex16(gTransferPort);
            HalSerialBootMark("\n");
            return Ret;
        }
        XactTries = 0;
        Done += (int)Actual;
        if (CompleteSplit) {
            continue;
        }
        if (Length == 0) {
            break;
        }
        if (Actual < Xfer) {
            break;
        }
        if (Done >= Length) {
            break;
        }
        /* LS SPLIT：包与包之间跨微帧，减轻第 2 包起 DATATGLERR（cfg short=0x0b） */
        if (gTransferDoSplit && gTransferLowSpeed) {
            UINT32 F0 = *Dwc2Register(DWC2_HFNUM) & 0x3FFFu;
            UINT32 Guard = 0;
            while ((((*Dwc2Register(DWC2_HFNUM) & 0x3FFFu) - F0) & 0x3FFFu) < 2u &&
                   Guard < 40000u) {
                Dwc2Spin(50);
                Guard++;
            }
        }
    }
    *Dwc2Register(DWC2_HCINTMSK(DWC2_HC_CHANNEL)) = 0;
    *Dwc2Register(DWC2_HCINT(DWC2_HC_CHANNEL)) = 0xFFFFFFFFu;
    return Done;
}

void TransferTargetHub(void) {
    gTransferDoSplit = 0;
    gTransferLowSpeed = 0;
    gTransferHub = 0;
    gTransferPort = 0;
    gTransferEndpoint0MaxPacketSize = gEndpoint0MaxPacketSize;
}

void TransferTargetSplit(UINT8 Hub, UINT8 Port, int Low, UINT16 Endpoint0MaxPacketSize) {
    gTransferDoSplit = (gRootSpeed == DWC2_HPRT0_PRTSPD_HS) ? 1 : 0;
    gTransferLowSpeed = Low;
    gTransferHub = Hub;
    gTransferPort = Port;
    gTransferEndpoint0MaxPacketSize = Endpoint0MaxPacketSize ? Endpoint0MaxPacketSize : (Low ? 8u : 64u);
}

/* HS 设备在 HS hub 后：无 TT/SPLIT */
void TransferTargetHighSpeed(UINT8 Hub, UINT8 Port, UINT16 Endpoint0MaxPacketSize) {
    gTransferDoSplit = 0;
    gTransferLowSpeed = 0;
    gTransferHub = Hub;
    gTransferPort = Port;
    gTransferEndpoint0MaxPacketSize = Endpoint0MaxPacketSize ? Endpoint0MaxPacketSize : 64u;
}

int ControlMessage(UINT8 DeviceAddress, UsbSetup *Setup, void *Data, int Length) {
    UINT8 PacketId;
    int Ret;
    int Act = 0;
    int StatusIn;
    int Try;
    UINT16 MaxPacketSize = gTransferEndpoint0MaxPacketSize;

    for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
        UINT32 i;
        PacketId = DWC2_HC_PID_SETUP;
        for (i = 0; i < 8; i++) {
            gSetupBuffer[i] = ((UINT8 *)Setup)[i];
        }
        Ret = ChunkMessage(DeviceAddress, 0, DWC2_HCCHAR_EPTYPE_CTRL, 0, &PacketId, gSetupBuffer,
                       8, MaxPacketSize);
        if (Ret == -2) {
            continue;
        }
        if (Ret < 0) {
            return Ret;
        }
        break;
    }
    if (Try >= (int)DWC2_CTRL_RETRY) {
        return -1;
    }

    if (Data != 0 && Length > 0) {
        int In = (Setup->BmRequestType & 0x80u) != 0;
        for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
            PacketId = DWC2_HC_PID_DATA1;
            Ret = ChunkMessage(DeviceAddress, 0, DWC2_HCCHAR_EPTYPE_CTRL, In, &PacketId, Data,
                           Length, MaxPacketSize);
            if (Ret == -2) {
                continue;
            }
            if (Ret < 0) {
                return Ret;
            }
            Act = Ret;
            break;
        }
        if (Try >= (int)DWC2_CTRL_RETRY) {
            return -1;
        }
        StatusIn = In ? 0 : 1;
    } else {
        StatusIn = 1;
    }

    for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
        PacketId = DWC2_HC_PID_DATA1;
        Ret = ChunkMessage(DeviceAddress, 0, DWC2_HCCHAR_EPTYPE_CTRL, StatusIn, &PacketId,
                       gStatusBuffer, 0, MaxPacketSize);
        if (Ret == -2) {
            continue;
        }
        if (Ret < 0) {
            return Ret;
        }
        break;
    }
    if (Try >= (int)DWC2_CTRL_RETRY) {
        return -1;
    }
    return Act;
}

void BoardDwc2SelectDevice(UINT8 Hub, UINT8 Port, int LowSpeed) {
    if (LowSpeed) {
        TransferTargetSplit(Hub, Port, 1, gTransferEndpoint0MaxPacketSize);
    } else {
        TransferTargetHighSpeed(Hub, Port, gTransferEndpoint0MaxPacketSize);
    }
}

int BoardDwc2Control(UINT8 Address, UINT8 BmRequestType, UINT8 BRequest,
                     UINT16 WValue, UINT16 WIndex, void *Data, int Length) {
    UsbSetup Setup;

    Setup.BmRequestType = BmRequestType;
    Setup.BRequest = BRequest;
    Setup.WValue = WValue;
    Setup.WIndex = WIndex;
    Setup.WLength = (UINT16)(Length > 0 ? Length : 0);
    return ControlMessage(Address, &Setup, Data, Length);
}

int BoardDwc2Bulk(UINT8 Address, UINT8 Endpoint, int EndpointIn, void *Data,
                  int Length, UINT16 MaxPacketSize, UINT8 *Toggle) {
    UINT8 PacketId;
    int Ret;

    if (!Toggle || MaxPacketSize == 0) {
        return -1;
    }
    PacketId = *Toggle;
    Ret = ChunkMessage(Address, Endpoint, DWC2_HCCHAR_EPTYPE_BULK, EndpointIn,
                       &PacketId, Data, Length, MaxPacketSize);
    if (Ret >= 0) {
        *Toggle = PacketId;
    }
    return Ret;
}

