/*
 * UsbHost/HubWatch.c — PR-PIXEL8-USB-2：xHCI 起环后热插扫 hub（屏反馈）
 *
 * 验收：拔电脑线→插 Type-C 坞；splash 见 CONNECT / `e…a1`。
 * 不经 Cereal 透传（Host 下行与 Device 上行不同线）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

void BoardPixel8UsbHubWatch(void) {
    UINT32 PortCount;
    UINT32 PortIndex;
    UINT32 HubAddress;
    UINT32 SawConnect;
    UINT32 LastStable[9];
    UINT32 StatusPaintCount;

#define PORT_STATUS_STABLE_MASK                                              \
    (PORT_STATUS_CONNECT | PORT_STATUS_ENABLE | PORT_STATUS_RESET |           \
     PORT_STATUS_POWER | (0xFu << PORT_STATUS_SPEED_SHIFT))

    (void)BoardBringUpSplashPaint("usb hub");
    BoardBringUpSplashPaintLine(112, "tc then xhci");
    HalSerialWriteShell("usb: hub watch\r\n");
    /*
     * 真机：initiate→xhci→再 initiate 会把 PORT1 拧成 Device（c10）；
     * 曾绿的是 c8=Host。保：Prepare→repeater→initiate→xhci→只脉冲 enable，勿整段重 initiate。
     * TCPC 已 tc1/r5/cc9/ps8/sw9；勿在 HME 后再 Open/Look4 打乱附着。
     */
    (void)UsbHostAliveClockPowerManagerEnableEmbeddedUsbRails();
    (void)UsbHostTypeCForceHost();
    (void)UsbHostClockEnableEmbeddedUsb();
    UsbHostPrepareUsb2PhysicalInterface();
    (void)UsbHostRepeaterEnableHost();
    (void)UsbHostPhysicalEnableRepeaterMode();
    if (UsbHostXhciStart() != 0) {
        BoardBringUpSplashPaintLine(112, "xhci fail");
        HalSerialWriteShell("usb: xhci fail\r\n");
        for (;;) {
            UsbHostStallYield(1000000u);
        }
    }
    UsbHostPhysicalRetriggerHostModeEnable();
    (void)UsbHostRepeaterEnableHost();
    UsbHostRepeaterRefreshStatusBytes();

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
    StatusPaintCount = 0;
    UsbHostEnumerateStatusCodeClear();
    /* 立刻刷 p*（盖住 wait 文案行），避免只盯 tc 看不到口状态 */
    for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
        LastStable[PortIndex] =
            UsbHostRootPortStatus(PortIndex) & PORT_STATUS_STABLE_MASK;
    }
    UsbHostPaintHubStatus(PortCount, LastStable, HubAddress);

    for (;;) {
        UINT32 EnumerateAttempted = SawConnect;
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

        if (HubAddress == 0 && EnumerateAttempted == 0 && AnyConnectPort != 0) {
            UINT32 Target =
                (PreferUsb2Port != 0) ? PreferUsb2Port : AnyConnectPort;
            /* 同插单次枚举（对表 Mix2）；失败勿狂重试耗槽 */
            SawConnect = 1;
            BoardBringUpSplashPaintLine(112, "usb enum");
            HalSerialWriteShell("usb: enum\r\n");
            (void)UsbHostEnumerateHubOnPort(Target, &HubAddress);
        } else if (AnyConnect == 0 && SawConnect != 0) {
            HubAddress = 0;
            SawConnect = 0;
            UsbHostEnumerateStatusCodeClear();
            BoardBringUpSplashPaintLine(112, "wait dock");
        }

        StatusPaintCount++;
        if (StatusPaintCount >= 1u) {
            StatusPaintCount = 0;
            UsbHostPaintHubStatus(PortCount, LastStable, HubAddress);
        }
        UsbHostStallMilliseconds(500u);
    }
#undef PORT_STATUS_STABLE_MASK
}

#else

void BoardPixel8UsbHubWatch(void) {
}

#endif
