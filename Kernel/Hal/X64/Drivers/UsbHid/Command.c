/*
 * Command.c — xHCI 命令环（Enable Slot、Address Device 等）
 *
 * 【初学者】
 * - UsbHid 子模块：无协议重命名；仅文档与清晰局部变量。
 */
#include "Internal.h"

int RegisterWaitClear(UINT64 Reg, UINT32 Mask, UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        if ((Read32(Reg) & Mask) == 0) {
            return 0;
        }
        CpuPause();
    }
    return -1;
}

int RegisterWaitSet(UINT64 Reg, UINT32 Mask, UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        if ((Read32(Reg) & Mask) == Mask) {
            return 0;
        }
        CpuPause();
    }
    return -1;
}

static int WaitCommand(UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        EventProcess();
        if (gCommandDone) {
            return (gCommandCode == CC_SUCCESS) ? 0 : -1;
        }
        CpuPause();
    }
    return -1;
}

int CommandSubmit(UINT64 Param, UINT32 Control, UINT32 *SlotOut) {
    gCommandDone = 0;
    gCommandCode = 0;
    gCommandSlot = 0;
    RingEnqueue(gCommandRing, &gCommand, Param, 0, Control | TRB_IOC);
    DoorbellRing(0, 0);
    MemoryFence();
    if (WaitCommand(200000u) < 0) {
        return -1;
    }
    if (SlotOut) {
        *SlotOut = gCommandSlot;
    }
    return 0;
}
