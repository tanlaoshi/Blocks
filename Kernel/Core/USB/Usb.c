/*
 * Usb.c — USB 模块胶水（K10/K13/K14）
 *
 * 【初学者】
 * 模块表只编排：MapMmio → HalXhci 附着/复位 → 报端口 CCS。
 * 寄存器细节在 Hal/X64/HalXhci.c（门面 HalXhci.h），便于换后端。
 */
#include "Usb.h"
#include "BootInfo.h"
#include "BootTypes.h"
#include "HalSerial.h"
#include "HalXhci.h"
#include "SerialConfig.h"
#include "VirtualMemory.h"

#define USB_MMIO_MAP_SIZE  0x10000ull
#define USB_PORT_LOG_MAX   8u

static UINT64 gXhciBase;
static UINT64 gXhciVirt;
static int gXhciReady;

int UsbXhciReady(void) {
    return gXhciReady;
}

UINT64 UsbXhciBase(void) {
    return gXhciBase;
}

UINT64 UsbXhciVirt(void) {
    return gXhciVirt;
}

#if defined(__x86_64__) || defined(_M_X64)
static void UsbLogPorts(void) {
    UINT32 N;
    UINT32 i;
    UINT32 Show;

    N = HalXhciPortCount();
    HalSerialWriteChannel(SLOG_USB, "Usb: ports=");
    HalSerialWriteChannelHex32(SLOG_USB, N);
    HalSerialWriteChannel(SLOG_USB, "\n");

    Show = N;
    if (Show > USB_PORT_LOG_MAX) {
        Show = USB_PORT_LOG_MAX;
    }
    for (i = 1; i <= Show; i++) {
        int Ccs = HalXhciPortCcs(i);

        HalSerialWriteChannel(SLOG_USB, "Usb: port ");
        HalSerialWriteChannelHex32(SLOG_USB, i);
        HalSerialWriteChannel(SLOG_USB, " CCS=");
        if (Ccs < 0) {
            HalSerialWriteChannel(SLOG_USB, "?\n");
        } else {
            HalSerialWriteChannelHex32(SLOG_USB, (UINT32)Ccs);
            HalSerialWriteChannel(SLOG_USB, "\n");
        }
    }
}
#endif

int UsbInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gXhciBase = 0;
    gXhciVirt = 0;
    gXhciReady = 0;

#if defined(__x86_64__) || defined(_M_X64)
    {
        UINT64 Base = 0;
        UINT64 Virt = 0;

        if (Info != 0) {
            Base = Info->XhciBase;
        }
        gXhciBase = Base;

        if (Base == 0) {
            HalSerialWriteChannel(SLOG_USB, "Usb: WARN no xhci (handoff)\n");
            return 0;
        }

        if (VirtualMemoryMapMmio(Base, USB_MMIO_MAP_SIZE, &Virt) != 0) {
            HalSerialWriteChannel(SLOG_USB, "Usb: WARN map mmio fail @0x");
            HalSerialWriteChannelHex64(SLOG_USB, Base);
            HalSerialWriteChannel(SLOG_USB, "\n");
            return 0;
        }
        gXhciVirt = Virt;

        if (HalXhciAttach(Virt) != 0) {
            HalSerialWriteChannel(SLOG_USB, "Usb: WARN xhci attach @0x");
            HalSerialWriteChannelHex64(SLOG_USB, Virt);
            HalSerialWriteChannel(SLOG_USB, "\n");
            return 0;
        }

        HalSerialWriteChannel(SLOG_USB, "Usb: xhci ok @0x");
        HalSerialWriteChannelHex64(SLOG_USB, Virt);
        HalSerialWriteChannel(SLOG_USB, "\n");

        if (HalXhciReset() != 0) {
            HalSerialWriteChannel(SLOG_USB, "Usb: WARN reset timeout\n");
            /* 仍尝试读端口；软成功 */
        } else {
            HalSerialWriteChannel(SLOG_USB, "Usb: reset ok\n");
        }

        UsbLogPorts();
        gXhciReady = 1;
        return 0;
    }
#else
    (void)Info;
    HalSerialWriteChannel(SLOG_USB, "Usb: stub (no xhci probe)\n");
    return 0;
#endif
}
