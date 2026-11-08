/*
 * UsbHost/HubStatusPaint.c — splash 根口 / ls / tc / sp 状态行
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

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable) {
    char Line[56];
    UINT32 PortIndex;
    UINT8 LineState;
    int TypeCResult;

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
    BoardBringUpSplashPaintLine(208, Line);

    LineState = UsbHostQusbLineStateRead();
    TypeCResult = UsbHostTypeCLastResult();
    UsbHostCopyText(Line, "ls ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), LineState);
    UsbHostAppendText(Line, " tc", sizeof(Line));
    if (TypeCResult < 0) {
        UsbHostAppendText(Line, "-", sizeof(Line));
        AppendCompactHex(Line, sizeof(Line), (UINT32)(-TypeCResult));
    } else {
        AppendCompactHex(Line, sizeof(Line), (UINT32)TypeCResult);
    }
    UsbHostAppendText(Line, "/", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCControlByte());
    /* cc=STATUS_4：bit0 attach、bit4 debounce、bit6 vbus、bit7 dfp */
    UsbHostAppendText(Line, " cc", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCStatus4Byte());
    BoardBringUpSplashPaintLine(256, Line);

    /* og=OTG_CFG（bit1 须为 0=软件控）；c2=STATUS_2（DFP Rd 探测） */
    UsbHostCopyText(Line, "og ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostOtgConfigurationByte());
    UsbHostAppendText(Line, " c2 ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCStatus2Byte());
    BoardBringUpSplashPaintLine(304, Line);
}

#else

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable) {
    (void)PortCount;
    (void)LastStable;
}

#endif
