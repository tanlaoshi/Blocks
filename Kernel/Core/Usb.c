/*
 * Usb.c — K10 探控；K13 经 VMM MapMmio 读 CAP/VER
 *
 * 【初学者】
 * Boot 交 XhciBase。窗内可直接读；窗外须先 VirtualMemoryMapMmio，
 * 再读 CAPLENGTH / HCIVERSION。不建环、不开中断、不枚举。
 */
#include "Usb.h"
#include "BootInfo.h"
#include "BootTypes.h"
#include "HalSerial.h"
#include "IdentityMap.h"
#include "ToySerialConfig.h"
#include "VirtualMemory.h"

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
static int ProbeXhciCaps(UINT64 Virt, UINT8 *OutCap, UINT16 *OutVer) {
    volatile UINT8 *Mmio;
    UINT8 Cap;
    UINT16 Ver;

    if (Virt == 0 || (Virt & 0xFu) != 0) {
        return -1;
    }
    Mmio = (volatile UINT8 *)(UINTN)Virt;
    Cap = Mmio[0];
    Ver = (UINT16)Mmio[2] | ((UINT16)Mmio[3] << 8);
    /* CAPLENGTH 经验窗；HCIVERSION 在部分 QEMU 上可为 0，勿误杀 */
    if (Cap < 0x20u || Cap > 0x80u || Ver == 0xFFFFu) {
        return -1;
    }
    if (OutCap != 0) {
        *OutCap = Cap;
    }
    if (OutVer != 0) {
        *OutVer = Ver;
    }
    return 0;
}
#endif

int UsbInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gXhciBase = 0;
    gXhciVirt = 0;
    gXhciReady = 0;

#if defined(__x86_64__) || defined(_M_X64)
    {
        UINT8 Cap = 0;
        UINT16 Ver = 0;
        UINT64 Base = 0;
        UINT64 Virt = 0;

        if (Info != 0) {
            Base = Info->XhciBase;
        }
        gXhciBase = Base;

        if (Base == 0) {
            HalSerialWriteChannel(TOY_SLOG_USB, "Usb: WARN no xhci (handoff)\n");
            return 0;
        }

        if (VirtualMemoryMapMmio(Base, 0x1000ull, &Virt) != 0) {
            HalSerialWriteChannel(TOY_SLOG_USB, "Usb: WARN map mmio fail @0x");
            HalSerialWriteChannelHex64(TOY_SLOG_USB, Base);
            HalSerialWriteChannel(TOY_SLOG_USB, "\n");
            return 0;
        }
        gXhciVirt = Virt;

        if (ProbeXhciCaps(Virt, &Cap, &Ver) != 0) {
            HalSerialWriteChannel(TOY_SLOG_USB, "Usb: WARN xhci mmio bad @0x");
            HalSerialWriteChannelHex64(TOY_SLOG_USB, Virt);
            HalSerialWriteChannel(TOY_SLOG_USB, "\n");
            return 0;
        }
        gXhciReady = 1;
        HalSerialWriteChannel(TOY_SLOG_USB, "Usb: xhci ok @0x");
        HalSerialWriteChannelHex64(TOY_SLOG_USB, Virt);
        HalSerialWriteChannel(TOY_SLOG_USB, " cap=0x");
        HalSerialWriteChannelHex32(TOY_SLOG_USB, Cap);
        HalSerialWriteChannel(TOY_SLOG_USB, " ver=0x");
        HalSerialWriteChannelHex32(TOY_SLOG_USB, Ver);
        HalSerialWriteChannel(TOY_SLOG_USB, "\n");
        return 0;
    }
#else
    (void)Info;
    HalSerialWriteChannel(TOY_SLOG_USB, "Usb: stub (no xhci probe)\n");
    return 0;
#endif
}
