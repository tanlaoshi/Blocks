/*
 * Process.c — K19 HELLO；K25 exec 按路径跑根目录 ELF
 */
#include "Process.h"
#include "ElfLoad.h"
#include "FatFile.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "HalSyscall.h"
#include "PhysicalMemory.h"
#include "ToySerialConfig.h"

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
    if (ElfLoadFromMemory(Buf, Size, &Img) != 0) {
        HalSerialWriteShell("User: elf load fail\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    PhysicalMemoryFreePages(Buf, Pages);

    SetLast(Path);
    Rc = HalSyscallRun(Img.Entry, Img.StackTop);
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
