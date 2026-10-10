/*
 * Process.c — K19 HELLO；K25 exec；K49 独立页表 + 切 CR3（execve 形）
 */
#include "Process.h"
#include "ElfLoader.h"
#include "FatFile.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "HalSyscall.h"
#include "PhysicalMemory.h"
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

const char *ProcessLastExecName(void) {
    return gLastName;
}

int ProcessExecPath(const char *Path) {
    void *Buf;
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
    if (HalSyscallInit() != 0) {
        return -1;
    }

    Pages = (ELF_MAX + 4095u) / 4096u;
    Buf = PhysicalMemoryAllocatePages(Pages);
    if (Buf == 0) {
        HalSerialWriteShell("User: oom\n");
        return -1;
    }

    N = FatFileReadPath(Path, Buf, ELF_MAX, &Size);
    if (N < 0 || Size == 0) {
        HalSerialWriteShell("User: missing ");
        HalSerialWriteShell(Path);
        HalSerialWriteShell("\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }

    HalSerialWriteShell("User: load ");
    HalSerialWriteShell(Path);
    HalSerialWriteShell("\n");

    Space = VirtualMemorySpaceCreate();
    if (Space != 0) {
        if (ElfLoaderFromMemoryToSpace(Buf, Size, Space, &Img) != 0) {
            HalSerialWriteShell("User: elf space load fail\n");
            VirtualMemorySpaceDestroy(Space);
            PhysicalMemoryFreePages(Buf, Pages);
            return -1;
        }
        PhysicalMemoryFreePages(Buf, Pages);
        KernelRoot = VirtualMemoryKernelRoot();
        UserRoot = VirtualMemorySpaceRoot(Space);
        HalSerialWriteShell("User: space cr3 switch\n");
        VirtualMemoryLoadPageTable(UserRoot);
        SetLast(Path);
        Rc = HalSyscallRun(Img.Entry, Img.StackTop);
        VirtualMemoryLoadPageTable(KernelRoot);
        VirtualMemorySpaceDestroy(Space);
    } else {
        /* 非 X64 或 Create 失败：回落恒等装载 */
        if (ElfLoaderFromMemory(Buf, Size, &Img) != 0) {
            HalSerialWriteShell("User: elf load fail\n");
            PhysicalMemoryFreePages(Buf, Pages);
            return -1;
        }
        PhysicalMemoryFreePages(Buf, Pages);
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

int ProcessRunHello(void) {
    return ProcessExecPath("HELLO.ELF");
}
