/*
 * LoadKernel.c — 找盘读 Kernel.elf，校验并装入内存
 *
 * Boot.c 第三步。含：按卷查找、TOYOS.ID 优先、ELF 段加载。
 */
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include "BootPrivate.h"

STATIC EFI_STATUS OpenKernelOnFs(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
                                 EFI_PHYSICAL_ADDRESS *OutBuffer, UINTN *OutSize) {
    STATIC CHAR16 *Paths[] = {
        L"\\Kernel.elf",
        L"\\KERNEL.ELF",
        L"\\EFI\\ToyOS\\Kernel.elf",
    };
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *FileInfo = NULL;
    UINTN InfoSize;
    UINTN p;

    if (OutSize != NULL) {
        *OutSize = 0;
    }

    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    for (p = 0; p < sizeof(Paths) / sizeof(Paths[0]); p++) {
        Status = Root->Open(Root, &File, Paths[p], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(Status)) {
            break;
        }
        File = NULL;
    }
    if (File == NULL) {
        Root->Close(Root);
        return EFI_NOT_FOUND;
    }

    InfoSize = sizeof(EFI_FILE_INFO) + 128;
    Status = gBS->AllocatePool(EfiLoaderData, InfoSize, (VOID **)&FileInfo);
    if (EFI_ERROR(Status)) {
        File->Close(File);
        Root->Close(Root);
        return Status;
    }

    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
    if (EFI_ERROR(Status)) {
        gBS->FreePool(FileInfo);
        File->Close(File);
        Root->Close(Root);
        return Status;
    }

    {
        UINTN FilePageSize = ((UINTN)FileInfo->FileSize >> 12) + 1;
        Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, FilePageSize, OutBuffer);
        if (EFI_ERROR(Status)) {
            gBS->FreePool(FileInfo);
            File->Close(File);
            Root->Close(Root);
            return Status;
        }

        {
            UINTN ReadSize = (UINTN)FileInfo->FileSize;
            Status = File->Read(File, &ReadSize, (VOID *)(UINTN)*OutBuffer);
            if (!EFI_ERROR(Status) && OutSize != NULL) {
                *OutSize = ReadSize;
            }
        }
    }

    gBS->FreePool(FileInfo);
    File->Close(File);
    Root->Close(Root);
    return Status;
}

EFI_STATUS ReadKernelFile(EFI_HANDLE ImageHandle, EFI_PHYSICAL_ADDRESS *OutBuffer,
                                 UINTN *OutSize) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    UINTN HandleCount = 0;
    EFI_HANDLE *Handles = NULL;
    UINTN i;
    UINTN Pass;

    BootSerialPrintf("Boot: ReadKernelFile\n");

    if (OutSize != NULL) {
        *OutSize = 0;
    }

    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                                 (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                     NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status)) {
        Handles = NULL;
        HandleCount = 0;
    }

    /*
     * 双盘布局：必须先读带 TOYOS.ID 的系统盘（disk1/rootfs），避免启动盘旧
     * Kernel.elf 抢先加载。Pass0=TOYOS；Pass1=其它非启动卷；Pass2=启动卷兜底。
     */
    for (Pass = 0; Pass < 3; Pass++) {
        for (i = 0; i < HandleCount; i++) {
            BOOLEAN IsBoot = (Handles[i] == LoadedImage->DeviceHandle);
            BOOLEAN IsToyOs;

            Status = gBS->HandleProtocol(Handles[i], &gEfiSimpleFileSystemProtocolGuid,
                                         (VOID **)&Fs);
            if (EFI_ERROR(Status)) {
                continue;
            }
            IsToyOs = FsHasToyOsId(Fs);
            if (Pass == 0) {
                if (!IsToyOs) {
                    continue;
                }
            } else if (Pass == 1) {
                if (IsBoot) {
                    continue;
                }
            } else {
                if (!IsBoot) {
                    continue;
                }
            }

            Status = OpenKernelOnFs(Fs, OutBuffer, OutSize);
            if (!EFI_ERROR(Status)) {
                if (Pass == 0) {
                    BootDbg("ToyBoot: Kernel.elf From TOYOS Volume\n");
                } else if (Pass == 1) {
                    BootDbg("ToyBoot: Kernel.elf From Secondary Volume\n");
                } else {
                    BootDbg("ToyBoot: Kernel.elf From Boot Volume\n");
                }
                if (Handles != NULL) {
                    gBS->FreePool(Handles);
                }
                return EFI_SUCCESS;
            }
        }
    }

    if (Handles != NULL) {
        gBS->FreePool(Handles);
    }
    BootSerialPrintf("ToyBoot: Kernel.elf Not Found On Any Volume\n");
    return EFI_NOT_FOUND;
}

