/*
 * UsbHost/HubChildKeyboard.c — hub 子口上电/复位 → 扫 Boot 键
 *
 * 一层：先扫非 hub 键（延后 26x），避免嵌套 FinishSetup 后兄口 GetDesc USBTE。
 * 二层：hub 仍立刻 FinishSetup + Route 下一级 nibble。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

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
    UINTN Settle;
    UINTN SettleRounds;
    static UINT32 sPoweredForHubSlot;
    int ProbeResult;
    UINT32 DeferredSlot[4];
    UINT32 DeferredPort[4];
    UINT32 DeferredSpeed[4];
    UINT32 DeferredCount = 0;
    UINT32 DeferredIndex;

    (void)HubPortSpeedValue;
    if (KeyboardSlotOut != 0) {
        *KeyboardSlotOut = 0;
    }
    if (HubPortCount == 0) {
        HubPortCount = 4u;
    }
    for (HubPort = 1; HubPort <= HubPortCount; HubPort++) {
        (void)UsbHostHubSetPortFeature(HubSlotId, HubPort, HUB_FEATURE_PORT_POWER);
    }
    /*
     * 一层坞内键晚亮 → 首扫约 2s；二层已在父口等过，短等即可
     * （长等 + 每口 GetDesc 5s 超时 → 屏上停在 2701）。
     */
    if (NestDepth != 0) {
        SettleRounds = 2u;
    } else {
        SettleRounds = (sPoweredForHubSlot == HubSlotId) ? 1u : 20u;
    }
    sPoweredForHubSlot = HubSlotId;
    for (Settle = 0; Settle < SettleRounds; Settle++) {
        UsbHostStallMilliseconds((NestDepth == 0 && SettleRounds > 1u) ? 100u : 50u);
        for (HubPort = 1; HubPort <= HubPortCount; HubPort++) {
            if (UsbHostHubGetPortStatus(HubSlotId, HubPort, &Status) != 0) {
                GetStatusFail++;
                continue;
            }
            if (HubPort == 1u) {
                FirstPortStatus = Status;
            }
            (void)UsbHostHubClearPortFeature(HubSlotId, HubPort,
                                             HUB_FEATURE_CONNECTION_CHANGE);
            if ((Status & HUB_PORT_CONNECTION) != 0) {
                SawConnect = 1;
                break;
            }
        }
        if (SawConnect != 0) {
            break;
        }
    }

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
        /* FS 在 Address 内一步 BSR0；HS 一律 BSR1→BSR0（勿再嵌套 addr0 后再 BSR0） */
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
            /* 一层延后嵌套：先把兄口键扫完（真机先嵌套后兄口 24x0x4） */
            if (NestDepth == 0 && DeferredCount < 4u) {
                DeferredSlot[DeferredCount] = KeyboardSlot;
                DeferredPort[DeferredCount] = HubPort;
                DeferredSpeed[DeferredCount] = DeviceSpeed;
                DeferredCount++;
                UsbHostKeyboardStatusCodeSet(0x2760u | (HubPort & 0xFu));
                continue;
            }
            if (TryClaimNestedHub(KeyboardSlot, RootPortIndex, HubPort, DeviceSpeed,
                                  ParentRouteString, NestDepth, KeyboardSlotOut) == 0) {
                return 0;
            }
        }
        (void)UsbHostXhciDisableSlot(KeyboardSlot);
    }
    for (DeferredIndex = 0; DeferredIndex < DeferredCount; DeferredIndex++) {
        if (TryClaimNestedHub(DeferredSlot[DeferredIndex], RootPortIndex,
                              DeferredPort[DeferredIndex], DeferredSpeed[DeferredIndex],
                              ParentRouteString, NestDepth, KeyboardSlotOut) == 0) {
            return 0;
        }
        (void)UsbHostXhciDisableSlot(DeferredSlot[DeferredIndex]);
    }
    if (SawConnect == 0) {
        if (GetStatusFail != 0 && FirstPortStatus == 0) {
            UsbHostKeyboardStatusCodeSet(0x21u | ((HubPortCount & 0xFu) << 8));
        } else {
            UsbHostKeyboardStatusCodeSet(
                0x2200u | ((HubPortCount & 0xFu) << 8) | (FirstPortStatus & 0xFFu));
        }
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
