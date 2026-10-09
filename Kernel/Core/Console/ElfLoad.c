/*
 * ElfLoad.c — K19：静态 ET_EXEC 最小装载
 *
 * 校验 ELF64 → 拷 PT_LOAD 到 p_vaddr（须落在 4GiB 恒等窗）。
 */
#include "ElfLoad.h"
#include "PhysicalMemory.h"

#define EI_MAG0 0
#define ELFMAG0 0x7Fu
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

static void MemCpy(UINT8 *D, const UINT8 *S, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

static void MemZero(UINT8 *D, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        D[i] = 0;
    }
}

int ElfLoadFromMemory(const void *Image, UINTN Size, ELF_IMAGE *Out) {
    const UINT8 *Base = (const UINT8 *)Image;
    const Elf64_Ehdr *Eh;
    const Elf64_Phdr *Ph;
    UINT16 i;
    void *Stack;

    if (Image == 0 || Out == 0 || Size < sizeof(Elf64_Ehdr)) {
        return -1;
    }
    Eh = (const Elf64_Ehdr *)Base;
    if (Eh->e_ident[EI_MAG0] != ELFMAG0 || Eh->e_ident[1] != ELFMAG1 ||
        Eh->e_ident[2] != ELFMAG2 || Eh->e_ident[3] != ELFMAG3) {
        return -1;
    }
    if (Eh->e_ident[4] != ELFCLASS64 || Eh->e_ident[5] != ELFDATA2LSB) {
        return -1;
    }
    if (Eh->e_type != ET_EXEC || Eh->e_machine != EM_X86_64) {
        return -1;
    }
    if (Eh->e_phoff + (UINT64)Eh->e_phnum * Eh->e_phentsize > Size) {
        return -1;
    }

    Ph = (const Elf64_Phdr *)(Base + Eh->e_phoff);
    for (i = 0; i < Eh->e_phnum; i++) {
        const Elf64_Phdr *P = (const Elf64_Phdr *)((const UINT8 *)Ph +
                                                   (UINTN)i * Eh->e_phentsize);
        UINT8 *Dst;
        if (P->p_type != PT_LOAD) {
            continue;
        }
        if (P->p_vaddr + P->p_memsz > 0x100000000ull) {
            return -1; /* 窗外 */
        }
        if (P->p_offset + P->p_filesz > Size) {
            return -1;
        }
        Dst = (UINT8 *)(UINTN)P->p_vaddr;
        MemCpy(Dst, Base + P->p_offset, (UINTN)P->p_filesz);
        if (P->p_memsz > P->p_filesz) {
            MemZero(Dst + P->p_filesz, (UINTN)(P->p_memsz - P->p_filesz));
        }
    }

    Stack = PhysicalMemoryAllocatePages(2);
    if (Stack == 0) {
        return -1;
    }
    MemZero((UINT8 *)Stack, 8192u);
    Out->Entry = Eh->e_entry;
    Out->StackTop = (UINT64)(UINTN)Stack + 8192u;
    return 0;
}
