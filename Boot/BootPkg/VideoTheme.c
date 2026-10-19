/*
 * VideoTheme.c — Video 辅助：THEME.CFG 显示偏好
 */
#include <Guid/FileInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

/* 从 FAT 小文本读 THEME.CFG 中的 mode=WxH（��重启生效） */
STATIC BOOLEAN ParseAsciiModeLine(const CHAR8 *Line, UINT32 *OutW, UINT32 *OutH) {
    UINT32 W = 0;
    UINT32 H = 0;
    UINTN i = 0;

    if (Line == NULL || OutW == NULL || OutH == NULL) {
        return FALSE;
    }
    while (Line[i] == ' ' || Line[i] == '\t') {
        i++;
    }
    if (Line[i] == 'm' && Line[i + 1] == 'o' && Line[i + 2] == 'd' &&
        Line[i + 3] == 'e' && Line[i + 4] == '=') {
        i += 5;
    } else {
        return FALSE;
    }
    if (Line[i] == 'a' || Line[i] == 'A') {
        *OutW = 0;
        *OutH = 0;
        return TRUE;
    }
    while (Line[i] >= '0' && Line[i] <= '9') {
        W = W * 10 + (UINT32)(Line[i] - '0');
        i++;
        if (W > 10000) {
            return FALSE;
        }
    }
    if (Line[i] != 'x' && Line[i] != 'X') {
        return FALSE;
    }
    i++;
    while (Line[i] >= '0' && Line[i] <= '9') {
        H = H * 10 + (UINT32)(Line[i] - '0');
        i++;
        if (H > 10000) {
            return FALSE;
        }
    }
    if (W < 640 || H < 480) {
        return FALSE;
    }
    *OutW = W;
    *OutH = H;
    return TRUE;
}

STATIC BOOLEAN ParseThemeConfigurationMode(const CHAR8 *Buf, UINTN Size, UINT32 *OutW,
                                 UINT32 *OutH) {
    UINTN i;
    CHAR8 Line[64];
    UINTN L = 0;

    if (Buf == NULL || Size == 0) {
        return FALSE;
    }
    for (i = 0; i <= Size; i++) {
        CHAR8 C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                if (ParseAsciiModeLine(Line, OutW, OutH)) {
                    return (*OutW >= 640 && *OutH >= 480);
                }
                L = 0;
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    return FALSE;
}

STATIC EFI_STATUS ReadThemeConfigurationOnFs(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
                                   CHAR8 **OutBuf, UINTN *OutSize) {
    STATIC CHAR16 *Paths[] = {
        L"\\THEME.CFG",
        L"\\theme.cfg",
        L"\\rootfs\\THEME.CFG",
    };
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *FileInfo = NULL;
    UINTN InfoSize;
    UINTN p;
    CHAR8 *Buf = NULL;
    UINTN ReadSize;

    *OutBuf = NULL;
    *OutSize = 0;

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
    if (EFI_ERROR(Status) || FileInfo->FileSize == 0 || FileInfo->FileSize > 4096) {
        gBS->FreePool(FileInfo);
        File->Close(File);
        Root->Close(Root);
        return EFI_NOT_FOUND;
    }

    Status = gBS->AllocatePool(EfiLoaderData, (UINTN)FileInfo->FileSize + 1,
                               (VOID **)&Buf);
    if (EFI_ERROR(Status)) {
        gBS->FreePool(FileInfo);
        File->Close(File);
        Root->Close(Root);
        return Status;
    }

    ReadSize = (UINTN)FileInfo->FileSize;
    Status = File->Read(File, &ReadSize, Buf);
    gBS->FreePool(FileInfo);
    File->Close(File);
    Root->Close(Root);
    if (EFI_ERROR(Status)) {
        gBS->FreePool(Buf);
        return Status;
    }
    Buf[ReadSize] = 0;
    *OutBuf = Buf;
    *OutSize = ReadSize;
    return EFI_SUCCESS;
}

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
    if (EFI_ERROR(Status)) {
        Status = Root->Open(Root, &File, L"\\TOYOS.ID", EFI_FILE_MODE_READ, 0); /* 旧标兼容 */
    }
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
STATIC BOOLEAN SamePhysicalDisk(EFI_HANDLE A, EFI_HANDLE B) {
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

STATIC BOOLEAN TryParseModeOnFs(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
                                UINT32 *OutW, UINT32 *OutH) {
    CHAR8 *Buf = NULL;
    UINTN Size = 0;

    if (EFI_ERROR(ReadThemeConfigurationOnFs(Fs, &Buf, &Size))) {
        return FALSE;
    }
    if (ParseThemeConfigurationMode(Buf, Size, OutW, OutH)) {
        gBS->FreePool(Buf);
        return TRUE;
    }
    gBS->FreePool(Buf);
    return FALSE;
}

BOOLEAN TryLoadDisplayPref(EFI_HANDLE ImageHandle, UINT32 *OutW,
                                  UINT32 *OutH) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    UINTN HandleCount = 0;
    EFI_HANDLE *Handles = NULL;
    UINTN i;
    UINTN Pass;

    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                                 (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        return FALSE;
    }

    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                     NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status)) {
        Handles = NULL;
        HandleCount = 0;
    }

    /*
     * Pass0: 和本 EFI 同一块盘上的 BLOCKS（U 盘启动读 U 盘，本机启动读本机）。
     * Pass1: 其它盘上的 BLOCKS（QEMU 的系统盘不在 ESP 那块盘上）。
     * Pass2: 其余卷。
     */
    for (Pass = 0; Pass < 3; Pass++) {
        for (i = 0; i < HandleCount; i++) {
            BOOLEAN SameDisk;

            Status = gBS->HandleProtocol(Handles[i], &gEfiSimpleFileSystemProtocolGuid,
                                         (VOID **)&Fs);
            if (EFI_ERROR(Status)) {
                continue;
            }
            SameDisk = SamePhysicalDisk(LoadedImage->DeviceHandle, Handles[i]);
            if (Pass == 0) {
                if (!FsHasOsMarker(Fs) || !SameDisk) {
                    continue;
                }
            } else if (Pass == 1) {
                if (!FsHasOsMarker(Fs) || SameDisk) {
                    continue;
                }
            } else if (FsHasOsMarker(Fs)) {
                continue;
            }
            if (TryParseModeOnFs(Fs, OutW, OutH)) {
                gBS->FreePool(Handles);
                return TRUE;
            }
        }
    }

    if (Handles != NULL) {
        gBS->FreePool(Handles);
    }

    /* 单盘布局：启动卷即系统卷 */
    Status = gBS->HandleProtocol(LoadedImage->DeviceHandle,
                                 &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (!EFI_ERROR(Status) && TryParseModeOnFs(Fs, OutW, OutH)) {
        return TRUE;
    }
    return FALSE;
}
