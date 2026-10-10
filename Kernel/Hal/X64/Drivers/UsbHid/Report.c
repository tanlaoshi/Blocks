/*
 * Report.c — HID boot 报告解析（键盘扫键、鼠标相对位移）
 *
 * 【初学者】
 * - UsbHid 子模块：入队 gCharQueue / gMouseQueue；Gui Poll 读取。
 */
#include "Internal.h"

static char HidUsageToAscii(UINT8 Usage, int Shift) {
    static const char Letters[] = "abcdefghijklmnopqrstuvwxyz";
    static const char Digits[] = "1234567890";
    static const char DigitsS[] = "!@#$%^&*()";

    if (Usage >= 4u && Usage <= 29u) {
        char C = Letters[Usage - 4u];
        if (Shift) {
            C = (char)(C - 'a' + 'A');
        }
        return C;
    }
    if (Usage >= 30u && Usage <= 39u) {
        return Shift ? DigitsS[Usage - 30u] : Digits[Usage - 30u];
    }
    if (Usage == 40u) {
        return '\n';
    }
    if (Usage == 42u) {
        return '\b';
    }
    if (Usage == 43u) {
        return '\t';
    }
    if (Usage == 44u) {
        return ' ';
    }
    if (Usage == 45u) {
        return Shift ? '_' : '-';
    }
    if (Usage == 46u) {
        return Shift ? '+' : '=';
    }
    if (Usage == 47u) {
        return Shift ? '{' : '[';
    }
    if (Usage == 48u) {
        return Shift ? '}' : ']';
    }
    if (Usage == 49u) {
        return Shift ? '|' : '\\';
    }
    if (Usage == 51u) {
        return Shift ? ':' : ';';
    }
    if (Usage == 52u) {
        return Shift ? '"' : '\'';
    }
    if (Usage == 54u) {
        return Shift ? '<' : ',';
    }
    if (Usage == 55u) {
        return Shift ? '>' : '.';
    }
    if (Usage == 56u) {
        return Shift ? '?' : '/';
    }
    return 0;
}

static int KeyWasDown(const UINT8 *Prev, UINT8 Usage) {
    int i;
    for (i = 2; i < 8; i++) {
        if (Prev[i] == Usage) {
            return 1;
        }
    }
    return 0;
}

void ReportKeyboard(const UINT8 *Report) {
    int Shift = (Report[0] & 0x22u) != 0;
    int i;

    for (i = 2; i < 8; i++) {
        UINT8 U = Report[i];
        char Ch;
        if (U == 0 || KeyWasDown(gKeyboard.PreviousKeys, U)) {
            continue;
        }
        Ch = HidUsageToAscii(U, Shift);
        if (Ch != 0 && gCharLength < sizeof(gCharQueue)) {
            gCharQueue[gCharLength++] = Ch;
        }
    }
    for (i = 0; i < 8; i++) {
        gKeyboard.PreviousKeys[i] = Report[i];
    }
}

void ReportMouse(USB_HID_DEVICE *Device, UINT8 TransferLength) {
    HAL_MOUSE_PACKET Pkt;
    UINT8 *B = Device->Report;
    UINT8 Next;

    MemoryZero(&Pkt, sizeof(Pkt));
    if (Device->Absolute && Device->Protocol != 2u) {
        UINT32 X0 = (UINT32)(B[1] | (B[2] << 8));
        UINT32 Y0 = (UINT32)(B[3] | (B[4] << 8));
        if (TransferLength >= 5u && X0 <= 32767u && Y0 <= 32767u) {
            Pkt.Buttons = B[0] & 7u;
            Pkt.Dx = (INT32)X0;
            Pkt.Dy = (INT32)Y0;
            Pkt.Absolute = 1;
        } else {
            return;
        }
    } else {
        Pkt.Buttons = B[0] & 7u;
        Pkt.Dx = (INT32)(INT8)B[1];
        Pkt.Dy = (INT32)(INT8)B[2];
        Pkt.Absolute = 0;
    }

    Next = (UINT8)((gMouseQueueWrite + 1u) % 16u);
    if (Next == gMouseQueueRead) {
        return;
    }
    gMouseQueue[gMouseQueueWrite] = Pkt;
    gMouseQueueWrite = Next;
}
