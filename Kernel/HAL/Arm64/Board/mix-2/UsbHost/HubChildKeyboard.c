/*
 * UsbHost/HubChildKeyboard.c — hub 子口上电/复位 → 扫 Boot 键
 *
 * 绿联坞：USB-A 在一级口上的二级 hub；兄口曾出 3-config 假 HID（a2+KEY. 无字）。
 * 见 hub（26x）立刻嵌套扫；勿延后（Pixel 兄口优先不适用于本坞）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static int ResetHubChildPort(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT32 *StatusOut) {
    UINT32 Status;
    UINTN Guard;

    (void)UsbHostHubSetPortFeature(HubSlotId, HubPortOneBased, HUB_FEATURE_PORT_RESET);
    for (Guard = 0; Guard < 200u; Guard++) {
        UsbHostStallMilliseconds(1u);
        if (UsbHostHubGetPortStatus(HubSlotId, HubPortOneBased, &Status) != 0) {
            return -1;
        }
        if (((Status >> 16) & (1u << 4)) != 0) {
            break;
        }
        if ((Status & HUB_PORT_ENABLE) != 0 && (Status & HUB_PORT_CONNECTION) != 0) {
            break;
        }
    }
    (void)UsbHostHubClearPortFeature(HubSlotId, HubPortOneBased, HUB_FEATURE_RESET_CHANGE);
    (void)UsbHostHubClearPortFeature(HubSlotId, HubPortOneBased,
                                     HUB_FEATURE_CONNECTION_CHANGE);
    if (UsbHostHubGetPortStatus(HubSlotId, HubPortOneBased, &Status) != 0) {
        return -1;
    }
    if ((Status & HUB_PORT_CONNECTION) == 0 || (Status & HUB_PORT_ENABLE) == 0) {
        return -1;
    }
    UsbHostStallMilliseconds(50u);
    if (UsbHostHubGetPortStatus(HubSlotId, HubPortOneBased, &Status) != 0) {
        return -1;
    }
    if (StatusOut != 0) {
        *StatusOut = Status;
    }
    return 0;
}

static int FindBootKeyboardBehindHubRouted(UINT32 HubSlotId, UINT32 RootPortIndex,
                                           UINT32 HubPortSpeedValue, UINT32 HubPortCount,
                                           UINT32 ParentRouteString, UINT32 NestDepth,
                                           UINT32 *KeyboardSlotOut);

static int TryClaimNestedHub(UINT32 NestedSlot, UINT32 RootPort, UINT32 HubPort,
                             UINT32 DeviceSpeed, UINT32 ParentRouteString,
                             UINT32 NestDepth, UINT32 *KeyboardSlotOut) {
    UINT32 NestedPortCount = 0;
    UINT32 NestedRoute;
    UINT32 RouteShift = 0;

    if (NestDepth >= 1u) {
        UsbHostKeyboardStatusCodeSet(0x2620u | (HubPort & 0xFu));
        return -1;
    }
    /* AddressKeyboardOnHubPort 已 BSR1→BSR0；此处只 FinishSetup */
    if (UsbHostFinishHubSetup(NestedSlot, RootPort, DeviceSpeed, &NestedPortCount) !=
        0) {
        UsbHostKeyboardStatusCodeSet(0x2630u | (HubPort & 0xFu));
        return -1;
    }
    /* 口数单独码，避免与 begin/fail 撞 nibble */
    UsbHostKeyboardStatusCodeSet(0x2720u | (NestedPortCount & 0xFFu));
    while (RouteShift < 20u && ((ParentRouteString >> RouteShift) & 0xFu) != 0u) {
        RouteShift += 4u;
    }
    NestedRoute =
        (ParentRouteString & 0xFFFFFu) | (((HubPort & 0xFu) << RouteShift));
    /* 延后 FinishSetup 后给下游口上电结算（真机嵌套口曾 341044 USBTE） */
    UsbHostStallMilliseconds(100u);
    UsbHostProbeTierSet(1u);
    UsbHostKeyboardStatusCodeSet(0x2700u | (HubPort & 0xFu));
    if (FindBootKeyboardBehindHubRouted(NestedSlot, RootPort, DeviceSpeed,
                                        NestedPortCount, NestedRoute, NestDepth + 1u,
                                        KeyboardSlotOut) == 0) {
        UsbHostProbeTierSet(0);
        return 0;
    }
    UsbHostProbeTierSet(0);
    UsbHostKeyboardStatusCodeSet(0x2710u | (HubPort & 0xFu));
    return -1;
}

