/*
 * Transfer.c — TRB 提交与完成等待
 *
 * 【初学者】
 * - UsbHid 子模块：Bulk/Interrupt 传输；gTransferDone 同步。
 */
#include "Internal.h"

static int WaitTransfer(UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        EventProcess();
        if (gTransferDone) {
            return (gTransferCode == CC_SUCCESS || gTransferCode == CC_SHORT_PACKET) ? 0 : -1;
        }
        CpuPause();
    }
    return -1;
}

int ControlTransfer(SETUP_PACKET *Setup, void *Data) {
    TRANSFER_REQUEST_BLOCK *Ring;
    RING_STATE *State;
    UINT64 SetupParameter = 0;
    UINT8 *Raw;
    UINT32 TransferType = 0;
    UINT32 StatusDirection;
    int i;

    if (gTransferDevice == 0 || Setup == 0) {
        return -1;
    }
    Ring = gTransferDevice->EndpointZeroRing;
    State = &gTransferDevice->EndpointZero;
    Raw = (UINT8 *)Setup;
    for (i = 0; i < 8; i++) {
        SetupParameter |= ((UINT64)Raw[i]) << (8 * i);
    }
    if (Setup->WLength && Data) {
        TransferType = (Setup->BmRequestType & 0x80u) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    gTransferDone = 0;
    gTransferCode = 0;
    RingEnqueue(Ring, State, SetupParameter, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | TransferType);
    if (Setup->WLength && Data) {
        UINT32 Direction = (Setup->BmRequestType & 0x80u) ? TRB_DIR_IN : 0;
        RingEnqueue(Ring, State, PhysicalAddress(Data), Setup->WLength, TRB_TYPE(TRB_DATA) | Direction);
    }
    StatusDirection = (Setup->WLength && (Setup->BmRequestType & 0x80u)) ? 0 : TRB_DIR_IN;
    RingEnqueue(Ring, State, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | StatusDirection);
    DoorbellRing(gTransferDevice->SlotIdentifier, 1);
    return WaitTransfer(150000u);
}

int DescriptorGet(UINT16 TypeIndex, UINT16 Length, void *Buffer) {
    SETUP_PACKET Setup;

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 0x06;
    Setup.WValue = TypeIndex;
    Setup.WIndex = 0;
    Setup.WLength = Length;
    MemoryZero(Buffer, Length);
    return ControlTransfer(&Setup, Buffer);
}
