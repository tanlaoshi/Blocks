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
    /* e=枚举粘性码（A1=hub）；q=QSCRATCH general（勿另起 PaintLine 叠字） */
    UsbHostAppendText(Line, " e", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostEnumerateStatusCode());
    UsbHostAppendText(Line, " q", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostQscratchGeneralConfiguration());
    BoardBringUpSplashPaintLine(160, Line);

    LineState = UsbHostQusbLineStateRead();
    TypeCResult = UsbHostTypeCLastResult();
    UsbHostCopyText(Line, "ls ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), LineState);
    /* cl=tcsr_clamp_dig_n：期望 1（解钳） */
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
    /* cc=STATUS_4；c2=DFP Rd；挤在可视带内 */
    UsbHostAppendText(Line, " cc", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCStatus4Byte());
    UsbHostAppendText(Line, " c2 ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCStatus2Byte());
    BoardBringUpSplashPaintLine(280, Line);

    /* os bit3=softstart；of bit0=fail；en=ENG halt 回读 */
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
    /* a=WCD USBC analog OUT：bit0=en1 bit1=en2n；USB idle 期望 a=2 */
    UsbHostAppendText(Line, " a ", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCAnalogSwitchByte());
    BoardBringUpSplashPaintLine(400, Line);
}

#else

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable) {
    (void)PortCount;
    (void)LastStable;
}

#endif
