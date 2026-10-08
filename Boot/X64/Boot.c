#include <Uefi.h>

#include "BootPrivate.h"

/*
 * Boot.c — 只编织流程；功能在 Serial / Video / Kernel / Acpi / Pci / Jump。
 */
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    X64_BOOT_CONFIG BootConfig = {0};

    BootSerialInitialize();
    BootSerialBanner();

    Status = GetAndSetVideo(ImageHandle, &BootConfig.VideoConfig, &BootConfig);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: GetAndSetVideo Failed: %r\n", Status);
        return Status;
    }

    Status = BootLoadKernel(ImageHandle, &BootConfig);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    BootFillRsdp(&BootConfig);
    BootConfig.SystemTable = SystemTable;
    BootFillXhci(&BootConfig);

    BootSerialPrintf("Boot: ExitBootServices + Jump 0x%lx\n", BootConfig.EntryAddress);
    return JumpToKernel(ImageHandle, &BootConfig);
}
