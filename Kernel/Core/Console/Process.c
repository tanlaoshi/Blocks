/*
 * Process.c — 用户 ELF 装载与 HalSyscallRun（K19/K25/K49）
 *
 * 【初学者】
 * - 分层：Core/Console；装载细节 ElfLoader.c；fork 见 ProcessFork.c
 * - 对外入口：ProcessExecPath、ProcessRunHello、ProcessLastExecName
 * - X64：优先 VirtualMemorySpace + CR3 切换；失败回落恒等映射
 * - 不做：完整进程调度、argv/env（另刀）
 */
#include "Process.h"
#include "ElfLoader.h"
#include "FatFile.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "HalSyscall.h"
#include "PhysicalMemory.h"
#include "ProcessFork.h"
#include "VirtualMemory.h"
#include "SerialConfig.h"

#define ELF_MAX (128u * 1024u)

static char gLastName[FAT_NAME_MAX];

static void SetLast(const char *Path) {
    int i;
    if (Path == 0) {
        gLastName[0] = 0;
        return;
    }
    for (i = 0; i + 1 < (int)sizeof(gLastName) && Path[i]; i++) {
        gLastName[i] = Path[i];
    }
    gLastName[i] = 0;
}

/*
 * ProcessLastExecName — 最近一次 exec 路径（供 Shell `ps`）
 *
 * 谁调用：ShellSystem CommandPs。
 * 返回：静态缓冲指针；无 exec 时可能为空串
 */
const char *ProcessLastExecName(void) {
    return gLastName;
}

/*
 * ProcessExecPath — 从 FAT 根读 ELF 并 syscall 跑进用户态
 *
 * 做什么：读盘 → ElfLoader →（可选）独立页表 → HalSyscallRun → 恢复内核 CR3。
 * 谁调用：Shell `exec` / `hello`；ConsoleRun 开机 HELLO.ELF。
 * 前后文：前 — HalBlockReady；后 — SetLast；兄弟 ProcessForkAttach
 * 返回：HalSyscallRun 结果；负值表示装载/初始化失败
 */
int ProcessExecPath(const char *Path) {
    void *ImageBuffer;
    UINT32 Pages;
    UINT32 Size = 0;
    ELF_IMAGE Img;
    int N;
    int Rc;
    VIRTUAL_ADDRESS_SPACE *Space;
    UINT64 KernelRoot;
    UINT64 UserRoot;

    if (Path == 0 || Path[0] == 0) {
        return -1;
    }
    if (!HalBlockReady()) {
        HalSerialWriteShell("User: no block\n");
        return -1;
    }
    if (HalSyscallInitialize() != 0) {
        return -1;
    }

    Pages = (ELF_MAX + 4095u) / 4096u;
    ImageBuffer = PhysicalMemoryAllocatePages(Pages);
    if (ImageBuffer == 0) {
        HalSerialWriteShell("User: oom\n");
        return -1;
    }

    N = FatFileReadPath(Path, ImageBuffer, ELF_MAX, &Size);
    if (N < 0 || Size == 0) {
        HalSerialWriteShell("User: missing ");
        HalSerialWriteShell(Path);
        HalSerialWriteShell("\n");
        PhysicalMemoryFreePages(ImageBuffer, Pages);
        return -1;
    }

    HalSerialWriteShell("User: load ");
    HalSerialWriteShell(Path);
    HalSerialWriteShell("\n");

    Space = VirtualMemorySpaceCreate();
    if (Space != 0) {
        if (ElfLoaderFromMemoryToSpace(ImageBuffer, Size, Space, &Img) != 0) {
            HalSerialWriteShell("User: elf space load fail\n");
            VirtualMemorySpaceDestroy(Space);
            PhysicalMemoryFreePages(ImageBuffer, Pages);
            return -1;
        }
        PhysicalMemoryFreePages(ImageBuffer, Pages);
        KernelRoot = VirtualMemoryKernelRoot();
        UserRoot = VirtualMemorySpaceRoot(Space);
        HalSerialWriteShell("User: space cr3 switch\n");
        ProcessForkAttach(Space);
        VirtualMemoryLoadPageTable(UserRoot);
        SetLast(Path);
        Rc = HalSyscallRun(Img.Entry, Img.StackTop);
        VirtualMemoryLoadPageTable(KernelRoot);
        ProcessForkDetach();
        VirtualMemorySpaceDestroy(Space);
    } else {
        /* 非 X64 或 Create 失败：回落恒等装载 */
        if (ElfLoaderFromMemory(ImageBuffer, Size, &Img) != 0) {
            HalSerialWriteShell("User: elf load fail\n");
            PhysicalMemoryFreePages(ImageBuffer, Pages);
            return -1;
        }
        PhysicalMemoryFreePages(ImageBuffer, Pages);
        SetLast(Path);
        Rc = HalSyscallRun(Img.Entry, Img.StackTop);
    }

    if (Rc == 0) {
        HalSerialWriteShell("User: exit\n");
    } else {
        HalSerialWriteShell("User: run fail\n");
    }
    return Rc;
}

/*
 * ProcessRunHello — 开机跑一次 HELLO.ELF
 *
 * 谁调用：ConsoleRun 进入提示符前。
 * 返回：同 ProcessExecPath
 */
int ProcessRunHello(void) {
    return ProcessExecPath("HELLO.ELF");
}
