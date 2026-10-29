/*
 * Volume.c — 卷标记、启动盘 GUID、ROOT.CFG 三档认卷（boot-root-cfg）
 *
 * 供 Video/Theme 读 BLOCKS.DB、LoadKernel/ 找系统盘共用。
 * 认卷序：ESP \\EFI\\Blocks\\ROOT.CFG → 同盘 BLOCKS.ID → 它盘 BLOCKS.ID。
 */
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <IndustryStandard/Mbr.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

/* Settings 写在挂载的 BLOCKS 系统盘；ESP/启动盘上的 THEME.CFG 可能是旧的 */
BOOLEAN FsHasOsMarker(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs) {
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;

    if (Fs == NULL) {
        return FALSE;
    }
    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status)) {
        return FALSE;
    }
    Status = Root->Open(Root, &File, L"\\BLOCKS.ID", EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(Status) && File != NULL) {
        File->Close(File);
        Root->Close(Root);
        return TRUE;
    }
    Root->Close(Root);
    return FALSE;
}

STATIC BOOLEAN DevicePathPrefix(EFI_DEVICE_PATH_PROTOCOL *Prefix,
                                EFI_DEVICE_PATH_PROTOCOL *Full) {
    while (Prefix != NULL && !IsDevicePathEnd(Prefix)) {
        UINTN N;

        if (Full == NULL || IsDevicePathEnd(Full)) {
            return FALSE;
        }
        N = DevicePathNodeLength(Prefix);
        if (N < 4 || DevicePathNodeLength(Full) != N) {
            return FALSE;
        }
        if (CompareMem(Prefix, Full, N) != 0) {
            return FALSE;
        }
        Prefix = NextDevicePathNode(Prefix);
        Full = NextDevicePathNode(Full);
    }
    return TRUE;
}

/* 分区句柄路径在硬盘节点处分叉；节点之前相同就是同一块物理盘。 */
BOOLEAN BootSamePhysicalDisk(EFI_HANDLE A, EFI_HANDLE B) {
    EFI_DEVICE_PATH_PROTOCOL *Pa;
    EFI_DEVICE_PATH_PROTOCOL *Pb;

    Pa = DevicePathFromHandle(A);
    Pb = DevicePathFromHandle(B);
    if (Pa == NULL || Pb == NULL) {
        return FALSE;
    }
    while (!IsDevicePathEnd(Pa) && !IsDevicePathEnd(Pb)) {
        UINTN N;
        BOOLEAN PaPart;
        BOOLEAN PbPart;

        PaPart = (DevicePathType(Pa) == MEDIA_DEVICE_PATH &&
                  (DevicePathSubType(Pa) == MEDIA_HARDDRIVE_DP ||
                   DevicePathSubType(Pa) == MEDIA_CDROM_DP ||
                   DevicePathSubType(Pa) == MEDIA_FILEPATH_DP));
        PbPart = (DevicePathType(Pb) == MEDIA_DEVICE_PATH &&
                  (DevicePathSubType(Pb) == MEDIA_HARDDRIVE_DP ||
                   DevicePathSubType(Pb) == MEDIA_CDROM_DP ||
                   DevicePathSubType(Pb) == MEDIA_FILEPATH_DP));
        if (PaPart || PbPart) {
            return TRUE;
        }
        N = DevicePathNodeLength(Pa);
        if (N < 4 || DevicePathNodeLength(Pb) != N || CompareMem(Pa, Pb, N) != 0) {
            return FALSE;
        }
        Pa = NextDevicePathNode(Pa);
        Pb = NextDevicePathNode(Pb);
    }
    return TRUE;
}

