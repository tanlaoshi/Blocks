/*
 * Read.c — 按卷找 Kernel.elf 并读入内存
 */
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

STATIC EFI_STATUS OpenKernelOnFs(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
                                 EFI_PHYSICAL_ADDRESS *OutBuffer, UINTN *OutSize) {
    STATIC CHAR16 *Paths[] = {
        L"\\Kernel.elf",
        L"\\KERNEL.ELF",
        L"\\EFI\\Blocks\\Kernel.elf",
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
    if (Status == EFI_BUFFER_TOO_SMALL) {
        /*
         * 文件名比 sizeof(EFI_FILE_INFO)+128 还长时 GetInfo 会返回此码，
         * 并把所需长度写回 InfoSize；按实际大小重来一次即可，不必猜上限。
         */
        gBS->FreePool(FileInfo);
        FileInfo = NULL;
        Status = gBS->AllocatePool(EfiLoaderData, InfoSize, (VOID **)&FileInfo);
        if (!EFI_ERROR(Status)) {
            Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
        }
    }
    if (EFI_ERROR(Status)) {
        if (FileInfo != NULL) {
            gBS->FreePool(FileInfo);
        }
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
            UINTN Total = (UINTN)FileInfo->FileSize;
            UINTN Got = 0;
            UINT8 *Dst = (UINT8 *)(UINTN)*OutBuffer;

            Status = EFI_SUCCESS;
            while (Got < Total) {
                UINTN Chunk = Total - Got;

                if (Chunk > (256 * 1024)) {
                    Chunk = 256 * 1024;
                }
                Status = File->Read(File, &Chunk, Dst + Got);
                if (EFI_ERROR(Status) || Chunk == 0) {
                    break;
                }
                Got += Chunk;
            }
            /*
             * 短读：Read 返回 SUCCESS 但 Chunk==0（簇链比 FileSize 短等）时，
             * 原代码会带着截断的 ELF 继续往下走，最后报成 CheckKernelFile
             * 失败——归因错误。这里显式判定并报出 got/total。
             */
            if (!EFI_ERROR(Status) && Got < Total) {
                BootSerialPrintf("Boot: Kernel Short Read got=0x%lx total=0x%lx\n",
                                 (UINT64)Got, (UINT64)Total);
                Status = EFI_VOLUME_CORRUPTED;
            }
            if (!EFI_ERROR(Status) && OutSize != NULL) {
                *OutSize = Got;
            }
        }
    }

    gBS->FreePool(FileInfo);
    File->Close(File);
    Root->Close(Root);
    return Status;
}

typedef struct {
    EFI_PHYSICAL_ADDRESS Addr;
    UINTN                Size;
} KERNEL_LOAD;

/* 谓词：这块卷上真能装到 Kernel.elf 才算数（顺带把映像读进 Ctx） */
STATIC BOOLEAN KernelPredicate(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs, VOID *Ctx) {
    KERNEL_LOAD *Load = (KERNEL_LOAD *)Ctx;
    EFI_STATUS Status;

    if (Fs == NULL || Load == NULL) {
        return FALSE;
    }
    Load->Addr = 0;
    Load->Size = 0;
    Status = OpenKernelOnFs(Fs, &Load->Addr, &Load->Size);
    return (!EFI_ERROR(Status) && Load->Addr != 0) ? TRUE : FALSE;
}

EFI_STATUS ReadKernelFile(EFI_HANDLE ImageHandle, EFI_PHYSICAL_ADDRESS *OutBuffer,
                                 UINTN *OutSize) {
    EFI_STATUS Status;
    KERNEL_LOAD Load = { 0, 0 };
    EFI_HANDLE Vol = NULL;
    UINTN Pass = 0;

    BootSerialPrintf("Boot: ReadKernelFile\n");

    if (OutSize != NULL) {
        *OutSize = 0;
    }

    /*
     * boot-root-cfg：ROOT.CFG → 同盘 ID → 它盘 ID → 启动卷兜底，
     * 这套顺序只在 BootSelectOsVolume 里写一次。
     */
    Status = BootSelectOsVolume(ImageHandle, KernelPredicate, &Load, &Vol, &Pass);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    if (Pass == 0) {
        BootDbg("Boot: Kernel.elf From ROOT.CFG\n");
    } else if (Pass == 1) {
        BootDbg("Boot: Kernel.elf From Same-Disk BLOCKS\n");
    } else if (Pass == 2) {
        BootDbg("Boot: Kernel.elf From Other-Disk BLOCKS\n");
    } else {
        BootDbg("Boot: Kernel.elf From Boot Volume\n");
    }

    if (OutBuffer != NULL) {
        *OutBuffer = Load.Addr;
    }
    if (OutSize != NULL) {
        *OutSize = Load.Size;
    }
    return EFI_SUCCESS;
}
