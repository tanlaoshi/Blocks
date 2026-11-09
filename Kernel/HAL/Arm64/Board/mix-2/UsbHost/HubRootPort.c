/*
 * UsbHost/HubRootPort.c — 根口电源/复位（leavePRC；对表 USB-2 绿）
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static UINT32 PortStatusNeutral(UINT32 State) {
    return (State & PORT_STATUS_READ_ONLY) | (State & PORT_STATUS_READ_WRITE_SET);
}

static void ConfigureUsb2PhyForHostChirp(void) {
    UINT32 Usb2PhyConfiguration;

    Usb2PhyConfiguration =
        UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET);
    Usb2PhyConfiguration &=
        ~(DWC3_PHY_SOFTWARE_RESET | DWC3_GUSB2PHYCFG_ULPI_UTMI |
          DWC3_GUSB2PHYCFG_SUSPEND_USB20 | DWC3_GUSB2PHYCFG_ENABLE_SLEEP_MODE |
          DWC3_GUSB2PHYCFG_U2_FREE_CLOCK_EXISTS |
          (1u << DWC3_GUSB2PHYCFG_PHYSICAL_INTERFACE_SHIFT) |
          DWC3_GUSB2PHYCFG_USB_TURNAROUND_MASK);
    Usb2PhyConfiguration |= (DWC3_GUSB2PHYCFG_UTMI_TURNAROUND_8BIT
                             << DWC3_GUSB2PHYCFG_USB_TURNAROUND_SHIFT);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET,
                       Usb2PhyConfiguration);
}

UINT32 UsbHostRootPortStatus(UINT32 PortIndexOneBased) {
    return UsbHostReadMmio32(UsbHostXhciPortStatusAddress(PortIndexOneBased));
}

int UsbHostRootPortPower(UINT32 PortIndexOneBased) {
    UINT64 Address = UsbHostXhciPortStatusAddress(PortIndexOneBased);
    UINT32 State = UsbHostReadMmio32(Address);

    if ((State & PORT_STATUS_POWER) == 0) {
        UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_POWER);
        UsbHostStallYieldPumpPower(200000u);
        State = UsbHostReadMmio32(Address);
    }
    return (State & PORT_STATUS_POWER) ? 0 : -1;
}

/*
 * 对表 xHCI USB2 + X64 Port.c 真机：
 *   PED=0 时清 sticky → PR → 等 PRC → 等 PED；
 *   **成功后 leavePRC**（PED 后再 W1C CHANGE → 真机打回 Polling / Address USBTE）；
 *   再等 USB2 T_RSTRCY 才允许 Address。
 */
int UsbHostRootPortReset(UINT32 PortIndexOneBased) {
    UINT64 Address = UsbHostXhciPortStatusAddress(PortIndexOneBased);
    UINT32 State;
    UINTN Guard;
    UINT32 SawResetChange;

    ConfigureUsb2PhyForHostChirp();
    (void)UsbHostQscratchEnsureHostSessionOverride();
    UsbHostStallYieldPumpPower(200000u);

    State = UsbHostReadMmio32(Address);
    if ((State & PORT_STATUS_CONNECT) == 0) {
        return -1;
    }
    if ((State & PORT_STATUS_ENABLE) == 0 &&
        (State & PORT_STATUS_CHANGE_BITS) != 0) {
        UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_CHANGE_BITS);
        UsbHostStallYieldPumpPower(50000u);
    }

    State = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_POWER |
                                    PORT_STATUS_RESET);

    for (Guard = 0; Guard < 2000000u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_RESET) == 0) {
            break;
        }
        UsbHostStallYieldPumpPower(50u);
    }
    if ((State & PORT_STATUS_RESET) != 0) {
        return -1;
    }

    SawResetChange = 0;
    for (Guard = 0; Guard < 200000u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_RESET_CHANGE) != 0) {
            SawResetChange = 1;
            break;
        }
        UsbHostStallYieldPumpPower(50u);
    }
    if (SawResetChange == 0) {
        return -1;
    }

    for (Guard = 0; Guard < 400u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_CONNECT) == 0) {
            return -1;
        }
        if ((State & PORT_STATUS_ENABLE) != 0) {
            UsbHostStallYieldPumpPower(2000000u);
            return 0;
        }
        UsbHostStallYieldPumpPower(5000u);
    }
    return -1;
}

#else

UINT32 UsbHostRootPortStatus(UINT32 PortIndexOneBased) {
    (void)PortIndexOneBased;
    return 0;
}

int UsbHostRootPortPower(UINT32 PortIndexOneBased) {
    (void)PortIndexOneBased;
    return -1;
}

int UsbHostRootPortReset(UINT32 PortIndexOneBased) {
    (void)PortIndexOneBased;
    return -1;
}

#endif
