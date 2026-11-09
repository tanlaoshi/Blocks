/*
 * UsbHost/XhciTransfer.c — 命令/事件环、每槽 EP0（xHCI 1.1）
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

static UINT64 sDoorbellBase;
static UINT64 sRuntimeBase;
static UINT64 sOperationalBase;
static UINT32 sContextSizeBytes;

static TransferRequestBlock sCommandRing[COMMAND_RING_TRANSFER_REQUEST_BLOCK_COUNT]
    __attribute__((aligned(64)));
static TransferRequestBlock sEventRing[EVENT_RING_TRANSFER_REQUEST_BLOCK_COUNT]
    __attribute__((aligned(64)));
static TransferRequestBlock
    sEndpointZeroRings[DEVICE_SLOT_COUNT_MAX + 1u]
                      [TRANSFER_RING_TRANSFER_REQUEST_BLOCK_COUNT]
    __attribute__((aligned(64)));
static UINT8 sEventRingSegmentTable[64] __attribute__((aligned(64)));
static UINT64 sDeviceContextBaseAddressArray[DEVICE_SLOT_COUNT_MAX + 1u]
    __attribute__((aligned(64)));
static UINT8 sDeviceContext[DEVICE_SLOT_COUNT_MAX + 1u][64 * 32]
    __attribute__((aligned(64)));
static UINT8 sInputContext[64 * 33] __attribute__((aligned(64)));
static UINT8 sScratchpadBuffers[SCRATCHPAD_BUFFER_COUNT_MAX][4096]
    __attribute__((aligned(4096)));
static UINT64 sScratchpadPointerArray[SCRATCHPAD_BUFFER_COUNT_MAX]
    __attribute__((aligned(64)));
static UINT8 sSetupPacket[8] __attribute__((aligned(64)));
static UINT8 sControlData[USB_CONTROL_DATA_CAPACITY] __attribute__((aligned(64)));

static RingState sCommandRingState;
static UINT32 sEventDequeueIndex;
static UINT32 sEventConsumerCycleState;
static RingState sEndpointZeroRingStates[DEVICE_SLOT_COUNT_MAX + 1u];

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

static int WaitEvent(UINT32 WantedType, UINT32 *CompletionCode, UINT32 *SlotIdOut,
                     UINT32 EndpointIdOrZero, UINTN GuardLimit) {
    UINTN Guard;

    for (Guard = 0; Guard < GuardLimit; Guard++) {
        TransferRequestBlock *Event = &sEventRing[sEventDequeueIndex];
        UINT32 Control;
        UINT32 Cycle;
        UINT32 Type;
        UINT32 EndpointId;

        UsbHostCacheCleanInvalidateRange(Event, sizeof(*Event));
        Control = Event->Control;
        Cycle = Control & 1u;
        if (Cycle != sEventConsumerCycleState) {
            /* 命令/EP0 1ms；中断窥紧循环 */
            if (EndpointIdOrZero == 0) {
                UsbHostStallMilliseconds(1u);
            }
            continue;
        }
        Type = (Control >> TRANSFER_REQUEST_BLOCK_TYPE_SHIFT) & 0x3Fu;
        EndpointId = (Control >> TRANSFER_REQUEST_BLOCK_ENDPOINT_SHIFT) & 0x1Fu;
        if (Type == WantedType &&
            (EndpointIdOrZero == 0 || EndpointId == EndpointIdOrZero)) {
            if (CompletionCode != 0) {
                *CompletionCode = (Event->Status >> 24) & 0xFFu;
            }
            if (SlotIdOut != 0) {
                *SlotIdOut = (Control >> TRANSFER_REQUEST_BLOCK_SLOT_SHIFT) & 0xFFu;
            }
            sEventDequeueIndex++;
            if (sEventDequeueIndex >= EVENT_RING_TRANSFER_REQUEST_BLOCK_COUNT) {
                sEventDequeueIndex = 0;
                sEventConsumerCycleState ^= 1u;
            }
            UsbHostWriteMmio64(sRuntimeBase + 0x38u,
                               PointerPhysical(&sEventRing[sEventDequeueIndex]) |
                                   (1ULL << 3));
            UsbHostWriteMmio32(sOperationalBase + 4u, USB_STATUS_EVENT_INTERRUPT);
            return 0;
        }
        sEventDequeueIndex++;
        if (sEventDequeueIndex >= EVENT_RING_TRANSFER_REQUEST_BLOCK_COUNT) {
            sEventDequeueIndex = 0;
            sEventConsumerCycleState ^= 1u;
        }
        UsbHostWriteMmio64(sRuntimeBase + 0x38u,
                           PointerPhysical(&sEventRing[sEventDequeueIndex]) | (1ULL << 3));
        UsbHostWriteMmio32(sOperationalBase + 4u, USB_STATUS_EVENT_INTERRUPT);
    }
    return -1;
}

