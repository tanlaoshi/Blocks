/*
 * Command.c — 命令环提交与等待
 */
#include "Private.h"

int WaitClear(UINT64 Reg, UINT32 Mask, UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        if ((Rd32(Reg) & Mask) == 0) {
            return 0;
        }
        Pause();
    }
    return -1;
}

int WaitSet(UINT64 Reg, UINT32 Mask, UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        if ((Rd32(Reg) & Mask) == Mask) {
            return 0;
        }
        Pause();
    }
    return -1;
}

static int WaitCommand(UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        ProcessEvents();
        if (gCmdDone) {
            return (gCmdCode == CC_SUCCESS) ? 0 : -1;
        }
        Pause();
    }
    return -1;
}

int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut) {
    gCmdDone = 0;
    gCmdCode = 0;
    gCmdSlot = 0;
    Enqueue(gCmdRing, &gCmd, Param, 0, Control | TRB_IOC);
    RingDoorbell(0, 0);
    Fence();
    if (WaitCommand(200000u) < 0) {
        return -1;
    }
    if (SlotOut) {
        *SlotOut = gCmdSlot;
    }
    return 0;
}
