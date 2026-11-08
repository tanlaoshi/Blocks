/*
 * UsbHost/XhciStart.c — DWC3 PHY soft-reset + xHCI Halt/Reset/Run
 *
 * 对表 Linux dwc3_core_soft_reset（公开）与 xHCI USBCMD/USBSTS。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static UINT64 sCapabilityBase;
static UINT64 sOperationalBase;
static UINT32 sMaxPorts;

static int WaitClear(UINT64 Address, UINT32 Mask, UINTN GuardLimit) {
    UINTN Guard;

    for (Guard = 0; Guard < GuardLimit; Guard++) {
        if ((UsbHostReadMmio32(Address) & Mask) == 0) {
            return 1;
        }
        UsbHostStallYield(50u);
    }
    return 0;
}

static int WaitSet(UINT64 Address, UINT32 Mask, UINTN GuardLimit) {
    UINTN Guard;

    for (Guard = 0; Guard < GuardLimit; Guard++) {
        if ((UsbHostReadMmio32(Address) & Mask) != 0) {
            return 1;
        }
        UsbHostStallYield(50u);
    }
    return 0;
}

static void Dwc3PrepareHost(void) {
    UINT32 GlobalControl;
    UINT32 Usb2Phy;
    UINT32 Usb3Pipe;

    GlobalControl = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET);
    GlobalControl |= DWC3_GCTL_CORESOFTRESET;
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET, GlobalControl);

    Usb2Phy = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET,
                       Usb2Phy | DWC3_PHY_SOFTWARE_RESET);
    Usb3Pipe = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB3PIPECTL_OFFSET);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB3PIPECTL_OFFSET,
                       Usb3Pipe | DWC3_PHY_SOFTWARE_RESET);
    UsbHostStallYield(200000u);

    Usb2Phy = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET,
                       Usb2Phy & ~DWC3_PHY_SOFTWARE_RESET);
    Usb3Pipe = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB3PIPECTL_OFFSET);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB3PIPECTL_OFFSET,
                       Usb3Pipe & ~DWC3_PHY_SOFTWARE_RESET);
    UsbHostStallYield(200000u);

    GlobalControl = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET);
    GlobalControl &= ~DWC3_GCTL_CORESOFTRESET;
    GlobalControl &= ~DWC3_GCTL_PRTCAPDIR_MASK;
    GlobalControl |= DWC3_GCTL_PRTCAP_HOST;
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET, GlobalControl);

    /* HS-only（DT maximum-speed=high-speed）：退出 USB2 PHY suspend */
    Usb2Phy = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET);
    Usb2Phy &= ~((1u << 6) | DWC3_PHY_SOFTWARE_RESET); /* SUSPENDUSB20 */
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET, Usb2Phy);
    UsbHostStallYield(50000u);
}

int UsbHostXhciStart(void) {
    UINT32 Capability;
    UINT32 CapabilityLength;
    UINT32 Structural1;
    UINT32 Structural2;
    UINT32 CapabilityParameters1;
    UINT32 ScratchpadCount;
    UINT32 SlotLimit;
    UINT32 ContextSizeBytes;
    UINT64 DoorbellBase;
    UINT64 RuntimeBase;

    (void)UsbHostQusbPhyInitialize();
    /* DT maximum-speed=high-speed：先 QSCRATCH UTMI-as-PIPE，再松 DWC3 PHY reset */
    (void)UsbHostQscratchConfigureForHost();
    /* ssusb extcon=pdphy：强制 Type-C DFP，否则 ls 无 DP/DM、PORTSC 停 0x200 */
    (void)UsbHostTypeCForceHost();
    Dwc3PrepareHost();

    sCapabilityBase = DWC3_REGISTER_BASE;
    Capability = UsbHostReadMmio32(sCapabilityBase);
    CapabilityLength = Capability & 0xFFu;

    if (Capability == 0xFFFFFFFFu || CapabilityLength < 0x20u ||
        CapabilityLength == 0xFFu) {
        return -1;
    }

    sOperationalBase = sCapabilityBase + CapabilityLength;
    DoorbellBase = sCapabilityBase + (UsbHostReadMmio32(sCapabilityBase + 0x14) & ~0x3u);
    RuntimeBase = sCapabilityBase + (UsbHostReadMmio32(sCapabilityBase + 0x18) & ~0x1Fu);
    CapabilityParameters1 = UsbHostReadMmio32(sCapabilityBase + 0x10);
    ContextSizeBytes = (CapabilityParameters1 & (1u << 2)) ? 64u : 32u;

    Structural1 = UsbHostReadMmio32(sCapabilityBase + 0x04);
    SlotLimit = Structural1 & 0xFFu;
    sMaxPorts = (Structural1 >> 24) & 0xFFu;
    if (SlotLimit == 0 || SlotLimit > DEVICE_SLOT_COUNT_MAX) {
        SlotLimit = DEVICE_SLOT_COUNT_MAX;
    }

    Structural2 = UsbHostReadMmio32(sCapabilityBase + 0x08);
    ScratchpadCount = (((Structural2 >> 21) & 0x1Fu) << 5) | ((Structural2 >> 27) & 0x1Fu);
    if (ScratchpadCount > SCRATCHPAD_BUFFER_COUNT_MAX) {
        return -1;
    }

    UsbHostWriteMmio32(sOperationalBase, 0);
    if (!WaitSet(sOperationalBase + 4u, USB_STATUS_HOST_CONTROLLER_HALTED,
                 1000000u)) {
        return -1;
    }
    UsbHostWriteMmio32(sOperationalBase, USB_COMMAND_HOST_CONTROLLER_RESET);
    if (!WaitClear(sOperationalBase, USB_COMMAND_HOST_CONTROLLER_RESET,
                   1000000u) ||
        !WaitClear(sOperationalBase + 4u, USB_STATUS_CONTROLLER_NOT_READY,
                   1000000u)) {
        return -1;
    }

    if (UsbHostXhciTransferInitialize(sCapabilityBase, sOperationalBase,
                                      DoorbellBase, RuntimeBase,
                                      ContextSizeBytes, SlotLimit,
                                      ScratchpadCount) != 0) {
        return -1;
    }

    __asm__ volatile("dsb sy" ::: "memory");
    UsbHostWriteMmio32(sOperationalBase, USB_COMMAND_RUN_STOP);
    if (!WaitClear(sOperationalBase + 4u, USB_STATUS_HOST_CONTROLLER_HALTED,
                   1000000u)) {
        return -1;
    }
    return 0;
}

UINT32 UsbHostXhciMaxPorts(void) {
    return sMaxPorts;
}

UINT64 UsbHostXhciPortStatusAddress(UINT32 PortIndexOneBased) {
    return sOperationalBase + 0x400u + (UINT64)(PortIndexOneBased - 1u) * 0x10u;
}

#else

int UsbHostXhciStart(void) {
    return -1;
}

UINT32 UsbHostXhciMaxPorts(void) {
    return 0;
}

UINT64 UsbHostXhciPortStatusAddress(UINT32 PortIndexOneBased) {
    (void)PortIndexOneBased;
    return 0;
}

#endif