static int FindBootKeyboardBehindHubRouted(UINT32 HubSlotId, UINT32 RootPortIndex,
                                           UINT32 HubPortSpeedValue, UINT32 HubPortCount,
                                           UINT32 ParentRouteString, UINT32 NestDepth,
                                           UINT32 *KeyboardSlotOut) {
    UINT32 HubPort;
    UINT32 Status = 0;
    UINT32 DeviceSpeed;
    UINT32 KeyboardSlot;
    UINT32 SawConnect = 0;
    UINT32 FirstPortStatus = 0;
    UINT32 GetStatusFail = 0;
    UINT32 SettleMilliseconds;
    static UINT32 sPoweredForHubSlot;
    int ProbeResult;

    (void)HubPortSpeedValue;
    if (KeyboardSlotOut != 0) {
        *KeyboardSlotOut = 0;
    }
    if (HubPortCount == 0) {
        HubPortCount = 4u;
    }
    UsbHostKeyboardStatusCodeSet(0x2800u | (HubPortCount & 0xFFu));
    for (HubPort = 1; HubPort <= HubPortCount; HubPort++) {
        (void)UsbHostHubSetPortFeature(HubSlotId, HubPort, HUB_FEATURE_PORT_POWER);
    }
    /*
     * 读卡器常先亮、键晚百 ms：整段 Stall 一次，禁止 settle 里狂 GetStatus/Clear
     * （控制超时 5s×口×轮 → 屏死锁在 280x）。重扫短等；二层更短。
     */
    if (NestDepth != 0) {
        SettleMilliseconds = 100u;
    } else {
        SettleMilliseconds = (sPoweredForHubSlot == HubSlotId) ? 800u : 1500u;
    }
    sPoweredForHubSlot = HubSlotId;
    UsbHostStallMilliseconds(SettleMilliseconds);
    UsbHostKeyboardStatusCodeSet(0x2810u | (HubPortCount & 0xFFu));

    for (HubPort = 1; HubPort <= HubPortCount; HubPort++) {
        if (UsbHostHubGetPortStatus(HubSlotId, HubPort, &Status) != 0) {
            GetStatusFail++;
            continue;
        }
        if (HubPort == 1u) {
            FirstPortStatus = Status;
        }
        if ((Status & HUB_PORT_CONNECTION) == 0) {
            continue;
        }
        SawConnect = 1;
        UsbHostKeyboardStatusCodeSet(
            ((NestDepth != 0) ? 0x3730u : 0x2730u) | (HubPort & 0xFu));
        if (ResetHubChildPort(HubSlotId, HubPort, &Status) != 0) {
            UsbHostKeyboardStatusCodeSet(
                ((NestDepth != 0) ? 0x330u : 0x230u) | (HubPort & 0xFu));
            continue;
        }
        DeviceSpeed = UsbHostChildSlotSpeed(Status);
        /* 一层/二级均一步 BSR0（真机 BSR1 独发后槽停 4410 Default，ConfigEP 非法） */
        if (UsbHostAddressKeyboardOnHubPort(
                HubSlotId, RootPortIndex, ParentRouteString, HubPort, DeviceSpeed, 1u,
                &KeyboardSlot) != 0) {
            UsbHostKeyboardStatusCodeSet(
                ((NestDepth != 0) ? 0x3740u : 0x2740u) | (HubPort & 0xFu));
            continue;
        }
        UsbHostKeyboardStatusCodeSet(
            ((NestDepth != 0) ? 0x3750u : 0x2750u) | (HubPort & 0xFu));
        ProbeResult = UsbHostProbeChildSlot(KeyboardSlot, RootPortIndex, HubSlotId,
                                               HubPort, DeviceSpeed);
        if (ProbeResult == 0) {
            if (KeyboardSlotOut != 0) {
                *KeyboardSlotOut = KeyboardSlot;
            }
            return 0;
        }
        if (ProbeResult == 1) {
            UsbHostKeyboardStatusCodeSet(0x2760u | (HubPort & 0xFu));
            if (TryClaimNestedHub(KeyboardSlot, RootPortIndex, HubPort, DeviceSpeed,
                                  ParentRouteString, NestDepth, KeyboardSlotOut) == 0) {
                return 0;
            }
        }
        (void)UsbHostXhciDisableSlot(KeyboardSlot);
    }
    if (SawConnect == 0) {
        if (GetStatusFail != 0 && FirstPortStatus == 0) {
            UsbHostKeyboardStatusCodeSet(0x21u | ((HubPortCount & 0xFu) << 8));
        } else {
            UsbHostKeyboardStatusCodeSet(
                0x2200u | ((HubPortCount & 0xFu) << 8) | (FirstPortStatus & 0xFFu));
        }
    } else {
        UINT32 ConnectMask = 0;

        for (HubPort = 1; HubPort <= HubPortCount; HubPort++) {
            if (UsbHostHubGetPortStatus(HubSlotId, HubPort, &Status) != 0) {
                continue;
            }
            if ((Status & HUB_PORT_CONNECTION) != 0) {
                ConnectMask |= (1u << (HubPort & 0xFu));
            }
        }
        /* 46xx＝有 CONNECT 但无 Boot 键；低字节＝口位图（bitN=口 N） */
        UsbHostKeyboardStatusCodeSet(0x4600u | (ConnectMask & 0xFFu));
    }
    return -1;
}

int UsbHostFindBootKeyboardBehindHub(UINT32 HubSlotId, UINT32 RootPortIndex,
                                     UINT32 HubPortSpeedValue, UINT32 HubPortCount,
                                     UINT32 *KeyboardSlotOut) {
    return FindBootKeyboardBehindHubRouted(HubSlotId, RootPortIndex, HubPortSpeedValue,
                                           HubPortCount, 0, 0, KeyboardSlotOut);
}

#else

int UsbHostFindBootKeyboardBehindHub(UINT32 HubSlotId, UINT32 RootPortIndex,
                                     UINT32 HubPortSpeedValue, UINT32 HubPortCount,
                                     UINT32 *KeyboardSlotOut) {
    (void)HubSlotId;
    (void)RootPortIndex;
    (void)HubPortSpeedValue;
    (void)HubPortCount;
    (void)KeyboardSlotOut;
    return -1;
}

#endif
