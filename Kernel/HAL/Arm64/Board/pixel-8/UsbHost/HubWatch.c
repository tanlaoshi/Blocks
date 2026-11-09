/*
 * UsbHost/HubWatch.c — PR-PIXEL8-USB-3：hub enum → HID Boot → splash 回显
 *
 * 验收：坞直插；`e…a1` + `k a2`；敲键 `KEY>` 出字。不经 Cereal。
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
    UINT32 HubSlotId;
    UINT32 KeyboardSlotId;
    UINT32 HubRootPort;
    UINT32 HubSpeed;
    UINT32 HubPortCount;
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
    HubSlotId = 0;
    KeyboardSlotId = 0;
    HubRootPort = 0;
    HubSpeed = 0;
    HubPortCount = 0;
    SawConnect = 0;
    StatusPaintCount = 0;
    {
        UINT32 EnumerateCooldown = 0;
        UINT32 HidScanDone = 0;
        UINT32 DisconnectStreak = 0;
        UINT32 LastKeyboardPaintSerial = 0xFFFFFFFFu;
        UsbHostEnumerateStatusCodeClear();
        UsbHostKeyboardStatusCodeClear();
        for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
            LastStable[PortIndex] =
                UsbHostRootPortStatus(PortIndex) & PORT_STATUS_STABLE_MASK;
        }
        UsbHostPaintHubStatus(PortCount, LastStable, HubAddress);
        UsbHostPaintKeyboardStatus();

        for (;;) {
            UINT32 Usb2Mask = UsbHostXhciUsb2PortBitMask();
            UINT32 PreferUsb2Port = 0;
            UINT32 AnyConnectPort = 0;
            UINT32 AnyConnect = 0;
            UINT32 KeyboardPaintSerial;

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

            /*
             * 失败勿永久 SawConnect 锁死（USB-3 曾一枪 e304 就卡死）。
             * DisableSlot 在 Enumerate 失败路径；此处 ~2s 退避再试。
             */
            if (HubSlotId == 0 && AnyConnectPort != 0 && EnumerateCooldown == 0) {
                UINT32 Target =
                    (PreferUsb2Port != 0) ? PreferUsb2Port : AnyConnectPort;
                SawConnect = 1;
                BoardBringUpSplashPaintLine(112, "usb enum");
                HalSerialWriteShell("usb: enum\r\n");
                UsbHostPhysicalRetriggerHostModeEnable();
                if (UsbHostEnumerateHubOnPort(Target, &HubAddress) == 0) {
                    HubSlotId = UsbHostHubSlotId();
                    HubRootPort = UsbHostHubRootPortIndex();
                    HubSpeed = UsbHostHubPortSpeed();
                    if (HubSlotId != 0 &&
                        UsbHostFinishHubSetup(HubSlotId, HubRootPort, HubSpeed,
                                             &HubPortCount) == 0) {
                        BoardBringUpSplashPaintLine(112, "usb hid");
                        (void)UsbHostFindBootKeyboardBehindHub(
                            HubSlotId, HubRootPort, HubSpeed, HubPortCount,
                            &KeyboardSlotId);
                        UsbHostKeyboardLogFreeze();
                        HidScanDone = 1u; /* 一趟定稿；拔坞再扫，忌 k 数字狂滚 */
                    } else if (HubSlotId != 0) {
                        BoardBringUpSplashPaintLine(112, "hub setup");
                    }
                } else {
                    HubAddress = 0;
                    EnumerateCooldown = 4u;
                }
            } else if (HubSlotId != 0 && KeyboardSlotId == 0 && AnyConnect != 0 &&
                       HidScanDone == 0) {
                BoardBringUpSplashPaintLine(112, "usb hid");
                (void)UsbHostFindBootKeyboardBehindHub(
                    HubSlotId, HubRootPort, HubSpeed, HubPortCount, &KeyboardSlotId);
                UsbHostKeyboardLogFreeze();
                HidScanDone = 1u;
            } else if (AnyConnect == 0 && SawConnect != 0) {
                /*
                 * 有键后零延迟狂读根口，偶发 CCS=0 会清 sticky（a2「改没了」）。
                 * 连续多次无连接才当真拔坞。
                 */
                DisconnectStreak++;
                if (DisconnectStreak >= 8u) {
                    HubAddress = 0;
                    HubSlotId = 0;
                    KeyboardSlotId = 0;
                    HubRootPort = 0;
                    HubSpeed = 0;
                    HubPortCount = 0;
                    SawConnect = 0;
                    EnumerateCooldown = 0;
                    HidScanDone = 0;
                    DisconnectStreak = 0;
                    LastKeyboardPaintSerial = 0xFFFFFFFFu;
                    UsbHostEnumerateStatusCodeClear();
                    UsbHostKeyboardStatusCodeClear();
                    BoardBringUpSplashPaintLine(112, "wait dock");
                }
            } else {
                DisconnectStreak = 0;
            }
            if (EnumerateCooldown > 0) {
                EnumerateCooldown--;
            }

            if (KeyboardSlotId != 0) {
                (void)UsbHostHidBootKeyboardPoll();
            }

            StatusPaintCount++;
            /*
             * 有键后少刷 hub 行（整屏清画会饿死 HID）；无键仍常刷看插拔。
             */
            if (StatusPaintCount >= (KeyboardSlotId != 0 ? 64u : 1u)) {
                StatusPaintCount = 0;
                UsbHostPaintHubStatus(PortCount, LastStable, HubAddress);
                /*
                 * k 行：内容变了才重画。SplashPaintLine 每帧清行底，
                 * 同文狂刷也会「刷新闪」。
                 */
                KeyboardPaintSerial =
                    (UsbHostKeyboardStatusLogCount() << 28) ^
                    UsbHostKeyboardStatusCode() ^
                    (UsbHostKeyboardStatusLogAt(0) << 1) ^
                    (UsbHostKeyboardStatusLogAt(1) << 3);
                if (KeyboardPaintSerial != LastKeyboardPaintSerial) {
                    LastKeyboardPaintSerial = KeyboardPaintSerial;
                    UsbHostPaintKeyboardStatus();
                }
            }
            /* 有键：1ms 节拍（勿 0、勿 20）；兼顾快打与根口稳定 */
            UsbHostStallMilliseconds(KeyboardSlotId != 0 ? 1u : 500u);
        }
    }
#undef PORT_STATUS_STABLE_MASK
}

#else

void BoardPixel8UsbHubWatch(void) {
}

#endif
