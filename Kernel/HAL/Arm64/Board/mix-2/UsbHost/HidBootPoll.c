/*
 * UsbHost/HidBootPoll.c — 中断报告 → splash 回显
 *
 * KEY.＝等中断；KEY>＝出字。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define KEYBOARD_LINE_CAPACITY 36u
/* KEY / k 紧接 tc1（USB_HOST_SPLASH_LINE_KEYBOARD） */
#define KEYBOARD_LINE_TOP USB_HOST_SPLASH_LINE_KEYBOARD

static char sKeyboardLine[KEYBOARD_LINE_CAPACITY];
static UINT32 sKeyboardLineLength;
static UINT8 sPreviousReport[8];
static UINT8 sPreviousReportValid;
static UINT8 sIdleEchoPainted;

static int UsageWasPressed(const UINT8 *Report, UINT8 Usage) {
    UINTN Index;

    for (Index = 2; Index < 8u; Index++) {
        if (Report[Index] == Usage) {
            return 1;
        }
    }
    return 0;
}

static void AppendAscii(char Ascii) {
    if (Ascii == '\b') {
        if (sKeyboardLineLength > 0) {
            sKeyboardLineLength--;
            sKeyboardLine[sKeyboardLineLength] = '\0';
        }
        return;
    }
    if (Ascii == '\n') {
        sKeyboardLineLength = 0;
        sKeyboardLine[0] = '\0';
        return;
    }
    if (Ascii == 0) {
        return;
    }
    if (sKeyboardLineLength + 1u >= KEYBOARD_LINE_CAPACITY) {
        sKeyboardLineLength = 0;
        sKeyboardLine[0] = '\0';
    }
    sKeyboardLine[sKeyboardLineLength] = Ascii;
    sKeyboardLineLength++;
    sKeyboardLine[sKeyboardLineLength] = '\0';
}

static void PaintKeyboardEcho(const char *Prefix) {
    char Line[48];

    UsbHostCopyText(Line, Prefix, sizeof(Line));
    UsbHostAppendText(Line, sKeyboardLine, sizeof(Line));
    (void)BoardBringUpSplashPaintLine(KEYBOARD_LINE_TOP, Line);
}

static void ConsumeBootReport(const UINT8 *Report, const char *Prefix) {
    UINT8 Modifier;
    UINTN Index;

    Modifier = Report[0];
    for (Index = 2; Index < 8u; Index++) {
        UINT8 Usage = Report[Index];
        char Ascii;

        if (Usage == 0) {
            continue;
        }
        if (sPreviousReportValid != 0 && UsageWasPressed(sPreviousReport, Usage) != 0) {
            continue;
        }
        /* Caps/NumLock 等开关键回 0：勿填 '#' */
        Ascii = UsbHostHidBootUsageToAscii(Usage, Modifier);
        if (Ascii != 0) {
            AppendAscii(Ascii);
        }
    }
    for (Index = 0; Index < 8u; Index++) {
        sPreviousReport[Index] = Report[Index];
    }
    sPreviousReportValid = 1;
    PaintKeyboardEcho(Prefix);
}

void UsbHostPaintKeyboardStatus(void) {
    char Line[48];
    UINT32 Count = UsbHostKeyboardStatusLogCount();
    UINT32 Index;
    /* KEY 下一行起排 k，满行再 +64；勿盖 p1…tc */
    UINT32 LineTop = KEYBOARD_LINE_TOP + USB_HOST_SPLASH_LINE_STRIDE;
    UINT32 OnLine = 0;

    Line[0] = '\0';
    UsbHostAppendText(Line, "k", sizeof(Line));
    if (Count == 0) {
        char Hex[12];
        const char *Digits;
        UsbHostAppendText(Line, " ", sizeof(Line));
        UsbHostFormatHexadecimal32(Hex, UsbHostKeyboardStatusCode());
        Digits = Hex + 2;
        while (Digits[0] == '0' && Digits[1] != '\0') {
            Digits++;
        }
        UsbHostAppendText(Line, Digits, sizeof(Line));
        (void)BoardBringUpSplashPaintLine(LineTop, Line);
        return;
    }
    for (Index = 0; Index < Count; Index++) {
        char Hex[12];
        const char *Digits;
        if (OnLine >= 3u) {
            (void)BoardBringUpSplashPaintLine(LineTop, Line);
            LineTop += USB_HOST_SPLASH_LINE_STRIDE;
            Line[0] = '\0';
            UsbHostAppendText(Line, "k", sizeof(Line));
            OnLine = 0;
        }
        UsbHostFormatHexadecimal32(Hex, UsbHostKeyboardStatusLogAt(Index));
        Digits = Hex + 2;
        while (Digits[0] == '0' && Digits[1] != '\0') {
            Digits++;
        }
        UsbHostAppendText(Line, " ", sizeof(Line));
        UsbHostAppendText(Line, Digits, sizeof(Line));
        OnLine++;
    }
    (void)BoardBringUpSplashPaintLine(LineTop, Line);
}

int UsbHostHidBootKeyboardPoll(void) {
    UINT8 *Report;
    UINT32 CompletionCode;
    UINTN Burst;
    int GotReport = -1;

    /*
     * 连抽多份报告：快打时按下/抬起可能夹在两次慢轮询之间。
     * 超时勿每圈重画 KEY.（SplashPaintLine 清行极慢，会饿死收包）。
     */
    for (Burst = 0; Burst < 8u; Burst++) {
        if (UsbHostInterruptSubmitOnceDetailed(&CompletionCode) != 0) {
            break;
        }
        Report = UsbHostInterruptReportBuffer();
        if (Report == 0) {
            break;
        }
        ConsumeBootReport(Report, "KEY>");
        sIdleEchoPainted = 0;
        GotReport = 0;
    }
    if (GotReport != 0 && sIdleEchoPainted == 0) {
        PaintKeyboardEcho("KEY.");
        sIdleEchoPainted = 1u;
    }
    return GotReport;
}

#else

int UsbHostHidBootKeyboardPoll(void) {
    return -1;
}

void UsbHostPaintKeyboardStatus(void) {
}

#endif
