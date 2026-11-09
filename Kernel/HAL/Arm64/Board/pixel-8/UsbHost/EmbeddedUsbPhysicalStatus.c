/*
 * UsbHost/EmbeddedUsbPhysicalStatus.c — eUSB CON/dig 状态字（cm/es/ls/pd）
 *
 * dig@0x11120000：0x84 UTMI in（dp/dm pulldown）；0x8c LineState；0x15c es。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define EMBEDDED_USB_COMMON_CONTROL_REGISTER 0x04u
#define EMBEDDED_USB_PHYSICAL_DIGITAL_BASE 0x11120000ULL
#define EMBEDDED_USB_STATE_JUMP_DEBUG_OUT_REGISTER 0x15Cu
#define EMBEDDED_USB_UNIVERSAL_TRANSCEIVER_MACROCELL_INPUT_ONE_REGISTER 0x84u
#define EMBEDDED_USB_UNIVERSAL_TRANSCEIVER_MACROCELL_OUTPUT_ZERO_REGISTER 0x8Cu
#define EMBEDDED_USB_LINE_STATE_MASK 0x3u
#define EMBEDDED_USB_DATA_PLUS_PULLDOWN (1u << 3)
#define EMBEDDED_USB_DATA_MINUS_PULLDOWN (1u << 4)

static UINT8 sPhysicalLastResultCode;
static UINT8 sPhysicalEmbeddedUsbState;
static UINT8 sPhysicalLineState;
static UINT8 sPhysicalUtmiPullDown;
static UINT32 sPhysicalCommonControl;
static UINT32 sPhysicalStrapControl;
static UINT32 sPhysicalFoundBasePage;

UINT8 UsbHostPhysicalLastResultCode(void) {
    return sPhysicalLastResultCode;
}

UINT8 UsbHostPhysicalEmbeddedUsbState(void) {
    return sPhysicalEmbeddedUsbState;
}

UINT8 UsbHostPhysicalLineState(void) {
    return sPhysicalLineState;
}

UINT8 UsbHostPhysicalUtmiPullDown(void) {
    return sPhysicalUtmiPullDown;
}

UINT32 UsbHostPhysicalCommonControl(void) {
    return sPhysicalCommonControl;
}

UINT32 UsbHostPhysicalStrapControl(void) {
    return sPhysicalStrapControl;
}

UINT32 UsbHostPhysicalFoundBasePage(void) {
    return sPhysicalFoundBasePage;
}

void UsbHostPhysicalStatusClear(void) {
    sPhysicalLastResultCode = 0;
    sPhysicalEmbeddedUsbState = 0;
    sPhysicalLineState = 0;
    sPhysicalUtmiPullDown = 0;
    sPhysicalCommonControl = 0;
    sPhysicalStrapControl = 0;
    sPhysicalFoundBasePage = 0;
}

void UsbHostPhysicalStatusSetResultCode(UINT8 ResultCode) {
    sPhysicalLastResultCode = ResultCode;
}

void UsbHostPhysicalStatusSetFoundBasePage(UINT32 Page) {
    sPhysicalFoundBasePage = Page;
}

void UsbHostPhysicalStatusSetStrapAndCommon(UINT32 Strap, UINT32 Common) {
    sPhysicalStrapControl = Strap;
    sPhysicalCommonControl = Common;
}

void UsbHostPhysicalStatusSetEmbeddedUsbState(UINT8 State) {
    sPhysicalEmbeddedUsbState = State;
}

void UsbHostPhysicalRefreshCommonControl(void) {
    UINT32 UtmiInputOne;
    UINT32 UtmiOutputZero;

    if (sPhysicalFoundBasePage == 0u) {
        return;
    }
    sPhysicalCommonControl = UsbHostReadMmio32(
        (((UINT64)sPhysicalFoundBasePage) << 12) +
        EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    sPhysicalEmbeddedUsbState = (UINT8)(UsbHostReadMmio32(
        EMBEDDED_USB_PHYSICAL_DIGITAL_BASE +
        EMBEDDED_USB_STATE_JUMP_DEBUG_OUT_REGISTER) &
                                        0xFFu);
    UtmiInputOne = UsbHostReadMmio32(
        EMBEDDED_USB_PHYSICAL_DIGITAL_BASE +
        EMBEDDED_USB_UNIVERSAL_TRANSCEIVER_MACROCELL_INPUT_ONE_REGISTER);
    UtmiOutputZero = UsbHostReadMmio32(
        EMBEDDED_USB_PHYSICAL_DIGITAL_BASE +
        EMBEDDED_USB_UNIVERSAL_TRANSCEIVER_MACROCELL_OUTPUT_ZERO_REGISTER);
    sPhysicalLineState =
        (UINT8)(UtmiOutputZero & EMBEDDED_USB_LINE_STATE_MASK);
    sPhysicalUtmiPullDown = 0;
    if ((UtmiInputOne & EMBEDDED_USB_DATA_PLUS_PULLDOWN) != 0u) {
        sPhysicalUtmiPullDown |= 1u;
    }
    if ((UtmiInputOne & EMBEDDED_USB_DATA_MINUS_PULLDOWN) != 0u) {
        sPhysicalUtmiPullDown |= 2u;
    }
}

#else

UINT8 UsbHostPhysicalLastResultCode(void) { return 0; }
UINT8 UsbHostPhysicalEmbeddedUsbState(void) { return 0; }
UINT8 UsbHostPhysicalLineState(void) { return 0; }
UINT8 UsbHostPhysicalUtmiPullDown(void) { return 0; }
UINT32 UsbHostPhysicalCommonControl(void) { return 0; }
UINT32 UsbHostPhysicalStrapControl(void) { return 0; }
UINT32 UsbHostPhysicalFoundBasePage(void) { return 0; }
void UsbHostPhysicalRefreshCommonControl(void) {}
void UsbHostPhysicalStatusClear(void) {}
void UsbHostPhysicalStatusSetResultCode(UINT8 ResultCode) { (void)ResultCode; }
void UsbHostPhysicalStatusSetFoundBasePage(UINT32 Page) { (void)Page; }
void UsbHostPhysicalStatusSetStrapAndCommon(UINT32 Strap, UINT32 Common) {
    (void)Strap;
    (void)Common;
}
void UsbHostPhysicalStatusSetEmbeddedUsbState(UINT8 State) { (void)State; }

#endif
