/*
 * Process.c — exec / execve / 嵌入演示（story-s2 / PR-S-process-1）
 *
 * ProcessExec：LoadPath→argv 栈→（开窗 App 才 StopAllUsers）→StartElf；virt 则 CoopDrain。
 * ProcessRunDemo：链入 `_binary_User_hello_elf_*`（无盘 hello）；同 Exec 铺 argc=0 栈（CRT 读 %rsp）。
 * 上游：Shell SystemProcess / 桌面 EXEC。装载细节 Load.c；停旧用户见 ProcessStopAllUsers。
 */
#include "Process.h"
#include "ProcessPrivate.h"
#include "Elf.h"
#include "CoreOps.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "SerialLog.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Fat.h"

#define EXEC_ARGV_MAX 8
#define EXEC_ARG_LEN  64
#define EXEC_PATH_MAX 63

extern char _binary_User_hello_elf_start[];
extern char _binary_User_hello_elf_end[];

static int CopyUserCString(char *Dst, UINTN Max, UINT64 UserPtr) {
    UINTN i;
    char C;

    if (!Dst || Max == 0) {
        return -1;
    }
    if (UserPtr == 0) {
        Dst[0] = 0;
        return -1;
    }
    for (i = 0; i + 1 < Max; i++) {
        if (VirtualMemoryCopyFromUser(&C, UserPtr + i, 1) < 0) {
            Dst[0] = 0;
            return -1;
        }
        Dst[i] = C;
        if (C == 0) {
            return 0;
        }
    }
    Dst[Max - 1] = 0;
    return 0;
}

static void CopyPathName(char *Dst, int Max, const char *Path) {
    int i;
    const char *Base = Path;

    for (i = 0; Path[i]; i++) {
        if (Path[i] == '/' || Path[i] == '\\' || Path[i] == ':') {
            Base = &Path[i + 1];
        }
    }
    for (i = 0; i < Max - 1 && Base[i]; i++) {
        Dst[i] = Base[i];
    }
    Dst[i] = 0;
}

/* 任务名：去掉路径和 .ELF，英文按 PascalCase。全大写的 Demo 拆开；Mgr 写成 Manager。 */
static void StyleTaskName(char *Name) {
    int N = 0;
    int Dot = -1;
    int i;
    int Lower = 0;
    int Upper = 0;
    int Tail = 0;
    int ExpandMgr = 0;

    while (Name[N]) {
        if (Name[N] == '.') {
            Dot = N;
        }
        N++;
    }
    if (Dot > 0 && N - Dot == 4) {
        char A = Name[Dot + 1];
        char B = Name[Dot + 2];
        char C = Name[Dot + 3];

        if (A >= 'A' && A <= 'Z') {
            A = (char)(A - 'A' + 'a');
        }
        if (B >= 'A' && B <= 'Z') {
            B = (char)(B - 'A' + 'a');
        }
        if (C >= 'A' && C <= 'Z') {
            C = (char)(C - 'A' + 'a');
        }
        if (A == 'e' && B == 'l' && C == 'f') {
            Name[Dot] = 0;
            N = Dot;
        }
    }
    for (i = 0; i < N; i++) {
        if (Name[i] >= 'a' && Name[i] <= 'z') {
            Lower = 1;
        }
        if (Name[i] >= 'A' && Name[i] <= 'Z') {
            Upper = 1;
        }
    }
    if (Upper && !Lower) {
        if (N >= 5 && Name[N - 4] == 'D' && Name[N - 3] == 'E' && Name[N - 2] == 'M' &&
            Name[N - 1] == 'O') {
            Tail = N - 4;
        } else if (N >= 4 && Name[N - 3] == 'M' && Name[N - 2] == 'G' && Name[N - 1] == 'R') {
            /* TASKMGR → TaskManager（15 字内写全） */
            ExpandMgr = 1;
            Tail = N - 3;
        }
        for (i = 0; i < N; i++) {
            if (Name[i] >= 'A' && Name[i] <= 'Z') {
                Name[i] = (char)(Name[i] - 'A' + 'a');
            }
        }
        if (Name[0] >= 'a' && Name[0] <= 'z') {
            Name[0] = (char)(Name[0] - 'a' + 'A');
        }
        if (ExpandMgr && Tail > 0 && Tail + 8 < 16) {
            Name[Tail] = 'M';
            Name[Tail + 1] = 'a';
            Name[Tail + 2] = 'n';
            Name[Tail + 3] = 'a';
            Name[Tail + 4] = 'g';
            Name[Tail + 5] = 'e';
            Name[Tail + 6] = 'r';
            Name[Tail + 7] = 0;
            return;
        }
        if (Tail > 0 && Name[Tail] >= 'a' && Name[Tail] <= 'z') {
            Name[Tail] = (char)(Name[Tail] - 'a' + 'A');
        }
        return;
    }
    if (Name[0] >= 'a' && Name[0] <= 'z') {
        Name[0] = (char)(Name[0] - 'a' + 'A');
    }
}

