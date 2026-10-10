/*
 * Ring.c — TRB 环 / DCBAA / 门铃
 */
#include "Private.h"

void InitRing(Trb *Ring, RingState *St, UINT32 Size) {
    if (Size < 2u) {
        Size = RING_SIZE;
    }
    Zero(Ring, sizeof(Trb) * Size);
    Ring[Size - 1u].Parameter = Phys(&Ring[0]);
    Ring[Size - 1u].Control = TRB_TYPE(TRB_LINK) | TRB_TC | TRB_C;
    St->Enq = 0;
    St->Pcs = 1;
    St->Size = Size;
}

void Enqueue(Trb *Ring, RingState *St, UINT64 Param, UINT32 Status, UINT32 Control) {
    UINT32 i = St->Enq;
    UINT32 Size = St->Size ? St->Size : RING_SIZE;

    Ring[i].Parameter = Param;
    Ring[i].Status = Status;
    Fence();
    Ring[i].Control = Control | (St->Pcs & 1u);
    i++;
    if (i == Size - 1u) {
        Ring[Size - 1u].Parameter = Phys(&Ring[0]);
        Ring[Size - 1u].Control = TRB_TYPE(TRB_LINK) | TRB_TC | (St->Pcs & 1u);
        i = 0;
        St->Pcs ^= 1u;
    }
    St->Enq = i;
}

void RingDoorbell(UINT32 Slot, UINT32 Target) {
    Fence();
    Wr32(gDb + (UINT64)Slot * 4u, Target & 0xFFu);
}

void DcbaaSet(UINT32 Slot, UINT64 P) {
    if (gDcbaa == 0 || Slot > gMaxSlots) {
        return;
    }
    gDcbaa[Slot] = P;
}