int UsbHostXhciTransferInitialize(UINT64 CapabilityBase, UINT64 OperationalBase,
                                  UINT64 DoorbellBase, UINT64 RuntimeBase,
                                  UINT32 ContextSizeBytes, UINT32 MaxSlots,
                                  UINT32 ScratchpadCount) {
    UINT32 ScratchIndex;

    (void)CapabilityBase;
    sOperationalBase = OperationalBase;
    sDoorbellBase = DoorbellBase;
    sRuntimeBase = RuntimeBase;
    sContextSizeBytes = ContextSizeBytes;

    MemoryZero(sDeviceContextBaseAddressArray, sizeof(sDeviceContextBaseAddressArray));
    MemoryZero(sDeviceContext, sizeof(sDeviceContext));
    if (ScratchpadCount > 0) {
        MemoryZero(sScratchpadPointerArray, sizeof(sScratchpadPointerArray));
        for (ScratchIndex = 0; ScratchIndex < ScratchpadCount; ScratchIndex++) {
            MemoryZero(sScratchpadBuffers[ScratchIndex], 4096u);
            sScratchpadPointerArray[ScratchIndex] =
                PointerPhysical(sScratchpadBuffers[ScratchIndex]);
            UsbHostCacheCleanRange(sScratchpadBuffers[ScratchIndex], 4096u);
        }
        sDeviceContextBaseAddressArray[0] = PointerPhysical(sScratchpadPointerArray);
        UsbHostCacheCleanRange(sScratchpadPointerArray, sizeof(UINT64) * ScratchpadCount);
    }

    UsbHostWriteMmio32(OperationalBase + 0x38u, MaxSlots);
    UsbHostCacheCleanRange(sDeviceContextBaseAddressArray,
                           sizeof(sDeviceContextBaseAddressArray));
    UsbHostWriteMmio64(OperationalBase + 0x30u,
                       PointerPhysical(sDeviceContextBaseAddressArray));

    RingInitialize(sCommandRing, &sCommandRingState,
                   COMMAND_RING_TRANSFER_REQUEST_BLOCK_COUNT);
    UsbHostWriteMmio64(OperationalBase + 0x18u, PointerPhysical(sCommandRing) | 1ULL);

    MemoryZero(sEventRing, sizeof(sEventRing));
    sEventDequeueIndex = 0;
    sEventConsumerCycleState = 1;
    MemoryZero(sEventRingSegmentTable, sizeof(sEventRingSegmentTable));
    *(UINT64 *)(void *)sEventRingSegmentTable = PointerPhysical(sEventRing);
    *(UINT16 *)(void *)(sEventRingSegmentTable + 8) =
        (UINT16)EVENT_RING_TRANSFER_REQUEST_BLOCK_COUNT;
    UsbHostCacheCleanRange(sEventRing, sizeof(sEventRing));
    UsbHostCacheCleanRange(sEventRingSegmentTable, sizeof(sEventRingSegmentTable));
    UsbHostWriteMmio32(RuntimeBase + 0x20u, 0);
    UsbHostWriteMmio32(RuntimeBase + 0x28u, 1);
    UsbHostWriteMmio64(RuntimeBase + 0x30u, PointerPhysical(sEventRingSegmentTable));
    UsbHostWriteMmio64(RuntimeBase + 0x38u, PointerPhysical(sEventRing) | (1ULL << 3));
    return 0;
}

