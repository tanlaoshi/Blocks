#include <Uefi.h>

#include "BootPrivate.h"

/*
 * Boot.c — 只编织流程（文件名 = 入口函数名）
 *
 *   1. BootSerial      串口
 *   2. BootVideo       GetVideoInfo / SetVideoMode（辅助：BootVideoScore / Edid / Theme）
 *   3. BootLoadKernel  读盘装入 Kernel.elf
 *   4. BootFillRsdp    ACPI RSDP
 *   5. BootFillXhci    xHCI 基址
 *   6. JumpToKernel    ExitBootServices + 跳内核
 */
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    UEFI_BOOT_CONFIG BootConfig = {0};

    BootSerialInitialize();
    BootSerialBanner();

    Status = GetVideoInfo(ImageHandle, &BootConfig);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: GetVideoInfo Failed: %r\n", Status);
        return Status;
    }
    Status = SetVideoMode(ImageHandle, &BootConfig.VideoConfig, &BootConfig);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: SetVideoMode Failed: %r\n", Status);
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
