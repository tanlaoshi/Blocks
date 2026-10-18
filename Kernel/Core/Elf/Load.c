/*
 * Load.c — PR-S3-elf-1：静态 ET_EXEC 装载（段 + 用户栈）
 */
#include "ElfPrivate.h"
#include "PhysicalMemory.h"
#include "ToySerialLog.h"

static void ReleaseUserRange(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Start, UINT64 End) {
    UINT64 Virt;

    if (!Space || Space->Root == 0 || Start >= End) {
        return;
    }
    Start &= ~(UINT64)(PAGE_SIZE - 1);
    for (Virt = Start; Virt < End; Virt += PAGE_SIZE) {
        UINT64 Pte = HalPageGetEntry(Space->Root, Virt);
        UINT64 Phys;

        if ((Pte & HAL_PAGE_PRESENT) == 0) {
            continue;
        }
        Phys = Pte & 0x000FFFFFFFFFF000ULL;
        if (Phys != 0) {
            PhysicalMemoryFreePage((void *)(UINTN)Phys);
        }
    }
    HalPageUnmapRange(Space->Root, Start, End);
}

static void ReleaseLoad(VIRTUAL_ADDRESS_SPACE *Space, const Elf64_Phdr *Ph, UINT64 Bias) {
    UINT64 Vaddr;
    UINT64 End;

    if (!Ph || Ph->p_memsz == 0) {
        return;
    }
    Vaddr = Ph->p_vaddr + Bias;
    End = Vaddr + Ph->p_memsz;
    ReleaseUserRange(Space, Vaddr, End);
}

int ElfLoadFromMemory(VIRTUAL_ADDRESS_SPACE *Space, const void *Image, UINTN Size,
                      ELF_LOAD_RESULT *Out) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;
    const Elf64_Phdr *Phdrs;
    UINT16 Pn;
    UINT16 i;
    UINT64 BrkBase = USER_CODE_VIRT;

    if (!ElfHeaderOk(Hdr, Size, ET_EXEC)) {
        ToyLogMem("elf: bad header\n");
        return -1;
    }
    if (ElfPhdrs(Bytes, Size, Hdr, &Phdrs, &Pn) != 0) {
        ToyLogMem("elf: phdr out of range\n");
        return -1;
    }

    for (i = 0; i < Pn; i++) {
        UINT64 SegEnd;

        if (Phdrs[i].p_type != PT_LOAD) {
            continue;
        }
        if (ElfMapSegment(Space, Bytes, &Phdrs[i], 0) != 0) {
            UINT16 j;

            ToyLogMem("elf: map segment failed\n");
            for (j = 0; j <= i; j++) {
                if (Phdrs[j].p_type == PT_LOAD) {
                    ReleaseLoad(Space, &Phdrs[j], 0);
                }
            }
            return -1;
        }
        SegEnd = Phdrs[i].p_vaddr + Phdrs[i].p_memsz;
        if (SegEnd > BrkBase) {
            BrkBase = SegEnd;
        }
    }

    if (ElfMapStack(Space) != 0) {
        UINT16 j;

        ToyLogMem("elf: map stack failed\n");
        for (j = 0; j < Pn; j++) {
            if (Phdrs[j].p_type == PT_LOAD) {
                ReleaseLoad(Space, &Phdrs[j], 0);
            }
        }
        ReleaseUserRange(Space, USER_STACK_VIRT, USER_STACK_VIRT + USER_STACK_SIZE);
        return -1;
    }

    if (Out) {
        Out->Entry = Hdr->e_entry;
        Out->StackTop = USER_STACK_VIRT + USER_STACK_SIZE;
        Out->BrkBase = BrkBase;
    }
    return 0;
}
