/*
 * ProcessFork.c — K50：fork 克隆页表；asm 跳进子、exit 跳回父
 *
 * gUserDone：3=进子（rax=0）；2=回父（rax=pid）；1=结束 HalSyscallRun
 * 用户恢复点用 gForkUser*；勿覆盖 gSyscallRet*（否则父 exit 会跳回用户死循环）
 */
#include "ProcessFork.h"
#include "VirtualMemory.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define FORK_REG_N 15
#define FORK_PID_CHILD 2

extern volatile int gUserDone;
UINT64 gForkResumeRax;
UINT64 gForkChildCr3;
UINT64 gForkParentCr3;
UINT64 gForkUserRip;
UINT64 gForkUserRsp;

static VIRTUAL_ADDRESS_SPACE *gParentSpace;
static VIRTUAL_ADDRESS_SPACE *gChildSpace;
static int gHasParent;
static int gHasChild;
static int gChildExited;
static int gChildExitCode;
static int gCurrentIsChild;
static int gForkLive;

void ProcessForkAttach(VIRTUAL_ADDRESS_SPACE *Space) {
    gParentSpace = Space;
    gChildSpace = 0;
    gHasParent = Space != 0 ? 1 : 0;
    gHasChild = 0;
    gChildExited = 0;
    gChildExitCode = 0;
    gCurrentIsChild = 0;
    gForkLive = gHasParent;
    gForkParentCr3 = Space != 0 ? VirtualMemorySpaceRoot(Space) : 0;
    gForkChildCr3 = 0;
}

void ProcessForkDetach(void) {
    if (gChildSpace != 0) {
        VirtualMemorySpaceDestroy(gChildSpace);
        gChildSpace = 0;
    }
    gParentSpace = 0;
    gHasParent = 0;
    gHasChild = 0;
    gForkLive = 0;
    gCurrentIsChild = 0;
}

int ProcessForkSyscall(UINT64 *Regs) {
    if (!gForkLive || !gHasParent || gParentSpace == 0 || Regs == 0) {
        if (Regs != 0) {
            Regs[0] = ~((UINT64)0);
        }
        return 1;
    }
    if (gHasChild) {
        Regs[0] = ~((UINT64)0);
        return 1;
    }
    gChildSpace = VirtualMemorySpaceClone(gParentSpace);
    if (gChildSpace == 0) {
        HalSerialWriteShell("fork: clone fail\n");
        Regs[0] = ~((UINT64)0);
        return 1;
    }
    gForkUserRip = Regs[FORK_REG_N];
    gForkUserRsp = (UINT64)(UINTN)(Regs + FORK_REG_N + 3);
    gForkParentCr3 = VirtualMemorySpaceRoot(gParentSpace);
    gForkChildCr3 = VirtualMemorySpaceRoot(gChildSpace);

    gHasChild = 1;
    gChildExited = 0;
    gCurrentIsChild = 1;
    gForkResumeRax = 0;
    gUserDone = 3;
    return 1;
}

int ProcessWaitSyscall(UINT64 *Regs) {
    if (!gForkLive || Regs == 0) {
        if (Regs != 0) {
            Regs[0] = ~((UINT64)0);
        }
        return 1;
    }
    if (!gHasChild || !gChildExited) {
        Regs[0] = ~((UINT64)0);
        return 1;
    }
    Regs[0] = (UINT64)(UINT32)gChildExitCode;
    return 1;
}

int ProcessExitSyscall(UINT64 *Regs) {
    UINT64 Code;

    if (Regs == 0) {
        return 0;
    }
    Code = Regs[5];
    if (!gForkLive || !gCurrentIsChild) {
        return 0;
    }
    if (!gHasParent || gForkParentCr3 == 0) {
        return 0;
    }

    gChildExited = 1;
    gChildExitCode = (int)Code;
    gCurrentIsChild = 0;
    gForkResumeRax = (UINT64)FORK_PID_CHILD;
    gUserDone = 2;
    return 1;
}
