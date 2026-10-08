/*
 * JumpToKernel.c — Boot 第 6 步：GetMemoryMap、ExitBootServices、跳转 Kernel
 */
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

EFI_STATUS JumpToKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig) {
    EFI_STATUS Status;
    MEMORY_MAP MemoryMap = {NULL, 0, 0, 0, 0};
    UINTN MapKey = 0;
    UINTN Tries;
    UINTN Needed;

    BootSerialPrintf("Boot: JumpToKernel Entry=0x%lx\n",
                     (UINT64)BootConfig->EntryAddress);

    Status = gBS->GetMemoryMap(&MemoryMap.MapSize, NULL, &MapKey,
                               &MemoryMap.DescriptorSize, &MemoryMap.DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        return Status;
    }

    MemoryMap.MapSize += MemoryMap.DescriptorSize * 16;
    Status = gBS->AllocatePool(EfiLoaderData, MemoryMap.MapSize, &MemoryMap.Buffer);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    for (Tries = 0; Tries < 8; Tries++) {
        Needed = MemoryMap.MapSize;
        Status = gBS->GetMemoryMap(&Needed, (EFI_MEMORY_DESCRIPTOR *)MemoryMap.Buffer,
                                   &MapKey, &MemoryMap.DescriptorSize,
                                   &MemoryMap.DescriptorVersion);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            gBS->FreePool(MemoryMap.Buffer);
            MemoryMap.MapSize = Needed + MemoryMap.DescriptorSize * 16;
            Status = gBS->AllocatePool(EfiLoaderData, MemoryMap.MapSize, &MemoryMap.Buffer);
            if (EFI_ERROR(Status)) {
                return Status;
            }
            continue;
        }
        if (EFI_ERROR(Status)) {
            gBS->FreePool(MemoryMap.Buffer);
            return Status;
        }
        MemoryMap.MapSize = Needed;

        Status = gBS->ExitBootServices(ImageHandle, MapKey);
        if (!EFI_ERROR(Status)) {
            typedef VOID (*KERNEL_ENTRY_FN)(UEFI_BOOT_CONFIG *);
            KERNEL_ENTRY_FN Entry;

            BootConfig->MemoryMap = MemoryMap;
            Entry = (KERNEL_ENTRY_FN)(UINTN)BootConfig->EntryAddress;
            /* UEFI/MS ABI：首参在 RCX；HAL 入口优先读 RCX */
            Entry(BootConfig);
            __builtin_unreachable();
        }
    }

    BootSerialPrintf("ExitBootServices Failed After Retries: %r\n", Status);
    gBS->FreePool(MemoryMap.Buffer);
    return Status;
}
