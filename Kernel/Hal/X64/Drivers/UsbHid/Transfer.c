/*
 * Transfer.c — EP0 控制传输 / GET_DESCRIPTOR
 */
#include "Private.h"

static int WaitTransfer(UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        ProcessEvents();
        if (gXferDone) {
            return (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET) ? 0 : -1;
        }
        Pause();
    }
    return -1;
}

int ControlXfer(SetupPkt *Setup, void *Data) {
    Trb *Ring;
    RingState *St;
    UINT64 SetupParam = 0;
    UINT8 *Raw;
    UINT32 Trt = 0;
    UINT32 StatusDir;
    int i;

    if (gXferDev == 0 || Setup == 0) {
        return -1;
    }
    Ring = gXferDev->Ep0Ring;
    St = &gXferDev->Ep0;
    Raw = (UINT8 *)Setup;
    for (i = 0; i < 8; i++) {
        SetupParam |= ((UINT64)Raw[i]) << (8 * i);
    }
    if (Setup->WLength && Data) {
        Trt = (Setup->BmRequestType & 0x80u) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    gXferDone = 0;
    gXferCode = 0;
    Enqueue(Ring, St, SetupParam, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | Trt);
    if (Setup->WLength && Data) {
        UINT32 Dir = (Setup->BmRequestType & 0x80u) ? TRB_DIR_IN : 0;
        Enqueue(Ring, St, Phys(Data), Setup->WLength, TRB_TYPE(TRB_DATA) | Dir);
    }
    StatusDir = (Setup->WLength && (Setup->BmRequestType & 0x80u)) ? 0 : TRB_DIR_IN;
    Enqueue(Ring, St, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | StatusDir);
    RingDoorbell(gXferDev->SlotId, 1);
    return WaitTransfer(150000u);
}

int GetDesc(UINT16 TypeIndex, UINT16 Length, void *Buf) {
    SetupPkt Setup;

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 0x06;
    Setup.WValue = TypeIndex;
    Setup.WIndex = 0;
    Setup.WLength = Length;
    Zero(Buf, Length);
    return ControlXfer(&Setup, Buf);
}
