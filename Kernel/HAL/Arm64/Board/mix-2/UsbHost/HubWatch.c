/*
 * UsbHost/HubWatch.c — PR-MIX2-USB-2：热插循环（根口/枚举见 HubEnumerate）
 *
 * 验收：§7.0 boot 后再插坞，splash 见 `e a1`。禁止只扫一次。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

void BoardMix2UsbHubWatch(void) {
    UINT32 PortCount;
    UINT32 PortIndex;
    UINT32 HubAddress;
    UINT32 SawConnect;
    UINT32 LastStable[9];

#define PORT_STATUS_STABLE_MASK                                              \
    (PORT_STATUS_CONNECT | PORT_STATUS_ENABLE | PORT_STATUS_RESET |           \
     PORT_STATUS_POWER | (0xFu << PORT_STATUS_SPEED_SHIFT))

    if (UsbHostXhciStart() != 0) {
        for (;;) {
            UsbHostStallYield(1000000u);
        }
    }

    (void)BoardBringUpSplashPaint("usb hub");
    (void)UsbHostTypeCForceHost();

    PortCount = UsbHostXhciMaxPorts();
    if (PortCount == 0) {
        PortCount = 1;
    }
    if (PortCount > 4u) {
        PortCount = 4u;
    }

    for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
        (void)UsbHostRootPortPower(PortIndex);
        LastStable[PortIndex] = 0xFFFFFFFFu;
    }

    HubAddress = 0;
    SawConnect = 0;
    UsbHostEnumerateStatusCodeClear();

    for (;;) {
        UsbHostTypeCMaintainAfterAttach();

        for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
            UINT32 State = UsbHostRootPortStatus(PortIndex);
            UINT32 Stable = State & PORT_STATUS_STABLE_MASK;

            LastStable[PortIndex] = Stable;

            if ((Stable & PORT_STATUS_CONNECT) != 0 && HubAddress == 0) {
                if (UsbHostEnumerateHubOnPort(PortIndex, &HubAddress) == 0) {
                    SawConnect = 1;
                }
            } else if ((Stable & PORT_STATUS_CONNECT) == 0 && SawConnect != 0) {
                UINT32 AnyConnect = 0;
                UINT32 Check;
                for (Check = 1; Check <= PortCount; Check++) {
                    if ((LastStable[Check] & PORT_STATUS_CONNECT) != 0) {
                        AnyConnect = 1;
                        break;
                    }
                }
                if (AnyConnect == 0) {
                    HubAddress = 0;
                    SawConnect = 0;
                    UsbHostEnumerateStatusCodeClear();
                }
            }
        }

        UsbHostPaintHubStatus(PortCount, LastStable);
        UsbHostStallYield(1000000u);
    }
#undef PORT_STATUS_STABLE_MASK
}

#else

void BoardMix2UsbHubWatch(void) {
}

#endif
