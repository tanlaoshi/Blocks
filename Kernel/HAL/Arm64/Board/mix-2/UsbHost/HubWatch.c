/*
 * UsbHost/HubWatch.c — PR-MIX2-USB-3：hub → HID Boot → splash 回显
 *
 * 对表 Pixel HubWatch；保 Type-C Maintain + PON 短按。
 * §7.0：拔坞须 DisableSlot 拆树，插回再枚举；禁只清软件态。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static void TearDownDockTree(UINT32 *KeyboardSlotId, UINT32 *HubSlotId,
                             UINT32 *HubAddress, UINT32 *HubRootPort, UINT32 *HubSpeed,
                             UINT32 *HubPortCount, UINT32 *SawConnect,
                             UINT32 *EnumerateCooldown, UINT32 *HidScanCooldown) {
    if (KeyboardSlotId != 0 && *KeyboardSlotId != 0) {
        (void)UsbHostXhciDisableSlot(*KeyboardSlotId);
        *KeyboardSlotId = 0;
    }
    if (HubSlotId != 0 && *HubSlotId != 0) {
        (void)UsbHostXhciDisableSlot(*HubSlotId);
        *HubSlotId = 0;
    }
    UsbHostHidBootKeyboardLedsBind(0, 0, 0, 0);
    UsbHostInterruptRingInitialize(0, 0, 0);
    if (HubAddress != 0) {
        *HubAddress = 0;
    }
    if (HubRootPort != 0) {
        *HubRootPort = 0;
    }
    if (HubSpeed != 0) {
        *HubSpeed = 0;
    }
    if (HubPortCount != 0) {
        *HubPortCount = 0;
    }
    if (SawConnect != 0) {
        *SawConnect = 0;
    }
    if (EnumerateCooldown != 0) {
        *EnumerateCooldown = 0;
    }
    if (HidScanCooldown != 0) {
        *HidScanCooldown = 0;
    }
    UsbHostEnumerateStatusCodeClear();
    UsbHostKeyboardStatusCodeClear();
}

void BoardMix2UsbHubWatch(void) {
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

    if (UsbHostXhciStart() != 0) {
        for (;;) {
            UsbHostStallYieldPumpPower(1000000u);
        }
    }

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
    HubSlotId = 0;
    KeyboardSlotId = 0;
    HubRootPort = 0;
    HubSpeed = 0;
    HubPortCount = 0;
    SawConnect = 0;
    StatusPaintCount = 0;
    {
        UINT32 EnumerateCooldown = 0;
        UINT32 HidScanCooldown = 0;
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
        BoardBringUpSplashPaintLine(112, "wait dock");

        for (;;) {
            UINT32 Usb2Mask = UsbHostXhciUsb2PortBitMask();
            UINT32 PreferUsb2Port = 0;
            UINT32 AnyConnectPort = 0;
            UINT32 AnyConnect = 0;
            UINT32 KeyboardPaintSerial;

            (void)BoardPowerButtonPressed();
            UsbHostTypeCMaintainAfterAttach();

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
             * §7.0 热插：拔坞 → DisableSlot 拆树 → wait dock；插回再 enum。
             * 有键后偶发 CCS=0：连续多次才当真拔（对表 Pixel）。
             */
            if (AnyConnect == 0 && SawConnect != 0) {
                DisconnectStreak++;
                if (DisconnectStreak >= (KeyboardSlotId != 0 ? 64u : 8u)) {
                    TearDownDockTree(&KeyboardSlotId, &HubSlotId, &HubAddress,
                                     &HubRootPort, &HubSpeed, &HubPortCount,
                                     &SawConnect, &EnumerateCooldown, &HidScanCooldown);
                    DisconnectStreak = 0;
                    LastKeyboardPaintSerial = 0xFFFFFFFFu;
                    BoardBringUpSplashPaintLine(112, "wait dock");
                }
            } else {
                DisconnectStreak = 0;
            }

            if (HubSlotId == 0 && AnyConnectPort != 0 && EnumerateCooldown == 0) {
                UINT32 Target =
                    (PreferUsb2Port != 0) ? PreferUsb2Port : AnyConnectPort;
                SawConnect = 1;
                BoardBringUpSplashPaintLine(112, "usb enum");
                if (UsbHostEnumerateHubOnPort(Target, &HubAddress) == 0) {
                    HubSlotId = UsbHostHubSlotId();
                    HubRootPort = UsbHostHubRootPortIndex();
                    HubSpeed = UsbHostHubPortSpeed();
                    if (HubSlotId != 0 &&
                        UsbHostFinishHubSetup(HubSlotId, HubRootPort, HubSpeed,
                                             &HubPortCount) == 0) {
                        BoardBringUpSplashPaintLine(112, "usb hid");
                        UsbHostKeyboardStatusCodeClear();
                        (void)UsbHostFindBootKeyboardBehindHub(
                            HubSlotId, HubRootPort, HubSpeed, HubPortCount,
                            &KeyboardSlotId);
                        if (KeyboardSlotId != 0) {
                            UsbHostKeyboardLogFreeze();
                        } else {
                            HidScanCooldown = 10u;
                            BoardBringUpSplashPaintLine(112, "hid retry");
                        }
                    } else if (HubSlotId != 0) {
                        (void)UsbHostXhciDisableSlot(HubSlotId);
                        HubSlotId = 0;
                        HubAddress = 0;
                        EnumerateCooldown = 4u;
                        BoardBringUpSplashPaintLine(112, "hub setup");
                    }
                } else {
                    HubAddress = 0;
                    EnumerateCooldown = 4u;
                }
            } else if (HubSlotId != 0 && KeyboardSlotId == 0 && AnyConnect != 0 &&
                       HidScanCooldown == 0) {
                BoardBringUpSplashPaintLine(112, "usb hid");
                UsbHostKeyboardStatusCodeClear();
                (void)UsbHostFindBootKeyboardBehindHub(
                    HubSlotId, HubRootPort, HubSpeed, HubPortCount, &KeyboardSlotId);
                if (KeyboardSlotId != 0) {
                    UsbHostKeyboardLogFreeze();
                } else {
                    HidScanCooldown = 10u;
                    BoardBringUpSplashPaintLine(112, "hid retry");
                }
            }
            if (EnumerateCooldown > 0) {
                EnumerateCooldown--;
            }
            if (HidScanCooldown > 0) {
                HidScanCooldown--;
            }

            if (KeyboardSlotId != 0) {
                (void)UsbHostHidBootKeyboardPoll();
            }

            StatusPaintCount++;
            if (StatusPaintCount >= (KeyboardSlotId != 0 ? 64u : 1u)) {
                StatusPaintCount = 0;
                UsbHostPaintHubStatus(PortCount, LastStable, HubAddress);
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
            UsbHostStallMilliseconds(KeyboardSlotId != 0 ? 1u : 200u);
        }
    }
#undef PORT_STATUS_STABLE_MASK
}

#else

void BoardMix2UsbHubWatch(void) {
}

#endif
