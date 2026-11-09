/*
 * UsbHost/XhciInterrupt.c — HID Boot 中断 IN 环
 *
 * 武装一次 → 短窥事件；超时保持武装勿拆环。
 * 等事件只认本 DCI（勿吞 EP0）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static TransferRequestBlock sInterruptRing[TRANSFER_RING_TRANSFER_REQUEST_BLOCK_COUNT]
    __attribute__((aligned(64)));
static RingState sInterruptRingState;
static UINT8 sInterruptReportBuffer[64] __attribute__((aligned(64)));
static UINT32 sInterruptSlotId;
static UINT32 sInterruptDeviceContextIndex;
static UINT32 sInterruptTransferLength;
static UINT8 sInterruptArmed;

static UINT64 PointerPhysical(const void *Pointer) {
    return (UINT64)(UINTN)Pointer;
}

static void MemoryZero(void *Destination, UINTN Size) {
    UINT8 *Bytes = (UINT8 *)Destination;
    UINTN Index;

    for (Index = 0; Index < Size; Index++) {
        Bytes[Index] = 0;
    }
}

static void RingInitialize(TransferRequestBlock *Ring, RingState *State, UINT32 Count) {
    MemoryZero(Ring, sizeof(TransferRequestBlock) * Count);
    State->EnqueueIndex = 0;
    State->ProducerCycleState = 1;
    State->BlockCount = Count;
    Ring[Count - 1u].Parameter = PointerPhysical(Ring);
    Ring[Count - 1u].Control =
        TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_LINK) | (1u << 1) | 1u;
    UsbHostCacheCleanRange(Ring, sizeof(TransferRequestBlock) * Count);
}

static void RingEnqueue(TransferRequestBlock *Ring, RingState *State,
                        const TransferRequestBlock *Source) {
    TransferRequestBlock *Destination = &Ring[State->EnqueueIndex];

    Destination->Parameter = Source->Parameter;
    Destination->Status = Source->Status;
    Destination->Control = (Source->Control & ~1u) | State->ProducerCycleState;
    UsbHostCacheCleanRange(Destination, sizeof(*Destination));
    State->EnqueueIndex++;
    if (State->EnqueueIndex + 1u >= State->BlockCount) {
        TransferRequestBlock *Link = &Ring[State->BlockCount - 1u];
        Link->Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_LINK) |
                        (1u << 1) | State->ProducerCycleState;
        UsbHostCacheCleanRange(Link, sizeof(*Link));
        State->EnqueueIndex = 0;
        State->ProducerCycleState ^= 1u;
    }
}

void UsbHostDrainTransferEvents(void) {
    UINT32 CompletionCode;
    UINTN Guard;

    for (Guard = 0; Guard < 32u; Guard++) {
        if (UsbHostWaitTransferEventLimited(&CompletionCode, 20u, 0) != 0) {
            break;
        }
    }
}

void UsbHostInterruptRingInitialize(UINT32 SlotId, UINT32 DeviceContextIndex,
                                    UINT32 TransferLength) {
    sInterruptSlotId = SlotId;
    sInterruptDeviceContextIndex = DeviceContextIndex;
    sInterruptTransferLength = TransferLength != 0 ? TransferLength : 8u;
    if (sInterruptTransferLength > sizeof(sInterruptReportBuffer)) {
        sInterruptTransferLength = sizeof(sInterruptReportBuffer);
    }
    sInterruptArmed = 0;
    RingInitialize(sInterruptRing, &sInterruptRingState,
                   TRANSFER_RING_TRANSFER_REQUEST_BLOCK_COUNT);
    MemoryZero(sInterruptReportBuffer, sizeof(sInterruptReportBuffer));
}

UINT64 UsbHostInterruptRingPhysical(void) {
    return PointerPhysical(sInterruptRing) | 1ULL;
}

UINT8 *UsbHostInterruptReportBuffer(void) {
    return sInterruptReportBuffer;
}

int UsbHostInterruptSubmitOnceDetailed(UINT32 *CompletionCodeOut) {
    TransferRequestBlock Block;
    UINT32 CompletionCode = 0;

    if (CompletionCodeOut != 0) {
        *CompletionCodeOut = 0;
    }
    if (sInterruptSlotId == 0 || sInterruptDeviceContextIndex == 0) {
        return -1;
    }
    if (sInterruptArmed == 0) {
        UsbHostDrainTransferEvents();
        MemoryZero(sInterruptReportBuffer, sizeof(sInterruptReportBuffer));
        UsbHostCacheCleanRange(sInterruptReportBuffer, sizeof(sInterruptReportBuffer));
        MemoryZero(&Block, sizeof(Block));
        Block.Parameter = PointerPhysical(sInterruptReportBuffer);
        Block.Status = sInterruptTransferLength;
        Block.Control = TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_NORMAL) |
                        TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
                        TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_SHORT_PACKET;
        RingEnqueue(sInterruptRing, &sInterruptRingState, &Block);
        UsbHostRingDoorbell(sInterruptSlotId, sInterruptDeviceContextIndex);
        sInterruptArmed = 1;
    }
    /* 短窥：超时保持武装；由 HubWatch 紧轮询，勿长堵再刷屏 */
    if (UsbHostWaitTransferEventLimited(&CompletionCode, 2000u,
                                        sInterruptDeviceContextIndex) != 0) {
        return -1;
    }
    sInterruptArmed = 0;
    if (CompletionCodeOut != 0) {
        *CompletionCodeOut = CompletionCode;
    }
    if (CompletionCode != COMPLETION_CODE_SUCCESS &&
        CompletionCode != COMPLETION_CODE_SHORT_PACKET) {
        return -1;
    }
    UsbHostCacheCleanInvalidateRange(sInterruptReportBuffer,
                                     sizeof(sInterruptReportBuffer));
    return 0;
}

#else

void UsbHostInterruptRingInitialize(UINT32 SlotId, UINT32 DeviceContextIndex,
                                    UINT32 TransferLength) {
    (void)SlotId;
    (void)DeviceContextIndex;
    (void)TransferLength;
}

UINT64 UsbHostInterruptRingPhysical(void) {
    return 0;
}

UINT8 *UsbHostInterruptReportBuffer(void) {
    return 0;
}

int UsbHostInterruptSubmitOnceDetailed(UINT32 *CompletionCodeOut) {
    (void)CompletionCodeOut;
    return -1;
}

void UsbHostDrainTransferEvents(void) {
}

#endif
