/*
 * Ring.c — Command / Event 环生产消费
 *
 * 【初学者】
 * - UsbHid 子模块：Enqueue TRB、处理 Event Ring 回调。
 */
#include "Internal.h"

void RingInitialize(TRANSFER_REQUEST_BLOCK *Ring, RING_STATE *State, UINT32 Size) {
    if (Size < 2u) {
        Size = TRANSFER_RING_SIZE;
    }
    MemoryZero(Ring, sizeof(TRANSFER_REQUEST_BLOCK) * Size);
    Ring[Size - 1u].Parameter = PhysicalAddress(&Ring[0]);
    Ring[Size - 1u].Control = TRB_TYPE(TRB_LINK) | TRB_TC | TRB_C;
    State->EnqueueIndex = 0;
    State->ProducerCycleState = 1;
    State->Size = Size;
}

void RingEnqueue(TRANSFER_REQUEST_BLOCK *Ring, RING_STATE *State, UINT64 Param, UINT32 Status, UINT32 Control) {
    UINT32 i = State->EnqueueIndex;
    UINT32 Size = State->Size ? State->Size : TRANSFER_RING_SIZE;

    Ring[i].Parameter = Param;
    Ring[i].Status = Status;
    MemoryFence();
    Ring[i].Control = Control | (State->ProducerCycleState & 1u);
    i++;
    if (i == Size - 1u) {
        Ring[Size - 1u].Parameter = PhysicalAddress(&Ring[0]);
        Ring[Size - 1u].Control = TRB_TYPE(TRB_LINK) | TRB_TC | (State->ProducerCycleState & 1u);
        i = 0;
        State->ProducerCycleState ^= 1u;
    }
    State->EnqueueIndex = i;
}

void DoorbellRing(UINT32 Slot, UINT32 Target) {
    MemoryFence();
    Write32(gDoorbellBase + (UINT64)Slot * 4u, Target & 0xFFu);
}

void DeviceContextBaseAddressArraySet(UINT32 Slot, UINT64 P) {
    if (gDeviceContextBaseAddressArray == 0 || Slot > gMaxSlots) {
        return;
    }
    gDeviceContextBaseAddressArray[Slot] = P;
}
