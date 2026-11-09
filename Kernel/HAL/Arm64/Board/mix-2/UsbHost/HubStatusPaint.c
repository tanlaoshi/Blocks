/*
 * UsbHost/HubStatusPaint.c — splash 根口 / Type-C / PON 状态行
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static void AppendCompactHex(char *Line, UINT32 Capacity, UINT32 Value) {
    char Hex[12];
    const char *Digits;

    UsbHostFormatHexadecimal32(Hex, Value);
    Digits = Hex + 2;
    while (Digits[0] == '0' && Digits[1] != '\0') {
        Digits++;
    }
    UsbHostAppendText(Line, Digits, Capacity);
}

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable,
                           UINT32 HubAddress) {
    char Line[56];
    UINT32 PortIndex;
    UINT8 LineState;
    int TypeCResult;

    (void)HubAddress;
    Line[0] = '\0';
    for (PortIndex = 1; PortIndex <= PortCount; PortIndex++) {
        char Piece[4];
        if (PortIndex > 1u) {
            UsbHostAppendText(Line, " ", sizeof(Line));
        }
        Piece[0] = 'p';
        Piece[1] = (char)('0' + (int)PortIndex);
        Piece[2] = ':';
        Piece[3] = '\0';
        UsbHostAppendText(Line, Piece, sizeof(Line));
        AppendCompactHex(Line, sizeof(Line), LastStable[PortIndex]);
    }
    UsbHostAppendText(Line, " e", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostEnumerateStatusCode());
    UsbHostAppendText(Line, " q", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostQscratchGeneralConfiguration());
    UsbHostAppendText(Line, " u", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostXhciUsb2PortBitMask());
    UsbHostAppendText(Line, "/", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostXhciUsb3PortBitMask());
    UsbHostAppendText(Line, " pon", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPowerOnStatusDiagnostic());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_PORTS, Line);

    LineState = UsbHostQusbLineStateRead();
    TypeCResult = UsbHostTypeCLastResult();
    UsbHostCopyText(Line, "ls ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), LineState);
    UsbHostAppendText(Line, " cl", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostQusbClampByte());
    UsbHostAppendText(Line, " tc", sizeof(Line));
    if (TypeCResult < 0) {
        UsbHostAppendText(Line, "-", sizeof(Line));
        AppendCompactHex(Line, sizeof(Line), (UINT32)(-TypeCResult));
    } else {
        AppendCompactHex(Line, sizeof(Line), (UINT32)TypeCResult);
    }
    UsbHostAppendText(Line, "/", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCControlByte());
    UsbHostAppendText(Line, " cc", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCStatus4Byte());
    UsbHostAppendText(Line, " c2 ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCStatus2Byte());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_TYPE_C, Line);

    UsbHostCopyText(Line, "os ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostOtgStatusByte());
    UsbHostAppendText(Line, " oe ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostCommandOtgByte());
    UsbHostAppendText(Line, " il ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostUsbinCommandByte());
    UsbHostAppendText(Line, " en ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostOtgEngineeringByte());
    UsbHostAppendText(Line, " of ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostOtgInterruptStatusByte());
    UsbHostAppendText(Line, " a ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCAnalogSwitchByte());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_OTG, Line);
}

#else

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable,
                           UINT32 HubAddress) {
    (void)PortCount;
    (void)LastStable;
    (void)HubAddress;
}

#endif
