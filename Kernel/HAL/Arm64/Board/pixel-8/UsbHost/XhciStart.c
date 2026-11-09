/*
 * UsbHost/XhciStart.c — Pixel：DWC3 Host 窗上 xHCI Halt/Reset/Run
 *
 * 对表 xHCI 1.1 USBCMD/USBSTS；无 QUSB/QSCRATCH（USB-1 已切 PrtCapDir=Host）。
 * id-odd：跳过 GCTL CoreSoftReset，只配 GUSB2PHYCFG + xHCI。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

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

/*
 * 须在 eUSB initiate 前清 PHYSOFTRST：否则 CON FSM 可活（es≠0）但 UTMI/HME 被掐。
 * 对表 zuma DT + dwc3_exynos_core_init：
 *   清 FREECLK；adj-sof-accuracy=1 → 清 SOFITPSYNC（勿再置）；
 *   GFLADJ=0x20 + 19.2MHz 域；GUCTL：REFCLKPER=0x34、SPRS、NOEXTRDL、HSTINAUTO。
 * 屏证：e304+ls0 → 线态已排除；旧「须置 SOFITPSYNC」与 Android 相反。
 */
void UsbHostPrepareUsb2PhysicalInterface(void) {
    UINT32 Usb2Phy;
    UINT32 GlobalControl;
    UINT32 GlobalUserControl;
    UINT32 FrameLengthAdjustment;
    UINT64 Frequency;
    UINT64 Start;
    UINT64 Delta;

    GlobalControl = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET);
    GlobalControl &= ~DWC3_GCTL_PRTCAPDIR_MASK;
    GlobalControl &= ~DWC3_GCTL_SOFTWARE_ITP_SYNCHRONIZE;
    GlobalControl |= DWC3_GCTL_PRTCAP_HOST;
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET, GlobalControl);

    Usb2Phy = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET);
    Usb2Phy &= ~(DWC3_PHY_SOFTWARE_RESET | DWC3_GUSB2PHYCFG_ULPI_UTMI |
                 DWC3_GUSB2PHYCFG_SUSPEND_USB20 |
                 DWC3_GUSB2PHYCFG_ENABLE_SLEEP_MODE |
                 DWC3_GUSB2PHYCFG_U2_FREE_CLOCK_EXISTS |
                 (1u << DWC3_GUSB2PHYCFG_PHYSICAL_INTERFACE_SHIFT) |
                 DWC3_GUSB2PHYCFG_USB_TURNAROUND_MASK);
    Usb2Phy |= (DWC3_GUSB2PHYCFG_UTMI_TURNAROUND_8BIT
                << DWC3_GUSB2PHYCFG_USB_TURNAROUND_SHIFT);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET, Usb2Phy);

    GlobalUserControl =
        UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GLOBAL_USER_CONTROL_OFFSET);
    GlobalUserControl &= ~DWC3_GLOBAL_USER_CONTROL_REFERENCE_CLOCK_PERIOD_MASK;
    GlobalUserControl |=
        (DWC3_GLOBAL_USER_CONTROL_REFERENCE_CLOCK_PERIOD_19200KHZ
         << DWC3_GLOBAL_USER_CONTROL_REFERENCE_CLOCK_PERIOD_SHIFT) |
        DWC3_GLOBAL_USER_CONTROL_HOST_IN_AUTO_RETRY |
        DWC3_GLOBAL_USER_CONTROL_SPARSE_CONTROL_TRANSFER_ENABLE |
        DWC3_GLOBAL_USER_CONTROL_NO_EXTRA_DELAY;
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GLOBAL_USER_CONTROL_OFFSET,
                       GlobalUserControl);

    FrameLengthAdjustment = UsbHostReadMmio32(
        DWC3_REGISTER_BASE + DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_OFFSET);
    FrameLengthAdjustment &= ~(DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_30MHZ_MASK |
                               DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_REFERENCE_CLOCK_MASK |
                               DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_240MHZ_DECREMENT_MASK);
    FrameLengthAdjustment |=
        DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_30MHZ_VALUE |
        DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_30MHZ_SIDEBAND_SELECT |
        DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_REFERENCE_CLOCK_LOW_POWER_SELECT |
        DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_240MHZ_DECREMENT_VALUE |
        DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_240MHZ_DECREMENT_PLUS_ONE;
    UsbHostWriteMmio32(
        DWC3_REGISTER_BASE + DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_OFFSET,
        FrameLengthAdjustment);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Frequency));
    if (Frequency == 0u) {
        UsbHostStallYield(50000u);
        return;
    }
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Start));
    Delta = (Frequency * 100ull) / 1000000ull;
    for (;;) {
        UINT64 Now;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Now));
        if ((Now - Start) >= Delta) {
            break;
        }
    }
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

    UsbHostPrepareUsb2PhysicalInterface();

    sCapabilityBase = DWC3_REGISTER_BASE;
    Capability = UsbHostReadMmio32(sCapabilityBase);
    CapabilityLength = Capability & 0xFFu;
    HalSerialWriteShell("xhci: cap ");
    {
        char Hex[12];
        UsbHostFormatHexadecimal32(Hex, Capability);
        HalSerialWriteShell(Hex);
        HalSerialWriteShell("\r\n");
    }

    if (Capability == 0xFFFFFFFFu || CapabilityLength < 0x20u ||
        CapabilityLength == 0xFFu) {
        HalSerialWriteShell("xhci: bad cap\r\n");
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
        HalSerialWriteShell("xhci: scratch\r\n");
        return -1;
    }

    {
        char Hex[12];
        HalSerialWriteShell("xhci: ports ");
        UsbHostFormatHexadecimal32(Hex, sMaxPorts);
        HalSerialWriteShell(Hex);
        HalSerialWriteShell(" sts ");
        UsbHostFormatHexadecimal32(Hex, UsbHostReadMmio32(sOperationalBase + 4u));
        HalSerialWriteShell(Hex);
        HalSerialWriteShell("\r\n");
    }

    HalSerialWriteShell("xhci: halt\r\n");
    UsbHostWriteMmio32(sOperationalBase, 0);
    if (!WaitSet(sOperationalBase + 4u, USB_STATUS_HOST_CONTROLLER_HALTED,
                 200000u)) {
        char Hex[12];
        UsbHostFormatHexadecimal32(Hex, UsbHostReadMmio32(sOperationalBase + 4u));
        HalSerialWriteShell("xhci: halt-fail ");
        HalSerialWriteShell(Hex);
        HalSerialWriteShell("\r\n");
        return -1;
    }

    /*
     * 真机：已 Halted(sts=1) 时 HCRST 可能永不落（DWC3 钟/电源未全开）。
     * 对表 xHCI：CNR=0 且 Halted 时可跳过 HCRST，直接装环 Run。
     */
    {
        UINT32 StatusBeforeReset = UsbHostReadMmio32(sOperationalBase + 4u);
        if ((StatusBeforeReset & USB_STATUS_HOST_CONTROLLER_HALTED) != 0 &&
            (StatusBeforeReset & USB_STATUS_CONTROLLER_NOT_READY) == 0) {
            HalSerialWriteShell("xhci: skip-hcrst\r\n");
        } else {
            HalSerialWriteShell("xhci: reset\r\n");
            UsbHostWriteMmio32(sOperationalBase, USB_COMMAND_HOST_CONTROLLER_RESET);
            if (!WaitClear(sOperationalBase, USB_COMMAND_HOST_CONTROLLER_RESET,
                           200000u) ||
                !WaitClear(sOperationalBase + 4u, USB_STATUS_CONTROLLER_NOT_READY,
                           200000u)) {
                char Hex[12];
                UsbHostFormatHexadecimal32(Hex, UsbHostReadMmio32(sOperationalBase + 4u));
                HalSerialWriteShell("xhci: reset-fail ");
                HalSerialWriteShell(Hex);
                HalSerialWriteShell(" cmd ");
                UsbHostFormatHexadecimal32(Hex, UsbHostReadMmio32(sOperationalBase));
                HalSerialWriteShell(Hex);
                HalSerialWriteShell("\r\n");
                return -1;
            }
        }
    }

    HalSerialWriteShell("xhci: xfer\r\n");
    if (UsbHostXhciTransferInitialize(sCapabilityBase, sOperationalBase,
                                      DoorbellBase, RuntimeBase,
                                      ContextSizeBytes, SlotLimit,
                                      ScratchpadCount) != 0) {
        HalSerialWriteShell("xhci: xfer-fail\r\n");
        return -1;
    }

    (void)UsbHostXhciProtocolPortsProbe(sCapabilityBase, sMaxPorts);

    HalSerialWriteShell("xhci: run\r\n");
    UsbHostWriteMmio32(RuntimeBase + 0x20u, 0x2u);
    __asm__ volatile("dsb sy" ::: "memory");
    UsbHostWriteMmio32(sOperationalBase, USB_COMMAND_RUN_STOP | (1u << 2));
    if (!WaitClear(sOperationalBase + 4u, USB_STATUS_HOST_CONTROLLER_HALTED,
                   200000u)) {
        HalSerialWriteShell("xhci: run-fail\r\n");
        return -1;
    }
    HalSerialWriteShell("xhci: run ok\r\n");
    return 0;
}

UINT32 UsbHostXhciMaxPorts(void) {
    return sMaxPorts;
}

UINT64 UsbHostXhciPortStatusAddress(UINT32 PortIndexOneBased) {
    return sOperationalBase + 0x400u + (UINT64)(PortIndexOneBased - 1u) * 0x10u;
}

#else

void UsbHostPrepareUsb2PhysicalInterface(void) {
}

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
