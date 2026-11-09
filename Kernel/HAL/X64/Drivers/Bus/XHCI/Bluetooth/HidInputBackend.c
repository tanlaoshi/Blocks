/*
 * HidInputBackend.c — HOGP Notify → HAL_MOUSE_REPORT（PR-BTM-8）
 *
 * 第二 INPUT_BACKEND：与 USB 鼠并存、后到驱动光标；不占 USB 槽。
 * DrainEvents/Worker 已 HostPump——本 Poll 只消化 Notify，禁止再泵 ACL（双跑抢环）。
 * 解析：Boot 8 位或 16 位 LE；有/无 Report Id 都试（Master 细表留 BTM-9）。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#include "DriverInput.h"
#include "HalVideo.h"
#include "SpinLock.h"

#define BLUETOOTH_HID_MOUSE_QUEUE 32u
#define ATT_HANDLE_VALUE_NOTIFICATION 0x1Bu
#define ATT_HANDLE_VALUE_INDICATION 0x1Du

static SPIN_LOCK sBluetoothHidLock;
static HAL_MOUSE_REPORT sMouseQueue[BLUETOOTH_HID_MOUSE_QUEUE];
static UINT32 sMouseReadIndex;
static UINT32 sMouseWriteIndex;
static int sAbsoluteInitialized;
static int sAbsoluteX;
static int sAbsoluteY;
static int sBackendAttached;
static int sNotifyArmed;
static UINT16 sArmedReportValueHandle;
static UINT8 sArmedReportId;
static int sLoggedFirstReport;

void BluetoothHidInputNoteArmed(UINT16 ReportValueHandle, UINT8 ReportId) {
    sArmedReportValueHandle = ReportValueHandle;
    sArmedReportId = ReportId;
    sNotifyArmed = (ReportValueHandle != 0u) ? 1 : 0;
    sLoggedFirstReport = 0;
    if (!sNotifyArmed) {
        return;
    }
    BootLogHex("bt: hid armed handle=", ReportValueHandle, 4);
    BootLogHex(" id=", ReportId, 2);
}

void BluetoothHidInputHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    SpinLockAcquire(&sBluetoothHidLock);
    sAbsoluteX = (int)CursorX;
    sAbsoluteY = (int)CursorY;
    sAbsoluteInitialized = 1;
    sMouseReadIndex = sMouseWriteIndex;
    SpinLockRelease(&sBluetoothHidLock);
}

static void PushRelative(int DeltaX, int DeltaY, UINT8 Buttons, INT8 Wheel) {
    UINT32 Next = (sMouseWriteIndex + 1u) % BLUETOOTH_HID_MOUSE_QUEUE;
    UINT32 ScreenWidth = 0;
    UINT32 ScreenHeight = 0;
    int MaxX;
    int MaxY;
    HAL_MOUSE_REPORT *Report;

    if (Next == sMouseReadIndex) {
        return;
    }
    HalVideoGetSize(&ScreenWidth, &ScreenHeight);
    if (ScreenWidth == 0u) {
        ScreenWidth = 1024u;
    }
    if (ScreenHeight == 0u) {
        ScreenHeight = 768u;
    }
    MaxX = (int)(ScreenWidth > 0u ? ScreenWidth - 1u : 0u);
    MaxY = (int)(ScreenHeight > 0u ? ScreenHeight - 1u : 0u);
    if (!sAbsoluteInitialized) {
        sAbsoluteX = (int)(ScreenWidth / 2u);
        sAbsoluteY = (int)(ScreenHeight / 2u);
        sAbsoluteInitialized = 1;
    }
    sAbsoluteX += DeltaX;
    sAbsoluteY += DeltaY;
    if (sAbsoluteX < 0) {
        sAbsoluteX = 0;
    }
    if (sAbsoluteY < 0) {
        sAbsoluteY = 0;
    }
    if (sAbsoluteX > MaxX) {
        sAbsoluteX = MaxX;
    }
    if (sAbsoluteY > MaxY) {
        sAbsoluteY = MaxY;
    }
    Report = &sMouseQueue[sMouseWriteIndex];
    Report->X = (UINT32)sAbsoluteX;
    Report->Y = (UINT32)sAbsoluteY;
    Report->Buttons = (UINT8)(Buttons & 7u);
    Report->Wheel = Wheel;
    Report->Absolute = 0;
    sMouseWriteIndex = Next;
}

static void LogFirstReport(const UINT8 *Value, UINT16 ValueLength) {
    UINT16 Index;
    UINT16 Show = ValueLength;

    if (sLoggedFirstReport || Value == 0) {
        return;
    }
    sLoggedFirstReport = 1;
    if (Show > 12u) {
        Show = 12u;
    }
    BootLogHex("bt: hid report len=", ValueLength, 4);
    BootLog(" data=");
    for (Index = 0; Index < Show; Index++) {
        BootLogHex(Index == 0u ? "" : " ", Value[Index], 2);
    }
    BootLog(ValueLength > Show ? "...\n" : "\n");
}

/*
 * Payload = 报告体（已去掉 ATT 头）。
 * Master HOGP：buttons + X16 + Y16 [+wheel][+pan]。
 * 旧启发式「高字节=0/0xFF → 当 8 位」会在小位移时把 Dy 读成 X 高字节（≈0），
 * 表现为左右正常、上下极慢。Length≥5 一律 16 位；÷32 对齐有线（细表 BTM-9）。
 */
#define BLUETOOTH_HID_AXIS_16_DIVISOR 32

