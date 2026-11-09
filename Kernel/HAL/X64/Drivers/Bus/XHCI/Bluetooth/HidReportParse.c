/*
 * HidReportParse.c — Master HOGP 报告体 → dx/dy/buttons（PR-BTM-9）
 *
 * 初代 MX：常见 buttons(1|2) + X/Y **12 位打包** + wheel[+pan]。
 * 误当 16+16 时：纯 X 尚可、纯 Y 几乎不动（Y 残渣在错字节）。
 * 布局优先扫 Report Map（0x75 0x0C）；无图则按包长启发式。
 */
#include "XHCI/Bluetooth/Private.h"

#define HID_ITEM_SIZE_CODE_MASK 0x03u
#define HID_ITEM_TAG_MASK 0xFCu
#define HID_ITEM_REPORT_SIZE 0x74u
#define HID_ITEM_REPORT_COUNT 0x94u
#define HID_AXIS_BITS_12 12u
#define HID_AXIS_BITS_16 16u
/* 12 位±2047；÷8/÷2 仍蜗牛 → ÷1 原值（高 DPI 跟手） */
#define HID_AXIS_12_DIVISOR 1
#define HID_AXIS_16_DIVISOR 32

static UINT8 sButtonBytes = 1u;
static UINT8 sAxisBits = HID_AXIS_BITS_16;
static int sLayoutLogged;

static int SignExtend12(UINT32 Value) {
    Value &= 0xFFFu;
    if ((Value & 0x800u) != 0u) {
        return (int)Value - 0x1000;
    }
    return (int)Value;
}

static UINT32 ReadItemUnsigned(const UINT8 *Data, UINT8 SizeCode) {
    if (SizeCode == 0u || Data == 0) {
        return 0;
    }
    if (SizeCode == 1u) {
        return Data[0];
    }
    if (SizeCode == 2u) {
        return (UINT32)Data[0] | ((UINT32)Data[1] << 8);
    }
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) |
           ((UINT32)Data[3] << 24);
}

static UINT8 ItemDataLength(UINT8 SizeCode) {
    if (SizeCode == 3u) {
        return 4u;
    }
    return SizeCode;
}

/*
 * 短扫 Report Map：见 Report Size=12 → 12 位轴；见 Count=16 Size=1 → 两字节键。
 */
static void ApplyReportMap(const UINT8 *Map, UINT16 Length) {
    UINT16 Index = 0;
    UINT32 ReportSize = 8;
    UINT32 ReportCount = 1;
    int SawAxis12 = 0;
    int SawSixteenButtons = 0;

    sButtonBytes = 1u;
    sAxisBits = HID_AXIS_BITS_16;
    if (Map == 0 || Length == 0u) {
        return;
    }
    while (Index < Length) {
        UINT8 Prefix = Map[Index++];
        UINT8 SizeCode = (UINT8)(Prefix & HID_ITEM_SIZE_CODE_MASK);
        UINT8 Tag = (UINT8)(Prefix & HID_ITEM_TAG_MASK);
        UINT8 DataLength = ItemDataLength(SizeCode);
        UINT32 Value;

        if ((UINT32)Index + DataLength > Length) {
            break;
        }
        /* 长条目（0xFE）本刀不解析，跳过 */
        if (Prefix == 0xFEu) {
            if (DataLength == 0u || Index >= Length) {
                break;
            }
            DataLength = Map[Index];
            Index++;
            if ((UINT32)Index + DataLength > Length) {
                break;
            }
            Index = (UINT16)(Index + DataLength);
            continue;
        }
        Value = ReadItemUnsigned(&Map[Index], SizeCode);
        Index = (UINT16)(Index + DataLength);
        if (Tag == HID_ITEM_REPORT_SIZE) {
            ReportSize = Value;
        } else if (Tag == HID_ITEM_REPORT_COUNT) {
            ReportCount = Value;
        } else if (Tag == 0x80u) {
            /* Input：Size=12 Count=2 → X/Y；Size=1 Count=16 → 键位 */
            if (ReportSize == HID_AXIS_BITS_12 && ReportCount >= 2u) {
                SawAxis12 = 1;
            }
            if (ReportSize == 1u && ReportCount == 16u) {
                SawSixteenButtons = 1;
            }
        }
    }
    if (SawAxis12) {
        sAxisBits = HID_AXIS_BITS_12;
    }
    if (SawSixteenButtons) {
        sButtonBytes = 2u;
    }
}