/*
 * 在新用户栈顶构造：argc | argv[] | NULL | envp NULL | 字符串区
 * 返回新 rsp（指向 argc；16 字节对齐，供 AAPCS64 / SysV CRT）
 */
static int ProcessSetupArgvStack(VIRTUAL_ADDRESS_SPACE *Space, UINT64 StackTop,
                                 char ArgBuf[][EXEC_ARG_LEN], int Argc,
                                 UINT64 *OutRsp) {
    UINT64 Sp = StackTop;
    UINT64 StrPtrs[EXEC_ARGV_MAX];
    UINT64 PtrSlot;
    UINT64 ArgcSlot;
    UINT64 Need;
    int i;
    UINTN Len;

    if (!Space || !OutRsp || Argc < 0 || Argc > EXEC_ARGV_MAX) {
        return -1;
    }

    /* 经 Space 页表写物理页，不 mov CR3——避免与定时器抢占互相踩页表 */
    for (i = Argc - 1; i >= 0; i--) {
        Len = 0;
        while (ArgBuf[i][Len] && Len + 1 < EXEC_ARG_LEN) {
            Len++;
        }
        Len++; /* NUL */
        Sp = (Sp - Len) & ~7ULL;
        if (VirtualMemoryCopyToSpace(Space, Sp, ArgBuf[i], Len) < 0) {
            return -1;
        }
        StrPtrs[i] = Sp;
    }

    /* argc + argv[]+NULL + envp NULL；整体 16 对齐 */
    Need = 8ULL * (UINT64)(Argc + 3);
    Sp = (Sp - Need) & ~0xFULL;
    ArgcSlot = Sp;
    PtrSlot = Sp + 8;
    {
        UINT64 Ac = (UINT64)(UINT32)Argc;
        UINT64 Z = 0;

        if (VirtualMemoryCopyToSpace(Space, ArgcSlot, &Ac, 8) < 0) {
            return -1;
        }
        for (i = 0; i < Argc; i++) {
            if (VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)i, &StrPtrs[i],
                                        8) < 0) {
                return -1;
            }
        }
        if (VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)Argc, &Z, 8) < 0 ||
            VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)(Argc + 1), &Z, 8) <
                0) {
            return -1;
        }
    }

    *OutRsp = ArgcSlot;
    return 0;
}

int ProcessExec(const char *Path) {
    VIRTUAL_ADDRESS_SPACE *Space;
    ELF_LOAD_RESULT Info;
    UINT64 NewRsp;
    char Dummy[1][EXEC_ARG_LEN];

    if (!Path || !Path[0]) {
        LogMisc("exec: empty path\n");
        return -1;
    }
    if (ProcessLoadPath(Path, &Space, &Info) != 0) {
        return -1;
    }
    /* CRT _start 读 (%rsp)=argc；须把 rsp 落到已映射栈页内（与 execve 一致） */
    if (ProcessSetupArgvStack(Space, Info.StackTop, Dummy, 0, &NewRsp) != 0) {
        VirtualMemorySpaceDestroy(Space);
        LogMisc("exec: argv stack failed\n");
        return -1;
    }
    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    Info.StackTop = NewRsp;
    HalInstallUserMode();
    /* 单用户 GUI：先停旧任务，避免 Raise 挪槽导致 wid 串台 */
    /* 只有会开窗的程序才清掉已有用户进程。Hello 只打一行字，不应关掉 GUI Demo。 */
    if (ProcessAppTakesWindow(Path)) {
        ProcessStopAllUsers();
    }
    SchedulerReapOrphanZombies();
    ProcessApplyAppFont(Path);
    {
        char Name[16];

        CopyPathName(Name, (int)sizeof(Name), Path);
        StyleTaskName(Name);
        if (ProcessStartElf(Space, &Info, Name, ProcessAppTakesWindow(Path)) != 0) {
            ProcessRestoreAppFont();
            return -1;
        }
    }
    /*
     * 无窗短命 ELF：同步 CoopDrain。
     * 开窗 App（PKG window=yes）：异步；drain 会卡死 GUI 事件循环。
     * HalPlatformIsVirtSerialConsole：仅平台为真才对开窗 App 也 drain；
     * x64 桌面该函数恒 0 → 开窗恒异步。
     */
    if (!ProcessAppTakesWindow(Path) || HalPlatformIsVirtSerialConsole()) {
        SchedulerCoopDrainUsers();
    }
    return 0;
}

/*
 * 替换当前用户任务映像。成功：改写 Frame，不返回用户态旧点；
 * 失败：返回 -1（Frame 原样，可写 rax=-1）。
 */
