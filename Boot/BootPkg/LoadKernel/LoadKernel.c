/*
 * LoadKernel.c — 编排：读盘 → 校验 → 装入
 *
 * 读盘见 Read.c；ELF 见 Elf.c。
 */
#include "BootPrivate.h"

EFI_STATUS BootLoadKernel(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig) {
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS ElfBuffer = 0;
    UINTN ElfSize = 0;

    Status = ReadKernelFile(ImageHandle, &ElfBuffer, &ElfSize);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: ReadKernelFile Failed: %r\n", Status);
        return Status;
    }

    {
        EFI_PHYSICAL_ADDRESS LoadLow = 0;
        UINTN PageCount = 0;

        Status = CheckKernelFile(ElfBuffer, ElfSize, &LoadLow, &PageCount);
        if (EFI_ERROR(Status)) {
            BootSerialPrintf("Boot: CheckKernelFile Failed: %r\n", Status);
            return Status;
        }
        Status = LoadKernelFile(ElfBuffer, LoadLow, PageCount, &BootConfig->EntryAddress);
        if (EFI_ERROR(Status)) {
            BootSerialPrintf("Boot: LoadKernelFile Failed: %r\n", Status);
            return Status;
        }
    }
    BootSerialPrintf("Boot: Kernel Loaded, Entry=0x%lx\n", BootConfig->EntryAddress);
    /* OsIdSeen 已废弃，保持交接包清零值；系统卷由内核挂载时再认 */
    return EFI_SUCCESS;
}