static void ApplyLengthHeuristic(UINT16 PayloadLength) {
    /*
     * 无 Map 时：7 字节常见 = 2 键 + 3 轴 + wheel；6 = 1 键 + 3 轴 + wheel[+pan]。
     * 仍优先 12 位（Master）；短包才退 16/8。
     */
    if (PayloadLength >= 7u) {
        sAxisBits = HID_AXIS_BITS_12;
        if (sButtonBytes < 2u) {
            sButtonBytes = 2u;
        }
    } else if (PayloadLength >= 5u) {
        sAxisBits = HID_AXIS_BITS_12;
        sButtonBytes = 1u;
    } else {
        sAxisBits = 8u;
        sButtonBytes = 1u;
    }
}

void BluetoothHidReportLayoutRefresh(void) {
    const UINT8 *Map = 0;
    UINT16 MapLength = 0;

    sLayoutLogged = 0;
    sButtonBytes = 1u;
    sAxisBits = HID_AXIS_BITS_16;
    if (BluetoothAttHidReportMapGet(&Map, &MapLength) == 0 && MapLength > 0u) {
        ApplyReportMap(Map, MapLength);
        BootLogHex("bt: hid map axis=", sAxisBits, 2);
        BootLogHex(" btn=", sButtonBytes, 2);
        sLayoutLogged = 1;
        return;
    }
    /* 无 Map：等首包 Length 再定 */
    BootLog("bt: hid map missing; wait first report\n");
}

static void LogLayoutOnce(UINT16 PayloadLength) {
    if (sLayoutLogged) {
        return;
    }
    sLayoutLogged = 1;
    BootLogHex("bt: hid layout axis=", sAxisBits, 2);
    BootLogHex(" btn=", sButtonBytes, 2);
    BootLogHex(" len=", PayloadLength, 2);
}

int BluetoothHidReportParsePayload(const UINT8 *Payload, UINT16 Length,
                                   UINT8 *ButtonsOut, int *DeltaXOut,
                                   int *DeltaYOut, INT8 *WheelOut) {
    UINT8 Buttons;
    int DeltaX = 0;
    int DeltaY = 0;
    INT8 Wheel = 0;
    UINT32 Packed;
    UINT16 AxisOffset;

    if (Payload == 0 || Length < 3u || ButtonsOut == 0 || DeltaXOut == 0 ||
        DeltaYOut == 0 || WheelOut == 0) {
        return -1;
    }

    /* 首包且无 Map：按长度启发式钉 12 位 */
    if (!sLayoutLogged && sAxisBits == HID_AXIS_BITS_16) {
        ApplyLengthHeuristic(Length);
    }
    if (sButtonBytes == 0u || sButtonBytes > 2u) {
        sButtonBytes = 1u;
    }
    if (Length < sButtonBytes) {
        return -1;
    }

    Buttons = Payload[0];
    AxisOffset = sButtonBytes;

    if (sAxisBits == HID_AXIS_BITS_12 && Length >= (UINT16)(AxisOffset + 3u)) {
        Packed = (UINT32)Payload[AxisOffset] |
                 ((UINT32)Payload[AxisOffset + 1u] << 8) |
                 ((UINT32)Payload[AxisOffset + 2u] << 16);
        DeltaX = SignExtend12(Packed);
        DeltaY = SignExtend12(Packed >> 12);
        DeltaX /= HID_AXIS_12_DIVISOR;
        DeltaY /= HID_AXIS_12_DIVISOR;
        if (Length >= (UINT16)(AxisOffset + 4u)) {
            Wheel = (INT8)Payload[AxisOffset + 3u];
        }
    } else if (Length >= (UINT16)(AxisOffset + 4u)) {
        DeltaX = (int)(INT16)(Payload[AxisOffset] |
                              ((UINT16)Payload[AxisOffset + 1u] << 8));
        DeltaY = (int)(INT16)(Payload[AxisOffset + 2u] |
                              ((UINT16)Payload[AxisOffset + 3u] << 8));
        DeltaX /= HID_AXIS_16_DIVISOR;
        DeltaY /= HID_AXIS_16_DIVISOR;
        if (Length >= (UINT16)(AxisOffset + 5u)) {
            Wheel = (INT8)Payload[AxisOffset + 4u];
        }
    } else {
        DeltaX = (int)(INT8)Payload[1];
        DeltaY = (int)(INT8)Payload[2];
        if (Length >= 4u) {
            Wheel = (INT8)Payload[3];
        }
    }

    LogLayoutOnce(Length);
    *ButtonsOut = Buttons;
    *DeltaXOut = DeltaX;
    *DeltaYOut = DeltaY;
    *WheelOut = Wheel;
    return 0;
}