VOID BootFillDiskGuid(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_DEVICE_PATH_PROTOCOL *VolPath;
    EFI_HANDLE *Handles = NULL;
    UINTN Count = 0;
    UINTN i;

    if (BootConfig == NULL) {
        return;
    }
    BootConfig->OsIdPad = 0;
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                                 (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || LoadedImage == NULL) {
        return;
    }
    VolPath = DevicePathFromHandle(LoadedImage->DeviceHandle);
    if (VolPath == NULL) {
        return;
    }
    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid,
                                     NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || Handles == NULL) {
        return;
    }
    for (i = 0; i < Count; i++) {
        EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
        EFI_DEVICE_PATH_PROTOCOL *DiskPath;
        UINT8 *Sector = NULL;
        UINT32 Block;

        Status = gBS->HandleProtocol(Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
        if (EFI_ERROR(Status) || Bio == NULL || Bio->Media == NULL ||
            Bio->Media->LogicalPartition) {
            continue;
        }
        DiskPath = DevicePathFromHandle(Handles[i]);
        if (DiskPath == NULL || !DevicePathPrefix(DiskPath, VolPath)) {
            continue;
        }
        Block = Bio->Media->BlockSize;
        if (Block < 512 || Block > 8192) {
            continue;
        }
        Status = gBS->AllocatePool(EfiLoaderData, Block, (VOID **)&Sector);
        if (EFI_ERROR(Status) || Sector == NULL) {
            continue;
        }
        Status = Bio->ReadBlocks(Bio, Bio->Media->MediaId, 1, Block, Sector);
        if (!EFI_ERROR(Status) &&
            Sector[0] == 'E' && Sector[1] == 'F' && Sector[2] == 'I' &&
            Sector[3] == ' ' && Sector[4] == 'P' && Sector[5] == 'A' &&
            Sector[6] == 'R' && Sector[7] == 'T') {
            CopyMem(BootConfig->BootDiskGuid, Sector + 56, 16);
            BootConfig->OsIdPad = UEFI_BOOT_DISK_GUID_MAGIC;
            gBS->FreePool(Sector);
            break;
        }
        gBS->FreePool(Sector);
    }
    gBS->FreePool(Handles);
}

/* EFI_GUID 混端 → 小写 UUID 串（与 Linux lsblk PTUUID/PARTUUID 一致） */
STATIC VOID GuidBinToUuid(CONST UINT8 *Bin, CHAR8 *Out) {
    CONST EFI_GUID *G = (CONST EFI_GUID *)(CONST VOID *)Bin;
    CONST UINT8 *D4 = G->Data4;
    UINTN i = 0;
    UINT32 D1 = G->Data1;
    UINT16 D2 = G->Data2;
    UINT16 D3 = G->Data3;
    STATIC CONST CHAR8 Hex[] = "0123456789abcdef";

    Out[i++] = Hex[(D1 >> 28) & 0xF];
    Out[i++] = Hex[(D1 >> 24) & 0xF];
    Out[i++] = Hex[(D1 >> 20) & 0xF];
    Out[i++] = Hex[(D1 >> 16) & 0xF];
    Out[i++] = Hex[(D1 >> 12) & 0xF];
    Out[i++] = Hex[(D1 >> 8) & 0xF];
    Out[i++] = Hex[(D1 >> 4) & 0xF];
    Out[i++] = Hex[D1 & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(D2 >> 12) & 0xF];
    Out[i++] = Hex[(D2 >> 8) & 0xF];
    Out[i++] = Hex[(D2 >> 4) & 0xF];
    Out[i++] = Hex[D2 & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(D3 >> 12) & 0xF];
    Out[i++] = Hex[(D3 >> 8) & 0xF];
    Out[i++] = Hex[(D3 >> 4) & 0xF];
    Out[i++] = Hex[D3 & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(D4[0] >> 4) & 0xF];
    Out[i++] = Hex[D4[0] & 0xF];
    Out[i++] = Hex[(D4[1] >> 4) & 0xF];
    Out[i++] = Hex[D4[1] & 0xF];
    Out[i++] = '-';
    Out[i++] = Hex[(D4[2] >> 4) & 0xF];
    Out[i++] = Hex[D4[2] & 0xF];
    Out[i++] = Hex[(D4[3] >> 4) & 0xF];
    Out[i++] = Hex[D4[3] & 0xF];
    Out[i++] = Hex[(D4[4] >> 4) & 0xF];
    Out[i++] = Hex[D4[4] & 0xF];
    Out[i++] = Hex[(D4[5] >> 4) & 0xF];
    Out[i++] = Hex[D4[5] & 0xF];
    Out[i++] = Hex[(D4[6] >> 4) & 0xF];
    Out[i++] = Hex[D4[6] & 0xF];
    Out[i++] = Hex[(D4[7] >> 4) & 0xF];
    Out[i++] = Hex[D4[7] & 0xF];
    Out[i] = 0;
}

