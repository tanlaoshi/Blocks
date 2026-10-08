/*
 * BootAcpi.c — RSDP 填入 X64_BOOT_CONFIG
 */
#include <Guid/Acpi.h>
#include <Library/UefiLib.h>

#include "BootPrivate.h"

STATIC EFI_STATUS GetRsdpAddress(EFI_PHYSICAL_ADDRESS *RsdpAddress) {
    EFI_STATUS Status;

    Status = EfiGetSystemConfigurationTable(&gEfiAcpiTableGuid, (VOID **)RsdpAddress);
    if (!EFI_ERROR(Status)) {
        return EFI_SUCCESS;
    }

    Status = EfiGetSystemConfigurationTable(&gEfiAcpi10TableGuid, (VOID **)RsdpAddress);
    return Status;
}

VOID BootFillRsdp(X64_BOOT_CONFIG *BootConfig) {
    EFI_STATUS Status;

    Status = GetRsdpAddress(&BootConfig->RsdpAddress);
    if (EFI_ERROR(Status)) {
        BootSerialPrintf("Boot: ACPI RSDP Not Found (Continue)\n");
        BootConfig->RsdpAddress = 0;
    }
}
