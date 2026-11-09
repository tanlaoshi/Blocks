/*
 * UsbHost/HubRootPort.c — 根口 Status/Power/Reset（自 HubEnumerate 拆出）
 *
 * 对表 xHCI 1.1 PORTSC + X64 Port.c leavePRC；T_RSTRCY 真 ms。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

static UINT32 PortStatusNeutral(UINT32 State) {
    return (State & PORT_STATUS_READ_ONLY) | (State & PORT_STATUS_READ_WRITE_SET);
}

static void ConfigureUsb2PhyForHostChirp(void) {
    UINT32 Usb2PhyConfiguration;
    UINT32 GlobalControl;

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
    /* 对表 zuma adj-sof-accuracy：复位路径也保持 SOFITPSYNC 清 */
    GlobalControl = UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET);
    GlobalControl &= ~DWC3_GCTL_SOFTWARE_ITP_SYNCHRONIZE;
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GCTL_OFFSET, GlobalControl);
}

UINT32 UsbHostRootPortStatus(UINT32 PortIndexOneBased) {
    return UsbHostReadMmio32(UsbHostXhciPortStatusAddress(PortIndexOneBased));
}

int UsbHostRootPortPower(UINT32 PortIndexOneBased) {
    UINT64 Address = UsbHostXhciPortStatusAddress(PortIndexOneBased);
    UINT32 State = UsbHostReadMmio32(Address);

    if ((State & PORT_STATUS_POWER) == 0) {
        UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_POWER);
        UsbHostStallYield(200000u);
        State = UsbHostReadMmio32(Address);
    }
    return (State & PORT_STATUS_POWER) ? 0 : -1;
}

/*
 * PED=0 时清 sticky → PR → 等 PRC → 等 PED；成功后 leavePRC；
 * 再 T_RSTRCY 50ms。禁 USB2 写 PLS+LWS。
 */
int UsbHostRootPortReset(UINT32 PortIndexOneBased) {
    UINT64 Address = UsbHostXhciPortStatusAddress(PortIndexOneBased);
    UINT32 State;
    UINTN Guard;

    ConfigureUsb2PhyForHostChirp();
    UsbHostStallMilliseconds(10u);

    State = UsbHostReadMmio32(Address);
    if ((State & PORT_STATUS_CONNECT) == 0) {
        UsbHostEnumerateStatusCodeSet(0x1100u | (State & 0xFFu));
        return -1;
    }
    if ((State & PORT_STATUS_ENABLE) == 0 &&
        (State & PORT_STATUS_CHANGE_BITS) != 0) {
        UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_CHANGE_BITS);
        UsbHostStallMilliseconds(5u);
    }

    State = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, PortStatusNeutral(State) | PORT_STATUS_POWER |
                                    PORT_STATUS_RESET);

    for (Guard = 0; Guard < 100u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_RESET) == 0) {
            break;
        }
        UsbHostStallMilliseconds(1u);
    }
    if ((State & PORT_STATUS_RESET) != 0) {
        UsbHostEnumerateStatusCodeSet(0x1200u | (State & 0xFFu));
        return -1;
    }

    for (Guard = 0; Guard < 500u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_RESET_CHANGE) != 0) {
            break;
        }
        UsbHostStallMilliseconds(1u);
    }
    if ((State & PORT_STATUS_RESET_CHANGE) == 0) {
        UsbHostEnumerateStatusCodeSet(0x1300u | (State & 0xFFu));
        return -1;
    }

    for (Guard = 0; Guard < 1000u; Guard++) {
        State = UsbHostReadMmio32(Address);
        if ((State & PORT_STATUS_CONNECT) == 0) {
            UsbHostEnumerateStatusCodeSet(0x1400u | (State & 0xFFu));
            return -1;
        }
        if ((State & PORT_STATUS_ENABLE) != 0) {
            UINT32 PortSpeed;
            UINTN Settle;

            /*
             * 串口：enum 后曾先 p1:601 再 e03——PED 边沿时 Speed 可能未锁。
             * 等 Speed≠0 再 T_RSTRCY；eUSB 上 SET_ADDRESS 尤需稳定 HS。
             */
            for (Settle = 0; Settle < 100u; Settle++) {
                State = UsbHostReadMmio32(Address);
                PortSpeed = (State >> PORT_STATUS_SPEED_SHIFT) & 0xFu;
                if ((State & PORT_STATUS_ENABLE) != 0 && PortSpeed != 0u) {
                    break;
                }
                UsbHostStallMilliseconds(1u);
            }
            if ((State & PORT_STATUS_ENABLE) == 0 ||
                ((State >> PORT_STATUS_SPEED_SHIFT) & 0xFu) == 0u) {
                UsbHostEnumerateStatusCodeSet(0x1600u | (State & 0xFFu));
                return -1;
            }
            UsbHostStallMilliseconds(100u);
            State = UsbHostReadMmio32(Address);
            if ((State & PORT_STATUS_ENABLE) == 0 ||
                (State & PORT_STATUS_CONNECT) == 0) {
                UsbHostEnumerateStatusCodeSet(0x1700u | (State & 0xFFu));
                return -1;
            }
            return 0;
        }
        UsbHostStallMilliseconds(1u);
    }
    UsbHostEnumerateStatusCodeSet(0x1500u | (State & 0xFFu));
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