int ProcessExecve(struct HAL_INTERRUPT_FRAME *Frame, const char *Path,
                  UINT64 UserArgv, UINT64 UserEnvp) {
    TASK *T;
    VIRTUAL_ADDRESS_SPACE *OldSpace;
    VIRTUAL_ADDRESS_SPACE *NewSpace;
    ELF_LOAD_RESULT Info;
    char ArgBuf[EXEC_ARGV_MAX][EXEC_ARG_LEN];
    int Argc = 0;
    int i;
    UINT64 NewRsp;
    char Name[16];

    (void)UserEnvp;

    T = SchedulerCurrent();
    if (!T || !T->IsUser || !T->UserSpace || !Frame || !Path || !Path[0]) {
        return -1;
    }

    /* 先从旧地址空间拷出 argv（再销毁页表） */
    VirtualMemoryLoadPageTable(T->PageRoot);
    if (UserArgv != 0) {
        for (i = 0; i < EXEC_ARGV_MAX; i++) {
            UINT64 Ptr = 0;
            if (VirtualMemoryCopyFromUser(&Ptr, UserArgv + 8ULL * (UINT64)i, 8) < 0) {
                break;
            }
            if (Ptr == 0) {
                break;
            }
            if (CopyUserCString(ArgBuf[i], EXEC_ARG_LEN, Ptr) != 0) {
                break;
            }
            Argc++;
        }
    }
    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());

    if (Argc == 0) {
        CopyPathName(ArgBuf[0], EXEC_ARG_LEN, Path);
        Argc = 1;
    }

    if (ProcessLoadPath(Path, &NewSpace, &Info) != 0) {
        return -1;
    }

    if (ProcessSetupArgvStack(NewSpace, Info.StackTop, ArgBuf, Argc, &NewRsp) != 0) {
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        VirtualMemorySpaceDestroy(NewSpace);
        LogMisc("execve: argv stack failed\n");
        return -1;
    }

    OldSpace = T->UserSpace;
    T->UserSpace = NewSpace;
    T->PageRoot = VirtualMemorySpaceRoot(NewSpace);
    CopyPathName(Name, (int)sizeof(Name), Path);
    StyleTaskName(Name);
    for (i = 0; i < 15 && Name[i]; i++) {
        T->Name[i] = Name[i];
    }
    T->Name[i] = 0;
    T->Waiting = 0;
    T->BrkBase = Info.BrkBase;
    T->Brk = Info.BrkBase;
    T->MmapNext = USER_MMAP_BASE;
    T->SigHandlerInt = 0;
    T->SigHandlerTerm = 0;
    T->PendingKill = 0;
    /* 占窗 GUI：开窗前 printf 亦勿进 blocks>；控制台 ELF 清回 0 */
    T->StdoutSerialOnly = ProcessAppTakesWindow(Path) ? 1 : 0;
    /* 保留 Fds / ParentId / Id；映像已换 */
    HalFrameSetUserEntry(Frame, Info.Entry, NewRsp);
    /* 本拍 syscall live 帧；切走前 PinLiveFrame 会再挂 */
    T->Frame = Frame;
    T->Started = 1; /* 经 sysret/iret 回到用户，非 HalUserEnter 首入 */

    VirtualMemoryLoadPageTable(T->PageRoot);
    VirtualMemorySpaceDestroy(OldSpace);

    HalInstallUserMode();
    ProcessApplyAppFont(Path);
    DebugWrite("execve: ");
    DebugWrite(Path);
    DebugWrite(" entry=");
    DebugHex64(Info.Entry);
    DebugWrite("\n");
    return 0;
}

int ProcessRunDemo(void) {
    UINTN Size = (UINTN)(_binary_User_hello_elf_end - _binary_User_hello_elf_start);
    UINT64 NewRsp;
    char Dummy[1][EXEC_ARG_LEN];
    VIRTUAL_ADDRESS_SPACE *Space;
    ELF_LOAD_RESULT Info;

    if (Size == 0) {
        LogMisc("user: no embedded hello.elf\n");
        return -1;
    }

    Space = VirtualMemorySpaceCreate();
    if (!Space) {
        LogMisc("user: address space failed\n");
        return -1;
    }

    if (ElfLoadFromMemory(Space, _binary_User_hello_elf_start, Size, &Info) != 0) {
        VirtualMemorySpaceDestroy(Space);
        return -1;
    }
    /* StackTop 为映射区开区间上沿；不铺栈则 _start 的 mov (%rsp) 必 #PF */
    if (ProcessSetupArgvStack(Space, Info.StackTop, Dummy, 0, &NewRsp) != 0) {
        VirtualMemorySpaceDestroy(Space);
        LogMisc("user: argv stack failed\n");
        return -1;
    }
    Info.StackTop = NewRsp;

    HalInstallUserMode();
    if (ProcessStartElf(Space, &Info, "hello", 0) != 0) {
        return -1;
    }
    SchedulerCoopDrainUsers();
    return 0;
}