static int PayloadLooksLikeAxis16(const UINT8 *Payload, UINT16 Length) {
    (void)Payload;
    return (Length >= 5u) ? 1 : 0;
}

static void PushFromPayload(const UINT8 *Payload, UINT16 Length) {
    UINT8 Buttons;
    int DeltaX;
    int DeltaY;
    INT8 Wheel = 0;

    if (Payload == 0 || Length < 3u) {
        return;
    }
    Buttons = Payload[0];
    if (PayloadLooksLikeAxis16(Payload, Length)) {
        DeltaX = (int)(INT16)(Payload[1] | ((UINT16)Payload[2] << 8));
        DeltaY = (int)(INT16)(Payload[3] | ((UINT16)Payload[4] << 8));
        DeltaX /= BLUETOOTH_HID_AXIS_16_DIVISOR;
        DeltaY /= BLUETOOTH_HID_AXIS_16_DIVISOR;
        if (Length >= 6u) {
            Wheel = (INT8)Payload[5];
        }
    } else {
        DeltaX = (int)(INT8)Payload[1];
        DeltaY = (int)(INT8)Payload[2];
        if (Length >= 4u) {
            Wheel = (INT8)Payload[3];
        }
    }
    PushRelative(DeltaX, DeltaY, Buttons, Wheel);
}

static void ParseAndPush(const UINT8 *Value, UINT16 ValueLength) {
    if (Value == 0 || ValueLength < 3u) {
        return;
    }
    LogFirstReport(Value, ValueLength);
    /*
     * HOGP：有的机把 Report Id 放在值首字节，有的只在 Reference 里。
     * 两种都试——先按武装 Id 剥一层，再整包当报告体。
     */
    if (sArmedReportId != 0u && Value[0] == sArmedReportId &&
        ValueLength >= 4u) {
        PushFromPayload(&Value[1], (UINT16)(ValueLength - 1u));
        return;
    }
    PushFromPayload(Value, ValueLength);
}

static void DrainNotifications(void) {
    UINT8 AttributeProtocolDataUnit[32];
    UINT16 Length = 0;
    UINT32 Guard = 16u;

    while (Guard-- > 0u) {
        UINT16 AttributeHandle;
        UINT16 ValueLength;

        if (!BluetoothAttNotifyQueueTake(AttributeProtocolDataUnit,
                                         sizeof(AttributeProtocolDataUnit),
                                         &Length)) {
            break;
        }
        if (Length < 4u) {
            continue;
        }
        if (AttributeProtocolDataUnit[0] != ATT_HANDLE_VALUE_NOTIFICATION &&
            AttributeProtocolDataUnit[0] != ATT_HANDLE_VALUE_INDICATION) {
            continue;
        }
        AttributeHandle = (UINT16)(AttributeProtocolDataUnit[1] |
                                   ((UINT16)AttributeProtocolDataUnit[2] << 8));
        if (sArmedReportValueHandle != 0u &&
            AttributeHandle != sArmedReportValueHandle) {
            continue;
        }
        ValueLength = (UINT16)(Length - 3u);
        ParseAndPush(&AttributeProtocolDataUnit[3], ValueLength);
    }
}

static void BluetoothHidInputPoll(void) {
    if (!sBackendAttached || !sNotifyArmed) {
        return;
    }
    if (!BluetoothLeIsConnected()) {
        return;
    }
    /*
     * 勿 HostPump：USB InputPoll→DrainEvents 与 Worker/IRQ 已泵。
     * 此处再泵 = 与 Drain 双跑抢 ACL 组装态，有线鼠与 BT 报告一起死。
     */
    BluetoothRequeueHid();
    SpinLockAcquire(&sBluetoothHidLock);
    DrainNotifications();
    SpinLockRelease(&sBluetoothHidLock);
}

static int BluetoothHidMousePresent(void) {
    return (sBackendAttached && sNotifyArmed && BluetoothLeIsConnected()) ? 1
                                                                          : 0;
}

static int BluetoothHidMouseDequeue(HAL_MOUSE_REPORT *Report) {
    int Ok = 0;

    if (Report == 0 || !sBackendAttached) {
        return 0;
    }
    SpinLockAcquire(&sBluetoothHidLock);
    if (sMouseReadIndex != sMouseWriteIndex) {
        *Report = sMouseQueue[sMouseReadIndex];
        sMouseReadIndex =
            (sMouseReadIndex + 1u) % BLUETOOTH_HID_MOUSE_QUEUE;
        Ok = 1;
    }
    SpinLockRelease(&sBluetoothHidLock);
    return Ok;
}

static const INPUT_BACKEND gBluetoothHidInputBackend = {
    .Poll = BluetoothHidInputPoll,
    .KeyboardDequeue = 0,
    .KeyboardSetLeds = 0,
    .MousePresent = BluetoothHidMousePresent,
    .MouseDequeue = BluetoothHidMouseDequeue,
};

int BluetoothHidInputAttach(void) {
    if (!sNotifyArmed) {
        BootLog("bt: hid input need notify first\n");
        return -1;
    }
    BluetoothAttNotifyAcceptSet(1);
    sBackendAttached = 1;
    if (DriverInputAttach(&gBluetoothHidInputBackend) < 0) {
        sBackendAttached = 0;
        BootLog("bt: hid input attach fail\n");
        return -1;
    }
    BootLog("bt: hid input backend on\n");
    return 0;
}

void XhciBluetoothHidInputHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    BluetoothHidInputHandoffDesktop(CursorX, CursorY);
}