STATIC CHAR8 AsciiLower(CHAR8 C) {
    if (C >= 'A' && C <= 'Z') {
        return (CHAR8)(C - 'A' + 'a');
    }
    return C;
}

STATIC BOOLEAN UuidStrEq(CONST CHAR8 *A, CONST CHAR8 *B) {
    UINTN i;

    if (A == NULL || B == NULL) {
        return FALSE;
    }
    for (i = 0; i < 36; i++) {
        if (AsciiLower(A[i]) != AsciiLower(B[i])) {
            return FALSE;
        }
    }
    return A[36] == 0 && B[36] == 0;
}

STATIC BOOLEAN ReadGptDiskGuidForVol(EFI_HANDLE VolHandle, UINT8 *OutGuid) {
    EFI_STATUS Status;
    EFI_DEVICE_PATH_PROTOCOL *VolPath;
    EFI_HANDLE *Handles = NULL;
    UINTN Count = 0;
    UINTN i;

    if (VolHandle == NULL || OutGuid == NULL) {
        return FALSE;
    }
    VolPath = DevicePathFromHandle(VolHandle);
    if (VolPath == NULL) {
        return FALSE;
    }
    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid,
                                     NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || Handles == NULL) {
        return FALSE;
    }
    for (i = 0; i < Count; i++) {
        EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
        EFI_DEVICE_PATH_PROTOCOL *DiskPath;
        UINT8 *Sector = NULL;
        UINT32 Block;

        Status = gBS->HandleProtocol(Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
        if (EFI_ERROR(Status) || Bio == NULL || Bio->Media == NULL ||
            Bio->Media->LogicalPartition) {
            continue;
        }
        DiskPath = DevicePathFromHandle(Handles[i]);
        if (DiskPath == NULL || !DevicePathPrefix(DiskPath, VolPath)) {
            continue;
        }
        Block = Bio->Media->BlockSize;
        if (Block < 512 || Block > 8192) {
            continue;
        }
        Status = gBS->AllocatePool(EfiLoaderData, Block, (VOID **)&Sector);
        if (EFI_ERROR(Status) || Sector == NULL) {
            continue;
        }
        Status = Bio->ReadBlocks(Bio, Bio->Media->MediaId, 1, Block, Sector);
        if (!EFI_ERROR(Status) &&
            Sector[0] == 'E' && Sector[1] == 'F' && Sector[2] == 'I' &&
            Sector[3] == ' ' && Sector[4] == 'P' && Sector[5] == 'A' &&
            Sector[6] == 'R' && Sector[7] == 'T') {
            CopyMem(OutGuid, Sector + 56, 16);
            gBS->FreePool(Sector);
            gBS->FreePool(Handles);
            return TRUE;
        }
        gBS->FreePool(Sector);
    }
    gBS->FreePool(Handles);
    return FALSE;
}

STATIC BOOLEAN ReadPartGuidForVol(EFI_HANDLE VolHandle, UINT8 *OutGuid) {
    EFI_DEVICE_PATH_PROTOCOL *Path;

    if (VolHandle == NULL || OutGuid == NULL) {
        return FALSE;
    }
    Path = DevicePathFromHandle(VolHandle);
    while (Path != NULL && !IsDevicePathEnd(Path)) {
        if (DevicePathType(Path) == MEDIA_DEVICE_PATH &&
            DevicePathSubType(Path) == MEDIA_HARDDRIVE_DP) {
            HARDDRIVE_DEVICE_PATH *Hd = (HARDDRIVE_DEVICE_PATH *)Path;

            if (Hd->SignatureType == SIGNATURE_TYPE_GUID) {
                CopyMem(OutGuid, Hd->Signature, 16);
                return TRUE;
            }
        }
        Path = NextDevicePathNode(Path);
    }
    return FALSE;
}

typedef struct {
    BOOLEAN Ok;
    CHAR8   Disk[40];
    CHAR8   Part[40];
} BOOT_ROOT_CFG;

