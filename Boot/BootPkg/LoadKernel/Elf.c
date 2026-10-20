/*
 * Elf.c — 校验 ELF 并按固定物理地址装入
 */
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include "BootPrivate.h"

#define PT_LOAD 1
#define EM_X86_64 0x3E

#pragma pack(1)
typedef struct {
    UINT32 Magic; UINT8 Format; UINT8 Endianness; UINT8 Version; UINT8 OSAbi;
    UINT8 AbiVersion; UINT8 Reserved[7]; UINT16 Type; UINT16 Machine;
    UINT32 ElfVersion; UINT64 Entry; UINT64 Phoff; UINT64 Shoff; UINT32 Flags;
    UINT16 HeadSize; UINT16 PHeadSize; UINT16 PHeadCount; UINT16 SHeadSize;
    UINT16 SHeadCount; UINT16 SNameIndex;
} ELF_HEADER_64;

typedef struct {
    UINT32 Type; UINT32 Flags; UINT64 Offset; UINT64 VAddress; UINT64 PAddress;
    UINT64 SizeInFile; UINT64 SizeInMemory; UINT64 Align;
} PROGRAM_HEADER_64;
#pragma pack()

/*
 * 谁调用：BootLoadKernel，文件已读入内存之后。
 * 只检查，不分配内核页。通过时写出装载下界和页数，交给 LoadKernelFile。
 */
EFI_STATUS CheckKernelFile(EFI_PHYSICAL_ADDRESS ElfBase, UINTN FileSize,
                           EFI_PHYSICAL_ADDRESS *LoadLow, UINTN *PageCount) {
    ELF_HEADER_64 *Hdr;
    PROGRAM_HEADER_64 *PHead;
    EFI_PHYSICAL_ADDRESS Low = 0xFFFFFFFFFFFFFFFFULL;
    EFI_PHYSICAL_ADDRESS High = 0;
    UINTN i;
    UINTN Pages;
    UINT64 PhEnd;

    BootSerialPrintf("Boot: CheckKernelFile Size=%lu\n", (UINT64)FileSize);

    if (LoadLow == NULL || PageCount == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (FileSize < sizeof(ELF_HEADER_64)) {
        return EFI_UNSUPPORTED;
    }
    if (*(UINT32 *)(UINTN)ElfBase != 0x464C457FU || *(UINT8 *)(UINTN)(ElfBase + 4) != 2) {
        return EFI_UNSUPPORTED;
    }

    Hdr = (ELF_HEADER_64 *)(UINTN)ElfBase;
    if (Hdr->Machine != EM_X86_64) {
        BootSerialPrintf("Boot: Bad ELF Machine 0x%x\n", Hdr->Machine);
        return EFI_UNSUPPORTED;
    }
    if (Hdr->PHeadSize < sizeof(PROGRAM_HEADER_64) || Hdr->PHeadCount == 0) {
        return EFI_UNSUPPORTED;
    }
    if (Hdr->Phoff >= FileSize) {
        return EFI_UNSUPPORTED;
    }
    PhEnd = Hdr->Phoff + (UINT64)Hdr->PHeadCount * (UINT64)Hdr->PHeadSize;
    if (PhEnd > FileSize || PhEnd < Hdr->Phoff) {
        return EFI_UNSUPPORTED;
    }

    PHead = (PROGRAM_HEADER_64 *)(UINTN)(ElfBase + Hdr->Phoff);
    for (i = 0; i < Hdr->PHeadCount; i++) {
        PROGRAM_HEADER_64 *Ph = (PROGRAM_HEADER_64 *)((UINT8 *)PHead + i * Hdr->PHeadSize);
        UINT64 SegEnd;

        if (Ph->Type != PT_LOAD) {
            continue;
        }
        if (Ph->Offset >= FileSize || Ph->SizeInFile > FileSize - Ph->Offset) {
            BootSerialPrintf("Boot: PT_LOAD Out Of File\n");
            return EFI_UNSUPPORTED;
        }
        SegEnd = Ph->PAddress + Ph->SizeInMemory;
        if (SegEnd < Ph->PAddress) {
            BootSerialPrintf("Boot: PT_LOAD Address Wrap\n");
            return EFI_UNSUPPORTED;
        }
        if (Low > Ph->PAddress) {
            Low = Ph->PAddress;
        }
        if (High < SegEnd) {
            High = SegEnd;
        }
    }
    if (High <= Low) {
        return EFI_UNSUPPORTED;
    }

    {
        UINT64 Span = High - Low;

        /* ceil(Span/4096)；防 Span 过大导致页数回绕 */
        if (Span > (~(UINT64)0 - 0xFFFULL)) {
            BootSerialPrintf("Boot: Page Count Wrap\n");
            return EFI_UNSUPPORTED;
        }
        Pages = (UINTN)((Span + 0xFFFULL) >> 12);
        if (Pages == 0 || (UINT64)Pages != ((Span + 0xFFFULL) >> 12)) {
            BootSerialPrintf("Boot: Page Count Wrap\n");
            return EFI_UNSUPPORTED;
        }
    }

    *LoadLow = Low;
    *PageCount = Pages;
    return EFI_SUCCESS;
}

/*
 * 谁调用：BootLoadKernel，且只在 CheckKernelFile 成功之后。
 * Kernel.elf 链在固定物理地址（-fno-pie），所以按 LoadLow 申请页，再拷贝 PT_LOAD。
 */
EFI_STATUS LoadKernelFile(EFI_PHYSICAL_ADDRESS ElfBase, EFI_PHYSICAL_ADDRESS LoadLow,
                          UINTN PageCount, EFI_PHYSICAL_ADDRESS *EntryPoint) {
    ELF_HEADER_64 *Hdr;
    PROGRAM_HEADER_64 *PHead;
    EFI_PHYSICAL_ADDRESS LoadBase;
    EFI_STATUS Status;
    UINTN i;

    BootSerialPrintf("Boot: LoadKernelFile\n");

    if (EntryPoint == NULL || PageCount == 0) {
        return EFI_INVALID_PARAMETER;
    }

    LoadBase = LoadLow;
    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderCode, PageCount, &LoadBase);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: AllocatePages(0x%lx, %lu Pages) Failed: %r\n",
                         LoadLow, PageCount, Status);
        return Status;
    }

    SetMem((VOID *)(UINTN)LoadBase, PageCount * 4096, 0);

    Hdr = (ELF_HEADER_64 *)(UINTN)ElfBase;
    PHead = (PROGRAM_HEADER_64 *)(UINTN)(ElfBase + Hdr->Phoff);
    for (i = 0; i < Hdr->PHeadCount; i++) {
        PROGRAM_HEADER_64 *Ph = (PROGRAM_HEADER_64 *)((UINT8 *)PHead + i * Hdr->PHeadSize);
        if (Ph->Type != PT_LOAD) {
            continue;
        }
        CopyMem((VOID *)(UINTN)Ph->PAddress,
                (VOID *)(UINTN)(ElfBase + Ph->Offset), (UINTN)Ph->SizeInFile);
        if (Ph->SizeInMemory > Ph->SizeInFile) {
            SetMem((VOID *)(UINTN)(Ph->PAddress + Ph->SizeInFile),
                   (UINTN)(Ph->SizeInMemory - Ph->SizeInFile), 0);
        }
    }

    *EntryPoint = Hdr->Entry;
    BootDbg("Boot: Kernel Loaded At 0x%lx, Entry 0x%lx\n", LoadBase, *EntryPoint);
    return EFI_SUCCESS;
}
