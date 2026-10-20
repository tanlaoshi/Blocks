/*
 * Volume.c — 卷标记与启动盘 GUID（BLOCKS.ID / GPT DiskGUID）
 *
 * 供 VideoTheme 选 THEME.CFG、LoadKernel 找系统盘共用。
 */
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
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