/* ROOT.CFG 是纯文本，正常几十字节；上限只用于挡住异常文件 */
#define ROOT_CFG_MAX_SIZE 4096

STATIC BOOLEAN LoadRootCfgFromEsp(EFI_HANDLE ImageHandle, BOOT_ROOT_CFG *Out) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *Info = NULL;
    UINTN InfoSize;
    UINTN FileSize;
    CHAR8 *Buf = NULL;
    UINTN Size;
    UINTN i;
    UINTN LineStart;

    if (Out == NULL) {
        return FALSE;
    }
    Out->Ok = FALSE;
    Out->Disk[0] = 0;
    Out->Part[0] = 0;

    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                                 (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || LoadedImage == NULL) {
        return FALSE;
    }
    Status = gBS->HandleProtocol(LoadedImage->DeviceHandle,
                                 &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR(Status) || Fs == NULL) {
        return FALSE;
    }
    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status) || Root == NULL) {
        return FALSE;
    }
    Status = Root->Open(Root, &File, L"\\EFI\\Blocks\\ROOT.CFG", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status) || File == NULL) {
        Root->Close(Root);
        return FALSE;
    }

    /*
     * 先问大小再整读：固定小缓冲会被 Read 静默夹到剩余长度
     * （EFI_FILE_PROTOCOL.Read 在缓冲不足时按剩余量返回成功），
     * disk=/part= 落在后面时就会认错盘，且日志看不出是配置没读到。
     */
    InfoSize = sizeof(EFI_FILE_INFO) + 128;
    Status = gBS->AllocatePool(EfiLoaderData, InfoSize, (VOID **)&Info);
    if (EFI_ERROR(Status)) {
        File->Close(File);
        Root->Close(Root);
        return FALSE;
    }
    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
    if (EFI_ERROR(Status) || Info->FileSize == 0 || Info->FileSize > ROOT_CFG_MAX_SIZE) {
        gBS->FreePool(Info);
        File->Close(File);
        Root->Close(Root);
        return FALSE;
    }
    FileSize = (UINTN)Info->FileSize;
    Status = gBS->AllocatePool(EfiLoaderData, FileSize + 1, (VOID **)&Buf);
    gBS->FreePool(Info);
    if (EFI_ERROR(Status)) {
        File->Close(File);
        Root->Close(Root);
        return FALSE;
    }
    Size = FileSize;
    Status = File->Read(File, &Size, Buf);
    File->Close(File);
    Root->Close(Root);
    if (EFI_ERROR(Status) || Size == 0) {
        gBS->FreePool(Buf);
        return FALSE;
    }
    Buf[Size] = 0;

    LineStart = 0;
    for (i = 0; i <= Size; i++) {
        if (i == Size || Buf[i] == '\n' || Buf[i] == '\r') {
            UINTN Len = i - LineStart;
            CHAR8 *Line = Buf + LineStart;

            if (Len >= 5 + 36 && Line[0] == 'd' && Line[1] == 'i' && Line[2] == 's' &&
                Line[3] == 'k' && Line[4] == '=') {
                CopyMem(Out->Disk, Line + 5, 36);
                Out->Disk[36] = 0;
            } else if (Len >= 5 + 36 && Line[0] == 'p' && Line[1] == 'a' &&
                       Line[2] == 'r' && Line[3] == 't' && Line[4] == '=') {
                CopyMem(Out->Part, Line + 5, 36);
                Out->Part[36] = 0;
            }
            if (i < Size && Buf[i] == '\r' && i + 1 < Size && Buf[i + 1] == '\n') {
                i++;
            }
            LineStart = i + 1;
        }
    }
    if (Out->Disk[0] != 0 && Out->Part[0] != 0) {
        Out->Ok = TRUE;
    }
    gBS->FreePool(Buf);
    return Out->Ok;
}