int UsbHostXhciSubmitCommand(TransferRequestBlock *Command, UINT32 *CompletionCode,
                             UINT32 *SlotIdOut) {
    RingEnqueue(sCommandRing, &sCommandRingState, Command);
    __asm__ volatile("dsb sy" ::: "memory");
    UsbHostWriteMmio32(sDoorbellBase, 0);
    return WaitEvent(TRANSFER_REQUEST_BLOCK_COMMAND_COMPLETION, CompletionCode, SlotIdOut, 0,
                     5000u);
}

void UsbHostEndpointZeroEnqueue(UINT32 SlotId, const TransferRequestBlock *Block) {
    if (SlotId == 0 || SlotId > DEVICE_SLOT_COUNT_MAX) {
        return;
    }
    RingEnqueue(sEndpointZeroRings[SlotId], &sEndpointZeroRingStates[SlotId], Block);
}

int UsbHostWaitTransferEvent(UINT32 *CompletionCode) {
    return WaitEvent(TRANSFER_REQUEST_BLOCK_TRANSFER_EVENT, CompletionCode, 0, 0, 5000u);
}

int UsbHostWaitTransferEventLimited(UINT32 *CompletionCode, UINTN GuardLimit,
                                    UINT32 EndpointIdOrZero) {
    return WaitEvent(TRANSFER_REQUEST_BLOCK_TRANSFER_EVENT, CompletionCode, 0,
                     EndpointIdOrZero, GuardLimit);
}

void UsbHostRingEndpointZeroDoorbell(UINT32 SlotId) {
    UsbHostRingDoorbell(SlotId, 1u);
}

void UsbHostRingDoorbell(UINT32 SlotId, UINT32 EndpointTarget) {
    __asm__ volatile("dsb sy" ::: "memory");
    UsbHostWriteMmio32(sDoorbellBase + (UINT64)SlotId * 4u, EndpointTarget);
}

UINT8 *UsbHostSetupPacketBuffer(void) {
    return sSetupPacket;
}

UINT8 *UsbHostControlDataBuffer(void) {
    return sControlData;
}

UINT8 *UsbHostDeviceContextSlot(UINT32 SlotId) {
    return sDeviceContext[SlotId];
}

UINT8 *UsbHostInputContextBuffer(void) {
    return sInputContext;
}

UINT32 UsbHostContextSizeBytes(void) {
    return sContextSizeBytes;
}

void UsbHostBindEndpointZeroRing(UINT32 SlotId) {
    if (SlotId == 0 || SlotId > DEVICE_SLOT_COUNT_MAX) {
        return;
    }
    RingInitialize(sEndpointZeroRings[SlotId], &sEndpointZeroRingStates[SlotId],
                   TRANSFER_RING_TRANSFER_REQUEST_BLOCK_COUNT);
}

UINT64 UsbHostEndpointZeroRingPhysical(UINT32 SlotId) {
    if (SlotId == 0 || SlotId > DEVICE_SLOT_COUNT_MAX) {
        return 0;
    }
    return PointerPhysical(sEndpointZeroRings[SlotId]);
}

void UsbHostSetDeviceContextBase(UINT32 SlotId, UINT64 Physical) {
    sDeviceContextBaseAddressArray[SlotId] = Physical;
    UsbHostCacheCleanRange(&sDeviceContextBaseAddressArray[SlotId], sizeof(UINT64));
}
#else
#endif
