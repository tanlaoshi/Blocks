/*
 * Elf.c — 校验 ELF 并按固定物理地址装入
 *
 * 按每个 PT_LOAD 分别 AllocateAddress（勿再要「Low..High」整跨度）：
 * 内核 .bss 链在 32MiB 后，低端与高位之间有意留洞；整跨度申请必败。
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
 * 只检查，不分配内核页。写出装载下界（最小 PAddress）供日志；页数仅为兼容旧调用方。
 */
EFI_STATUS CheckKernelFile(EFI_PHYSICAL_ADDRESS ElfBase, UINTN FileSize,
                           EFI_PHYSICAL_ADDRESS *LoadLow, UINTN *PageCount) {
    ELF_HEADER_64 *Hdr;
    PROGRAM_HEADER_64 *PHead;
    EFI_PHYSICAL_ADDRESS Low = 0xFFFFFFFFFFFFFFFFULL;
    EFI_PHYSICAL_ADDRESS High = 0;
    UINTN i;
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

        if (Span > (~(UINT64)0 - 0xFFFULL)) {
            BootSerialPrintf("Boot: Page Count Wrap\n");
            return EFI_UNSUPPORTED;
        }
        *PageCount = (UINTN)((Span + 0xFFFULL) >> 12);
        if (*PageCount == 0 || (UINT64)*PageCount != ((Span + 0xFFFULL) >> 12)) {
            BootSerialPrintf("Boot: Page Count Wrap\n");
            return EFI_UNSUPPORTED;
        }
    }

    *LoadLow = Low;
    return EFI_SUCCESS;
}

STATIC EFI_STATUS RelocateElfBufIfNeeded(EFI_PHYSICAL_ADDRESS *ElfBase, UINTN FileSize,
                                         EFI_PHYSICAL_ADDRESS LoadLow,
                                         EFI_PHYSICAL_ADDRESS DestEnd) {
    EFI_PHYSICAL_ADDRESS Buf;
    EFI_PHYSICAL_ADDRESS NewBuf;
    EFI_STATUS Status;
    UINTN FilePages;

    Buf = *ElfBase;
    FilePages = (FileSize >> 12) + 1;
    if (!(Buf < DestEnd && (Buf + ((EFI_PHYSICAL_ADDRESS)FilePages << 12)) > LoadLow)) {
        return EFI_SUCCESS;
    }

    NewBuf = DestEnd;
    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, FilePages, &NewBuf);
    if (EFI_ERROR(Status)) {
        NewBuf = 0;
        Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, FilePages, &NewBuf);
    }
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: Relocate ElfBuf Failed: %r\n", Status);
        return Status;
    }
    if (NewBuf < DestEnd && (NewBuf + ((EFI_PHYSICAL_ADDRESS)FilePages << 12)) > LoadLow) {
        BootSerialPrintf("Boot: Relocate ElfBuf Still Overlaps 0x%lx\n", NewBuf);
        gBS->FreePages(NewBuf, FilePages);
        return EFI_OUT_OF_RESOURCES;
    }
    CopyMem((VOID *)(UINTN)NewBuf, (VOID *)(UINTN)Buf, FileSize);
    gBS->FreePages(Buf, FilePages);
    *ElfBase = NewBuf;
    BootSerialPrintf("Boot: ElfBuf Relocated To 0x%lx\n", NewBuf);
    return EFI_SUCCESS;
}

/*
 * 谁调用：BootLoadKernel，且只在 CheckKernelFile 成功之后。
 * 每个 PT_LOAD 钉在自己的 PAddress；段间空洞不申请。
 */
EFI_STATUS LoadKernelFile(EFI_PHYSICAL_ADDRESS *ElfBase, UINTN FileSize,
                          EFI_PHYSICAL_ADDRESS LoadLow, UINTN PageCount,
                          EFI_PHYSICAL_ADDRESS *EntryPoint) {
    ELF_HEADER_64 *Hdr;
    PROGRAM_HEADER_64 *PHead;
    EFI_PHYSICAL_ADDRESS Buf;
    EFI_PHYSICAL_ADDRESS DestEnd;
    EFI_STATUS Status;
    UINTN i;

    BootSerialPrintf("Boot: LoadKernelFile\n");

    if (ElfBase == NULL || EntryPoint == NULL || PageCount == 0 || FileSize == 0) {
        return EFI_INVALID_PARAMETER;
    }

    DestEnd = LoadLow + ((EFI_PHYSICAL_ADDRESS)PageCount << 12);
    Status = RelocateElfBufIfNeeded(ElfBase, FileSize, LoadLow, DestEnd);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    Buf = *ElfBase;

    Hdr = (ELF_HEADER_64 *)(UINTN)Buf;
    PHead = (PROGRAM_HEADER_64 *)(UINTN)(Buf + Hdr->Phoff);
    for (i = 0; i < Hdr->PHeadCount; i++) {
        PROGRAM_HEADER_64 *Ph = (PROGRAM_HEADER_64 *)((UINT8 *)PHead + i * Hdr->PHeadSize);
        EFI_PHYSICAL_ADDRESS SegBase;
        UINTN SegPages;
        UINT64 SegSpan;

        if (Ph->Type != PT_LOAD) {
            continue;
        }
        if (Ph->SizeInMemory == 0) {
            continue;
        }
        SegSpan = Ph->SizeInMemory;
        if (SegSpan > (~(UINT64)0 - 0xFFFULL)) {
            return EFI_UNSUPPORTED;
        }
        SegPages = (UINTN)((SegSpan + 0xFFFULL) >> 12);
        SegBase = Ph->PAddress;
        Status = gBS->AllocatePages(AllocateAddress, EfiLoaderCode, SegPages, &SegBase);
        if (EFI_ERROR(Status)) {
            BootSerialPrintf("Boot: AllocatePages(0x%lx, %lu Pages) Failed: %r\n",
                             Ph->PAddress, (UINT64)SegPages, Status);
            return Status;
        }
        SetMem((VOID *)(UINTN)SegBase, SegPages * 4096, 0);
        if (Ph->SizeInFile > 0) {
            CopyMem((VOID *)(UINTN)Ph->PAddress,
                    (VOID *)(UINTN)(Buf + Ph->Offset), (UINTN)Ph->SizeInFile);
        }
    }

    *EntryPoint = Hdr->Entry;
    BootDbg("Boot: Kernel Loaded, Entry 0x%lx\n", *EntryPoint);
    return EFI_SUCCESS;
}
