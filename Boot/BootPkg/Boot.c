#include <Uefi.h>

#include "BootPrivate.h"

/*
 * Boot.c — 只编织流程（文件名 = 入口函数名）
 *
 *   1. Serial       串口（Serial.c）
 *   2. FillRsdp / FillXhci / DiskGuid  可选交接（找不到填 0）
 *   3. Video/       GetVideoInfo / SetVideoMode（Score/Edid/Theme）
 *   4. LoadKernel/  读盘装入 Kernel.elf（Read/Elf）
 *   5. JumpToKernel ExitBootServices + 跳内核
 */
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    UEFI_BOOT_CONFIG BootConfig = {0};
    VIDEO_SELECTION VideoSel;

    /* 入口参数，与选模/读盘无关；尽早写入交接包 */
    BootConfig.SystemTable = SystemTable;

    BootSerialInitialize();
    BootSerialBanner();

    /* 只读固件表/PCI，不依赖 GOP 与 Kernel.elf；找不到则填 0 继续 */
    BootFillRsdp(&BootConfig);
    BootFillXhci(&BootConfig);
    BootFillDiskGuid(ImageHandle, &BootConfig);

    /* 失败文案在各步骤函数内；这里只决定是否继续 */
    Status = GetVideoInfo(ImageHandle, &BootConfig, &VideoSel);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    Status = SetVideoMode(ImageHandle, &BootConfig.VideoConfig, &BootConfig,
                          &VideoSel);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    Status = BootLoadKernel(ImageHandle, &BootConfig);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    BootSerialPrintf("Boot: ExitBootServices + Jump 0x%lx\n", BootConfig.EntryAddress);
    return JumpToKernel(ImageHandle, &BootConfig);
}
