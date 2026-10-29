/*
 * Read.c — 按卷找 Kernel.elf 并读入内存
 */
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

/*
 * BootReadWholeFile — 整读一个文件（PR-12）
 *
 * 原来 Read.c 与 Theme.c 各写一份「开卷→开文件→GetInfo→分配→读→清理」，
 * 错误码与上限还不同（一份无上限、一份 8192）。统一到这里：
 *   UsePages=TRUE  → AllocatePages，返回可直接交给内核的物理缓冲；
 *   UsePages=FALSE → AllocatePool，多分配 1 字节并补 NUL（文本配置用）。
 *   MaxSize=0      → 不限制。
 *
 * 两个细节都来自踩过的坑：
 *   - GetInfo 的文件名缓冲可能不够（EFI_BUFFER_TOO_SMALL，PR-02），按回填
 *     的 InfoSize 重来一次，不猜上限；
 *   - Read 返回 SUCCESS 但读到的比 FileSize 少（簇链比目录项短，PR-05），
 *     显式报 short read 并返回 EFI_VOLUME_CORRUPTED，别把截断数据当成功。
 */
EFI_STATUS BootReadWholeFile(EFI_FILE_PROTOCOL *Root, CONST CHAR16 *Path,
                             UINTN MaxSize, BOOLEAN UsePages,
                             CHAR8 **OutBuf, UINTN *OutSize) {
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *FileInfo = NULL;
    UINTN InfoSize;
    UINTN Total;
    UINTN Got;
    CHAR8 *Buf = NULL;

    if (Root == NULL || Path == NULL || OutBuf == NULL || OutSize == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *OutBuf = NULL;
    *OutSize = 0;

    Status = Root->Open(Root, &File, (CHAR16 *)(UINTN)Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status) || File == NULL) {
        return EFI_NOT_FOUND;
    }

    InfoSize = sizeof(EFI_FILE_INFO) + 128;
    Status = gBS->AllocatePool(EfiLoaderData, InfoSize, (VOID **)&FileInfo);
    if (EFI_ERROR(Status)) {
        File->Close(File);
        return Status;
    }
    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
    if (Status == EFI_BUFFER_TOO_SMALL) {
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
        return Status;
    }
    Total = (UINTN)FileInfo->FileSize;
    gBS->FreePool(FileInfo);

    if (Total == 0 || (MaxSize != 0 && Total > MaxSize)) {
        File->Close(File);
        return EFI_NOT_FOUND;
    }

    if (UsePages) {
        UINTN Pages = (Total >> 12) + 1;

        Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, Pages,
                                    (EFI_PHYSICAL_ADDRESS *)(VOID **)&Buf);
        if (EFI_ERROR(Status)) {
            File->Close(File);
            return Status;
        }
    } else {
        Status = gBS->AllocatePool(EfiLoaderData, Total + 1, (VOID **)&Buf);
        if (EFI_ERROR(Status)) {
            File->Close(File);
            return Status;
        }
        Buf[Total] = 0;
    }

    Got = 0;
    Status = EFI_SUCCESS;
    while (Got < Total) {
        UINTN Chunk = Total - Got;

        if (Chunk > (256 * 1024)) {
            Chunk = 256 * 1024;
        }
        Status = File->Read(File, &Chunk, (VOID *)(Buf + Got));
        if (EFI_ERROR(Status) || Chunk == 0) {
            break;
        }
        Got += Chunk;
    }
    if (!EFI_ERROR(Status) && Got < Total) {
        BootSerialPrintf("Boot: Short Read got=0x%lx total=0x%lx\n",
                         (UINT64)Got, (UINT64)Total);
        Status = EFI_VOLUME_CORRUPTED;
    }
    if (EFI_ERROR(Status)) {
        if (UsePages) {
            gBS->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)Buf,
                           (Total >> 12) + 1);
        } else {
            gBS->FreePool(Buf);
        }
        File->Close(File);
        return Status;
    }

    File->Close(File);
    *OutBuf = Buf;
    *OutSize = Got;
    return EFI_SUCCESS;
}

STATIC EFI_STATUS OpenKernelOnFs(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
                                 EFI_PHYSICAL_ADDRESS *OutBuffer, UINTN *OutSize) {
    STATIC CHAR16 *Paths[] = {
        L"\\Kernel.elf",
        L"\\KERNEL.ELF",
        L"\\EFI\\Blocks\\Kernel.elf",
    };
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root = NULL;
    UINTN p;
    CHAR8 *Buf = NULL;
    UINTN Size = 0;
    EFI_STATUS Last = EFI_NOT_FOUND;

    if (OutSize != NULL) {
        *OutSize = 0;
    }
    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    for (p = 0; p < sizeof(Paths) / sizeof(Paths[0]); p++) {
        Status = BootReadWholeFile(Root, Paths[p], 0, TRUE, &Buf, &Size);
        if (!EFI_ERROR(Status)) {
            Root->Close(Root);
            *OutBuffer = (EFI_PHYSICAL_ADDRESS)(UINTN)Buf;
            if (OutSize != NULL) {
                *OutSize = Size;
            }
            return EFI_SUCCESS;
        }
        Last = Status;
    }
    Root->Close(Root);
    return Last;
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