STATIC BOOLEAN VolMatchesRootCfg(EFI_HANDLE VolHandle, CONST BOOT_ROOT_CFG *Cfg) {
    UINT8 DiskBin[16];
    UINT8 PartBin[16];
    CHAR8 DiskStr[40];
    CHAR8 PartStr[40];

    if (Cfg == NULL || !Cfg->Ok) {
        return FALSE;
    }
    if (!ReadGptDiskGuidForVol(VolHandle, DiskBin) ||
        !ReadPartGuidForVol(VolHandle, PartBin)) {
        return FALSE;
    }
    GuidBinToUuid(DiskBin, DiskStr);
    GuidBinToUuid(PartBin, PartStr);
    return UuidStrEq(DiskStr, Cfg->Disk) && UuidStrEq(PartStr, Cfg->Part);
}

STATIC BOOLEAN BootOsVolForPass(EFI_HANDLE ImageHandle, EFI_HANDLE VolHandle,
                                UINTN Pass) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    STATIC BOOT_ROOT_CFG sRootCfg;
    STATIC BOOLEAN sRootTried;
    BOOLEAN SameDisk;
    BOOLEAN HasId;

    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                                 (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || LoadedImage == NULL || VolHandle == NULL) {
        return FALSE;
    }
    Status = gBS->HandleProtocol(VolHandle, &gEfiSimpleFileSystemProtocolGuid,
                                 (VOID **)&Fs);
    if (EFI_ERROR(Status) || Fs == NULL) {
        return FALSE;
    }

    if (Pass == 3) {
        return VolHandle == LoadedImage->DeviceHandle;
    }

    HasId = FsHasOsMarker(Fs);
    SameDisk = BootSamePhysicalDisk(LoadedImage->DeviceHandle, VolHandle);

    if (Pass == 0) {
        if (!sRootTried) {
            sRootTried = TRUE;
            if (LoadRootCfgFromEsp(ImageHandle, &sRootCfg) && sRootCfg.Ok) {
                BootDbg("Boot: ROOT.CFG disk=%a part=%a\n", sRootCfg.Disk, sRootCfg.Part);
            }
        }
        if (!sRootCfg.Ok || !HasId) {
            return FALSE;
        }
        if (VolMatchesRootCfg(VolHandle, &sRootCfg)) {
            BootDbg("Boot: OS Vol Via ROOT.CFG\n");
            return TRUE;
        }
        return FALSE;
    }
    if (Pass == 1) {
        return HasId && SameDisk;
    }
    if (Pass == 2) {
        return HasId && !SameDisk;
    }
    return FALSE;
}

/*
 * BootSelectOsVolume — 「找系统盘」只写一次。
 *
 * 四段顺序（与历史行为一致）：0 ROOT.CFG 指认的盘 → 1 同盘 + BLOCKS.ID →
 * 2 它盘 + BLOCKS.ID → 3 启动卷兜底。对每个候选先过 Predicate（是否真有
 * Kernel.elf / BLOCKS.DB），第一个命中的写进 OutVol。
 *
 * Predicate 拿到的是已经打开的 Fs，省得调用方再 HandleProtocol 一次；
 * Ctx 由调用方自定义（例如装载缓冲区）。
 */
EFI_STATUS BootSelectOsVolume(EFI_HANDLE ImageHandle, BOOT_VOL_PREDICATE Pred,
                              VOID *Ctx, EFI_HANDLE *OutVol, UINTN *OutPass) {
    EFI_STATUS Status;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    UINTN HandleCount = 0;
    EFI_HANDLE *Handles = NULL;
    UINTN Pass;
    UINTN i;

    if (OutVol == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *OutVol = NULL;

    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                     NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    for (Pass = 0; Pass < 4; Pass++) {
        for (i = 0; i < HandleCount; i++) {
            Fs = NULL;
            Status = gBS->HandleProtocol(Handles[i], &gEfiSimpleFileSystemProtocolGuid,
                                         (VOID **)&Fs);
            if (EFI_ERROR(Status) || Fs == NULL) {
                continue;
            }
            if (!BootOsVolForPass(ImageHandle, Handles[i], Pass)) {
                continue;
            }
            if (Pred != NULL && !Pred(Fs, Ctx)) {
                continue;
            }
            *OutVol = Handles[i];
            if (OutPass != NULL) {
                *OutPass = Pass;
            }
            gBS->FreePool(Handles);
            return EFI_SUCCESS;
        }
    }

    gBS->FreePool(Handles);
    return EFI_NOT_FOUND;
}