EFI_STATUS BootLoadKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig) {
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS ElfBuffer = 0;
    UINTN ElfSize = 0;

    Status = ReadKernelFile(ImageHandle, &ElfBuffer, &ElfSize);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: ReadKernelFile Failed: %r\n", Status);
        return Status;
    }

    BootSerialPrintf("Boot: Loading Kernel ELF...\n");
    Status = CheckAndLoadKernel(ElfBuffer, ElfSize, &BootConfig->EntryAddress);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: CheckAndLoadKernel Failed: %r\n", Status);
        return Status;
    }
    BootSerialPrintf("Boot: Kernel Loaded, Entry=0x%lx\n", BootConfig->EntryAddress);
    return EFI_SUCCESS;
}

/* --- ELF 装载 --- */
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

EFI_STATUS CheckAndLoadKernel(EFI_PHYSICAL_ADDRESS ElfBase, UINTN FileSize,
                                      EFI_PHYSICAL_ADDRESS *EntryPoint) {
    ELF_HEADER_64 *Hdr;
    PROGRAM_HEADER_64 *PHead;
    EFI_PHYSICAL_ADDRESS Low = 0xFFFFFFFFFFFFFFFFULL;
    EFI_PHYSICAL_ADDRESS High = 0;
    UINTN i;
    UINTN PageCount;
    EFI_PHYSICAL_ADDRESS LoadBase;
    EFI_STATUS Status;
    UINT64 PhEnd;

    BootSerialPrintf("Boot: CheckAndLoadKernel Size=%lu\n", (UINT64)FileSize);

    if (FileSize < sizeof(ELF_HEADER_64)) {
        return EFI_UNSUPPORTED;
    }
    if (*(UINT32 *)(UINTN)ElfBase != 0x464C457FU || *(UINT8 *)(UINTN)(ElfBase + 4) != 2) {
        return EFI_UNSUPPORTED;
    }

    Hdr = (ELF_HEADER_64 *)(UINTN)ElfBase;
    if (Hdr->Machine != EM_X86_64) {
        BootSerialPrintf("ToyBoot: Bad ELF Machine 0x%x\n", Hdr->Machine);
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
            BootSerialPrintf("ToyBoot: PT_LOAD Out Of File\n");
            return EFI_UNSUPPORTED;
        }
        SegEnd = Ph->PAddress + Ph->SizeInMemory;
        if (SegEnd < Ph->PAddress) {
            BootSerialPrintf("ToyBoot: PT_LOAD Address Wrap\n");
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

    /* Kernel.elf @0x100000，-fno-pie：必须按 PhysAddr 固定加载 */
    {
        UINT64 Span = High - Low;

        /* ceil(Span/4096)；防 Span 过大导致 PageCount 回绕 */
        if (Span > (~(UINT64)0 - 0xFFFULL)) {
            BootSerialPrintf("ToyBoot: Page Count Wrap\n");
            return EFI_UNSUPPORTED;
        }
        PageCount = (UINTN)((Span + 0xFFFULL) >> 12);
        if (PageCount == 0 || (UINT64)PageCount != ((Span + 0xFFFULL) >> 12)) {
            BootSerialPrintf("ToyBoot: Page Count Wrap\n");
            return EFI_UNSUPPORTED;
        }
    }
    LoadBase = Low;
    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderCode, PageCount, &LoadBase);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("AllocatePages(0x%lx, %lu Pages) Failed: %r\n", Low, PageCount, Status);
        return Status;
    }

    SetMem((VOID *)(UINTN)LoadBase, PageCount * 4096, 0);

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
    BootDbg("Kernel Loaded At 0x%lx, Entry 0x%lx\n", LoadBase, *EntryPoint);
    return EFI_SUCCESS;
}
