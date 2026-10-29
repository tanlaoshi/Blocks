/*
 * FillXhci.c — PCI 上找 xHCI 基址（可选交接，找不到填 0）
 */
#include <Protocol/PciIo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

EFI_STATUS GetXhciBaseAddress(UINT64 *XhciBase) {
    EFI_STATUS Status;
    UINTN HandleCount = 0;
    EFI_HANDLE *HandleBuffer = NULL;
    UINTN i;

    if (XhciBase == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *XhciBase = 0;
    BootDbg("Boot: Looking for XHCI...\n");

    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid,
                                     NULL, &HandleCount, &HandleBuffer);
    if (EFI_ERROR(Status)) {
        BootDbg("Boot: LocateHandleBuffer Failed: %r\n", Status);
        return Status;
    }

    BootDbg("Boot: Found %d PCI Devices\n", HandleCount);

    for (i = 0; i < HandleCount; i++) {
        EFI_PCI_IO_PROTOCOL *PciIo = NULL;
        UINT16 VendorID = 0;
        UINT16 DeviceID = 0;
        UINT32 ClassCode = 0;
        UINT8 Class;
        UINT8 Subclass;
        UINT8 ProgIF;

        Status = gBS->OpenProtocol(HandleBuffer[i], &gEfiPciIoProtocolGuid,
                                   (VOID **)&PciIo, NULL, NULL,
                                   EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        if (EFI_ERROR(Status)) {
            continue;
        }

        /*
         * 每次读都判返回值：读失败的设备直接跳过。
         * VID/DID 各读 16 位，不再靠「根桥把地址 bit1:0 清零 + >>16」的实现细节。
         */
        Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16, 0x00, 1, &VendorID);
        if (EFI_ERROR(Status)) {
            BootDbg("Boot: Pci.Read VID Failed: %r\n", Status);
            continue;
        }
        Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16, 0x02, 1, &DeviceID);
        if (EFI_ERROR(Status)) {
            BootDbg("Boot: Pci.Read DID Failed: %r\n", Status);
            continue;
        }
        Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, 0x08, 1, &ClassCode);
        if (EFI_ERROR(Status)) {
            BootDbg("Boot: Pci.Read Class Failed: %r\n", Status);
            continue;
        }

        Class = (UINT8)((ClassCode >> 24) & 0xFF);
        Subclass = (UINT8)((ClassCode >> 16) & 0xFF);
        ProgIF = (UINT8)((ClassCode >> 8) & 0xFF);

        BootDbg("Boot: Device %d: VID=0x%04x DID=0x%04x Class=0x%02x Sub=0x%02x ProgIF=0x%02x\n",
              i, VendorID, DeviceID, Class, Subclass, ProgIF);

        /* XHCI = USB serial bus class, xHCI ProgIF 0x30 */
        if (Class == 0x0C && Subclass == 0x03 && ProgIF == 0x30) {
            UINT32 Bar0 = 0;
            UINT64 Address;
            UINT16 Vid = VendorID;
            UINT16 Did = DeviceID;

            Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, 0x10, 1, &Bar0);
            if (EFI_ERROR(Status)) {
                /* BAR 读不出来就不能把地址交给内核 */
                BootDbg("Boot: Pci.Read BAR0 Failed: %r\n", Status);
                continue;
            }
            Address = Bar0 & 0xFFFFFFF0U;
            if ((Bar0 & 0x6) == 0x4) {
                UINT32 Bar1 = 0;

                Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, 0x14, 1, &Bar1);
                if (EFI_ERROR(Status)) {
                    BootDbg("Boot: Pci.Read BAR1 Failed: %r\n", Status);
                    continue;
                }
                Address |= ((UINT64)Bar1 << 32);
            }

            BootDbg("Boot: XHCI Found VID=0x%04x DID=0x%04x BAR=0x%016lx\n",
                    Vid, Did, Address);
            /*
             * 优先 QEMU qemu-xhci（1b36:000d）：run.sh 把 kbd/tablet 挂在这颗上。
             * 否则记下候选，扫完取最后一颗（后加的控制器常带设备）。
             */
            *XhciBase = Address;
            if (Vid == 0x1B36 && Did == 0x000D) {
                gBS->FreePool(HandleBuffer);
                return EFI_SUCCESS;
            }
        }
    }

    if (*XhciBase != 0) {
        BootDbg("Boot: XHCI Using Last Match @0x%016lx\n", *XhciBase);
        gBS->FreePool(HandleBuffer);
        return EFI_SUCCESS;
    }

    BootDbg("Boot: No XHCI Controller Found\n");
    gBS->FreePool(HandleBuffer);
    return EFI_NOT_FOUND;
}

VOID BootFillXhci(UEFI_BOOT_CONFIG *BootConfig) {
    BootDbg("Boot: Calling GetXhciBaseAddress...\n");
    if (!EFI_ERROR(GetXhciBaseAddress(&BootConfig->XhciBaseAddress))) {
        BootDbg("Boot: XHCI Base: 0x%016lx\n", BootConfig->XhciBaseAddress);
    } else {
        BootConfig->XhciBaseAddress = 0;
        BootDbg("Boot: XHCI Not Found, Setting To 0\n");
    }
    BootDbg("Boot: UEFI_BOOT_CONFIG.XhciBaseAddress = 0x%016lx\n",
            BootConfig->XhciBaseAddress);
}
