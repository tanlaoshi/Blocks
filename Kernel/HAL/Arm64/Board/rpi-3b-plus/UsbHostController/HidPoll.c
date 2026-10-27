/*
 * HidPoll.c — 鼠优先 HID IN + Input 后端（bd-pi-usb）
 */
#include "UsbHostController/Private.h"

static void MouseEnqueue(UINT8 Buttons, INT8 Dx, INT8 Dy, INT8 Wheel) {
    UINT8 N = (UINT8)((gMouseQueueTail + 1u) % MOUSE_QUEUE_LENGTH);
    INT32 X;
    INT32 Y;
    UINT32 Sw = 0;
    UINT32 Sh = 0;
    INT32 MaxX;
    INT32 MaxY;

    if (N == gMouseQueueHead) {
        gMouseQueueHead = (UINT8)((gMouseQueueHead + 1u) % MOUSE_QUEUE_LENGTH);
    }
    /* 同 X64 UsbHid Mouse.c：相对鼠累加到屏像素；Absolute=0 交 Gui */
    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = 1920;
    }
    if (Sh == 0) {
        Sh = 1080;
    }
    MaxX = (INT32)(Sw > 0 ? Sw - 1u : 0u);
    MaxY = (INT32)(Sh > 0 ? Sh - 1u : 0u);
    if (!gMouseAbsoluteInitialized) {
        gMouseX = Sw / 2u;
        gMouseY = Sh / 2u;
        gMouseAbsoluteInitialized = 1;
    }
    X = (INT32)gMouseX + (INT32)Dx;
    Y = (INT32)gMouseY + (INT32)Dy;
    if (X < 0) {
        X = 0;
    }
    if (Y < 0) {
        Y = 0;
    }
    if (X > MaxX) {
        X = MaxX;
    }
    if (Y > MaxY) {
        Y = MaxY;
    }
    gMouseX = (UINT32)X;
    gMouseY = (UINT32)Y;
    gMouseQueueX[gMouseQueueTail] = gMouseX;
    gMouseQueueY[gMouseQueueTail] = gMouseY;
    gMouseQueueButtons[gMouseQueueTail] = Buttons;
    gMouseQueueWheel[gMouseQueueTail] = Wheel;
    gMouseQueueTail = N;
}

static void MousePollOnce(void) {
    UINT8 Buffer[8];
    int Ret;
    UINT8 PacketId;

    if (!gMouseOk) {
        return;
    }
    gTransferDoSplit = gMouseSplit;
    gTransferLowSpeed = gMouseLowSpeed;
    gTransferHub = gMouseHub;
    gTransferPort = gMousePort;
    PacketId = gMouseToggle;
    Ret = ChunkMessage(gMouseAddress, gMouseEndpoint, DWC2_HCCHAR_EPTYPE_INTR, 1, &PacketId, Buffer,
                   (int)gMouseMaxPacketSize > 8 ? 8 : (int)gMouseMaxPacketSize, gMouseMaxPacketSize);
    if (Ret == -2) {
        return; /* NAK：无报告 */
    }
    if (Ret < 3) {
        return;
    }
    gMouseToggle = PacketId;
    MouseEnqueue(Buffer[0], (INT8)Buffer[1], (INT8)Buffer[2],
                 Ret >= 4 ? (INT8)Buffer[3] : 0);
}

static void KeyboardEnqueue(const UINT8 *Report8) {
    UINT8 N = (UINT8)((gKeyboardQueueTail + 1u) % KEYBOARD_QUEUE_LENGTH);
    int i;

    if (!Report8) {
        return;
    }
    if (N == gKeyboardQueueHead) {
        return; /* 满：丢新保旧（同 X64 KbdPush） */
    }
    gKeyboardQueue[gKeyboardQueueTail].ModifierKeys = Report8[0];
    gKeyboardQueue[gKeyboardQueueTail].Reserved = Report8[1];
    for (i = 0; i < 6; i++) {
        gKeyboardQueue[gKeyboardQueueTail].KeyCode[i] = Report8[2 + i];
    }
    gKeyboardQueueTail = N;
}

static void KeyboardPollOnce(void) {
    UINT8 Buffer[8];
    int Ret;
    UINT8 PacketId;
    int Length;

    if (!gKeyboardOk) {
        return;
    }
    gTransferDoSplit = gKeyboardSplit;
    gTransferLowSpeed = gKeyboardLowSpeed;
    gTransferHub = gKeyboardHub;
    gTransferPort = gKeyboardPort;
    PacketId = gKeyboardToggle;
    Length = (int)gKeyboardMaxPacketSize > 8 ? 8 : (int)gKeyboardMaxPacketSize;
    if (Length < 8) {
        Length = 8; /* boot 报告 8 字节 */
    }
    Ret = ChunkMessage(gKeyboardAddress, gKeyboardEndpoint, DWC2_HCCHAR_EPTYPE_INTR, 1, &PacketId, Buffer, Length,
                   gKeyboardMaxPacketSize ? gKeyboardMaxPacketSize : 8);
    if (Ret == -2) {
        return;
    }
    if (Ret < 3) {
        return;
    }
    gKeyboardToggle = PacketId;
    KeyboardEnqueue(Buffer);
}

/* 鼠优先：先鼠后键，勿让键 IN 饿死鼠 */
void HidPoll(void) {
    MousePollOnce();
    KeyboardPollOnce();
}

void BoardUsbHostControllerMouseHandoff(UINT32 CursorX, UINT32 CursorY) {
    gMouseX = CursorX;
    gMouseY = CursorY;
    gMouseAbsoluteInitialized = 1;
    gMouseQueueHead = gMouseQueueTail; /* 丢积压，避免 Reveal 后飞角 */
}

static int MousePresentWrap(void) {
    return gMouseOk;
}

static int MouseDequeueWrap(HAL_MOUSE_REPORT *Report) {
    if (!Report || gMouseQueueHead == gMouseQueueTail) {
        return 0;
    }
    Report->X = gMouseQueueX[gMouseQueueHead];
    Report->Y = gMouseQueueY[gMouseQueueHead];
    Report->Buttons = gMouseQueueButtons[gMouseQueueHead];
    Report->Wheel = gMouseQueueWheel[gMouseQueueHead];
    Report->Absolute = 0;
    gMouseQueueHead = (UINT8)((gMouseQueueHead + 1u) % MOUSE_QUEUE_LENGTH);
    return 1;
}

static int KeyboardDequeueWrap(HAL_KEYBOARD_REPORT *Report) {
    if (!Report || gKeyboardQueueHead == gKeyboardQueueTail) {
        return 0;
    }
    *Report = gKeyboardQueue[gKeyboardQueueHead];
    gKeyboardQueueHead = (UINT8)((gKeyboardQueueHead + 1u) % KEYBOARD_QUEUE_LENGTH);
    return 1;
}

const INPUT_BACKEND gInputBackend = {
    .Poll = HidPoll,
    .KeyboardDequeue = KeyboardDequeueWrap,
    .KeyboardSetLeds = 0,
    .MousePresent = MousePresentWrap,
    .MouseDequeue = MouseDequeueWrap,
};
