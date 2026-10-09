/*
 * Process.c — K19：跑 RootFs 上的 HELLO.ELF
 */
#include "Process.h"
#include "ElfLoad.h"
#include "FatFile.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "HalSyscall.h"
#include "PhysicalMemory.h"
#include "ToySerialConfig.h"

#define HELLO_MAX (128u * 1024u)

int ProcessRunHello(void) {
    void *Buf;
    UINT32 Pages;
    UINT32 Size = 0;
    ELF_IMAGE Img;
    int N;
    int Rc;

    if (!HalBlockReady()) {
        HalSerialWriteShell("User: no block\n");
        return -1;
    }
    if (HalSyscallInit() != 0) {
        return -1;
    }

    Pages = (HELLO_MAX + 4095u) / 4096u;
    Buf = PhysicalMemoryAllocatePages(Pages);
    if (Buf == 0) {
        HalSerialWriteShell("User: oom\n");
        return -1;
    }

    N = FatFileRead83("HELLO   ELF", Buf, HELLO_MAX, &Size);
    if (N < 0 || Size == 0) {
        HalSerialWriteShell("User: HELLO.ELF missing\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }

    HalSerialWriteShell("User: load HELLO.ELF\n");
    if (ElfLoadFromMemory(Buf, Size, &Img) != 0) {
        HalSerialWriteShell("User: elf load fail\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    PhysicalMemoryFreePages(Buf, Pages);

    Rc = HalSyscallRun(Img.Entry, Img.StackTop);
    if (Rc == 0) {
        HalSerialWriteShell("User: exit\n");
    } else {
        HalSerialWriteShell("User: run fail\n");
    }
    return Rc;
}
