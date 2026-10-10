/*
 * ProcessFork.c — fork/wait/exit  syscall 协作（K50）
 *
 * 【初学者】
 * - 分层：Core/Console；页表克隆 VirtualMemorySpaceClone
 * - 对外入口：ProcessForkAttach/Detach、ProcessForkSyscall、ProcessWaitSyscall、ProcessExitSyscall
 * - gUserDone：3=进子；2=回父；与 HalSyscall.S 约定
 * - 不做：多子进程、waitpid 全语义
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

/*
 * ProcessForkAttach — exec 前绑定可 fork 的用户 Space
 *
 * 谁调用：ProcessExecPath（切 CR3 前）。
 * 返回：void
 */
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

/*
 * ProcessForkDetach — exec 结束销毁子 Space、清 fork 状态
 *
 * 谁调用：ProcessExecPath（恢复内核 CR3 后）。
 * 返回：void
 */
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

/*
 * ProcessForkSyscall — 用户 fork：克隆页表并安排父子各返回一次
 *
 * 谁调用：HalSyscallDispatch（fork 号）。
 * 返回：1 表示已改写 gUserDone/寄存器；0 拒绝
 */
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

/*
 * ProcessWaitSyscall — 等子 exit 码（单槽）
 *
 * 谁调用：HalSyscallDispatch（wait 号）。
 * 返回：1 已写 Regs[0]；子未退出时 Regs[0]=~0
 */
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

/*
 * ProcessExitSyscall — 子进程 exit：切回父 CR3，父 wait 可见退出码
 *
 * 谁调用：HalSyscallDispatch（exit 号，子进程上下文）。
 * 返回：1 子 exit 已拦截；0 普通 shell exit
 */
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
