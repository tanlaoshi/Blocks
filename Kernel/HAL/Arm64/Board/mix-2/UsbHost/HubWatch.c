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
            /* MIX2 不进 Tasks：起环失败也须轮询电源键 */
            UsbHostStallYieldPumpPower(1000000u);
        }
    }

    /* XhciStart 内已 ForceHost+OTG；此处勿再 ForceHost（会关 boost → os0） */
    (void)BoardBringUpSplashPaint("usb hub");

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
        UINT32 EnumerateAttempted = SawConnect; /* 同一次插着只枚举一回 */

        /*
         * PR-PHONE-PWR-0：Handoff 停在本循环；StallYieldPumpPower 已泵 PON。
         * 此处再采一次，保证 paint 前 RT_STS 新鲜。
         */
        (void)BoardPowerButtonPressed();
        UsbHostTypeCMaintainAfterAttach();

        {
            UINT32 Usb2Mask = UsbHostXhciUsb2PortBitMask();
            UINT32 PreferUsb2Port = 0;
            UINT32 AnyConnectPort = 0;
            UINT32 AnyConnect = 0;

            for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
                UINT32 State = UsbHostRootPortStatus(PortIndex);
                UINT32 Stable = State & PORT_STATUS_STABLE_MASK;

                LastStable[PortIndex] = Stable;
                if ((Stable & PORT_STATUS_CONNECT) == 0) {
                    continue;
                }
                AnyConnect = 1;
                if (AnyConnectPort == 0) {
                    AnyConnectPort = PortIndex;
                }
                if ((Usb2Mask & (1u << PortIndex)) != 0) {
                    PreferUsb2Port = PortIndex;
                }
            }

            if (HubAddress == 0 && EnumerateAttempted == 0 &&
                AnyConnectPort != 0) {
                UINT32 Target =
                    (PreferUsb2Port != 0) ? PreferUsb2Port : AnyConnectPort;
                SawConnect = 1;
                (void)UsbHostEnumerateHubOnPort(Target, &HubAddress);
            } else if (AnyConnect == 0 && SawConnect != 0) {
                HubAddress = 0;
                SawConnect = 0;
                UsbHostEnumerateStatusCodeClear();
            }
        }

        UsbHostPaintHubStatus(PortCount, LastStable);
        UsbHostStallYieldPumpPower(200000u);
    }
#undef PORT_STATUS_STABLE_MASK
}

#else

void BoardMix2UsbHubWatch(void) {
}

#endif
