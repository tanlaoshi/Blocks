/*
 * BluetoothTrayPopup.c — 蓝牙托盘弹层：Scan / Connect / Disconnect（PR-BT-TRAY-0）
 *
 * Scan/Connect 会短时占用（鼠优先切片在 Hal 内）；状态行显示结果。
 */
#include "DesktopPrivate.h"
#include "HalBluetooth.h"

#define BT_ICON 26u
#define BT_POP_PAD 8u
#define BT_POP_MIN_W 220u
#define BT_SCAN_MAX 8u
#define BT_SCAN_TIMEOUT_MS 6000u
#define BT_CONNECT_TIMEOUT_MS 20000u

static HAL_BLUETOOTH_SCAN_ENTRY sScanTable[BT_SCAN_MAX];
static UINTN sScanCount;
static char sStatusLine[48];

static void StrCopy(char *Dst, int Max, const char *Src) {
    int Index = 0;

    if (Dst == 0 || Max <= 0) {
        return;
    }
    while (Src && Src[Index] && Index < Max - 1) {
        Dst[Index] = Src[Index];
        Index++;
    }
    Dst[Index] = 0;
}

static void ClockAnchor(UINT32 *ClockXOut) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 ClockW;
    char Clock[8];

    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)BarY;
    Clock[0] = '0';
    Clock[1] = '0';
    Clock[2] = ':';
    Clock[3] = '0';
    Clock[4] = '0';
    Clock[5] = 0;
    ClockW = FontStringWidth(Clock);
    *ClockXOut = (Sw > ClockW + 12u) ? (Sw - ClockW - 12u) : 0;
}

static UINT32 PopupLineCount(void) {
    /* title + status + Scan + Disconnect + rows */
    return 4u + (UINT32)sScanCount;
}

static void PopupGeom(UINT32 *Px, UINT32 *Py, UINT32 *Pw, UINT32 *Ph) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 H;
    UINT32 W;

    TaskbarGeom(&BarY, &Sw, &Sh);
    H = BT_POP_PAD * 2u + PopupLineCount() * FontCellH();
    W = BT_POP_MIN_W;
    if (Sw > 8u && W + 8u > Sw) {
        W = Sw - 8u;
    }
    *Pw = W;
    *Ph = H;
    *Px = (Sw > W + 8u) ? (Sw - W - 8u) : 0;
    *Py = (BarY > H + 4u) ? (BarY - H - 4u) : 0;
}

void DesktopMarkBluetoothTrayPopup(void) {
    UINT32 Px;
    UINT32 Py;
    UINT32 Pw;
    UINT32 Ph;

    PopupGeom(&Px, &Py, &Pw, &Ph);
    DesktopMarkRect(Px, Py, Pw, Ph);
}

static void FormatScanRow(UINTN OneBased, char *Out, int Max) {
    const HAL_BLUETOOTH_SCAN_ENTRY *Entry;
    char Line[56];
    UINTN Index;
    int Pos = 0;

    if (Out == 0 || Max <= 0 || OneBased == 0u || OneBased > sScanCount) {
        if (Out && Max > 0) {
            Out[0] = 0;
        }
        return;
    }
    Entry = &sScanTable[OneBased - 1u];
    Line[Pos++] = (char)('0' + (int)(OneBased % 10u));
    Line[Pos++] = ' ';
    if (Entry->Name[0]) {
        for (Index = 0; Entry->Name[Index] && Pos < (int)sizeof(Line) - 1;
             Index++) {
            Line[Pos++] = Entry->Name[Index];
        }
    } else {
        /* 无名：显示地址末两字节 */
        static const char Hex[] = "0123456789ABCDEF";
        Line[Pos++] = Hex[(Entry->Address[1] >> 4) & 0xFu];
        Line[Pos++] = Hex[Entry->Address[1] & 0xFu];
        Line[Pos++] = ':';
        Line[Pos++] = Hex[(Entry->Address[0] >> 4) & 0xFu];
        Line[Pos++] = Hex[Entry->Address[0] & 0xFu];
    }
    Line[Pos] = 0;
    StrCopy(Out, Max, Line);
}

void DesktopBluetoothTrayDrawPopup(void) {
    UINT32 Px;
    UINT32 Py;
    UINT32 Pw;
    UINT32 Ph;
    UINT32 Ty;
    UINTN Index;
    char Row[56];

    if (!DesktopBluetoothTrayIsOpen()) {
        return;
    }
    PopupGeom(&Px, &Py, &Pw, &Ph);
    GraphicsFillRectangle(Px, Py, Pw, Ph, ThemeControlFace());
    GraphicsDrawRectangle(Px, Py, Pw, Ph, ThemeWindowBorderFocus());
    Ty = Py + BT_POP_PAD;
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, "Bluetooth", ThemeWindowTitleText());
    Ty += FontCellH();
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty,
                         sStatusLine[0] ? sStatusLine : "(idle)",
                         ThemeTextMuted());
    Ty += FontCellH();
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, "Scan", ThemeClockText());
    Ty += FontCellH();
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, "Disconnect", ThemeClockText());
    Ty += FontCellH();
    for (Index = 1u; Index <= sScanCount; Index++) {
        FormatScanRow(Index, Row, (int)sizeof(Row));
        HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, Row, ThemeClockText());
        Ty += FontCellH();
    }
}

static int TrayHit(UINT32 X, UINT32 Y, UINT32 ClockX, UINT32 BarY) {
    UINT32 Bx;
    UINT32 Bw;
    UINT32 Ty;
    UINT32 Th;

    DesktopBluetoothTrayGeom(ClockX, &Bx, &Bw);
    Th = FontCellH();
    if (Th < BT_ICON) {
        Th = BT_ICON;
    }
    Ty = BarY + (TASKBAR_H > Th ? (TASKBAR_H - Th) / 2u : 0);
    return (Y >= Ty && Y < Ty + Th && X >= Bx && X < Bx + Bw) ? 1 : 0;
}

static int PopupRowAt(UINT32 Y, UINT32 Py) {
    UINT32 Rel;

    if (Y < Py + BT_POP_PAD) {
        return -1;
    }
    Rel = (Y - Py - BT_POP_PAD) / FontCellH();
    return (int)Rel;
}

static void RunScan(void) {
    int Count;

    StrCopy(sStatusLine, (int)sizeof(sStatusLine), "scanning...");
    DesktopMarkBluetoothTrayPopup();
    RequestRefresh();
    Count = HalBluetoothLeScan(sScanTable, BT_SCAN_MAX, BT_SCAN_TIMEOUT_MS);
    if (Count < 0) {
        sScanCount = 0;
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "scan fail");
        return;
    }
    sScanCount = (UINTN)Count;
    if (sScanCount == 0u) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "scan: 0");
    } else {
        sStatusLine[0] = 's';
        sStatusLine[1] = 'c';
        sStatusLine[2] = 'a';
        sStatusLine[3] = 'n';
        sStatusLine[4] = ':';
        sStatusLine[5] = ' ';
        sStatusLine[6] = (char)('0' + (int)(sScanCount % 10u));
        sStatusLine[7] = 0;
    }
}

static void RunConnect(UINTN OneBased) {
    StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connecting...");
    DesktopMarkBluetoothTrayPopup();
    RequestRefresh();
    if (HalBluetoothMouseConnect(OneBased, BT_CONNECT_TIMEOUT_MS) != 0) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connect fail");
        return;
    }
    StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connected");
}

static void RunDisconnect(void) {
    if (HalBluetoothMouseDisconnect() != 0) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "disconnect fail");
        return;
    }
    StrCopy(sStatusLine, (int)sizeof(sStatusLine), "disconnected");
}

static void HandlePopupClick(UINT32 X, UINT32 Y) {
    UINT32 Px;
    UINT32 Py;
    UINT32 Pw;
    UINT32 Ph;
    int Row;

    (void)X;
    PopupGeom(&Px, &Py, &Pw, &Ph);
    Row = PopupRowAt(Y, Py);
    /* 0 title 1 status 2 Scan 3 Disconnect 4… scan rows */
    if (Row == 2) {
        if (!HalBluetoothReady()) {
            StrCopy(sStatusLine, (int)sizeof(sStatusLine), "not ready");
        } else {
            RunScan();
        }
    } else if (Row == 3) {
        RunDisconnect();
    } else if (Row >= 4 && (UINTN)(Row - 3) <= sScanCount) {
        RunConnect((UINTN)(Row - 3));
    }
    DesktopMarkBluetoothTrayPopup();
    DesktopMarkTaskbar();
    RequestRefresh();
}

int DesktopBluetoothTrayHandleClick(UINT32 X, UINT32 Y) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 ClockX;
    UINT32 Px;
    UINT32 Py;
    UINT32 Pw;
    UINT32 Ph;

    TaskbarGeom(&BarY, &Sw, &Sh);
    ClockAnchor(&ClockX);

    if (DesktopBluetoothTrayIsOpen()) {
        PopupGeom(&Px, &Py, &Pw, &Ph);
        if (X >= Px && X < Px + Pw && Y >= Py && Y < Py + Ph) {
            HandlePopupClick(X, Y);
            return 1;
        }
        DesktopMarkBluetoothTrayPopup();
        DesktopMarkTaskbar();
        DesktopBluetoothTraySetOpen(0);
        RequestRefresh();
        if (Y >= BarY && Y < Sh) {
            return 1;
        }
        return 0;
    }

    if (Y < BarY || Y >= Sh || !TrayHit(X, Y, ClockX, BarY)) {
        return 0;
    }
    gMenuOpen = 0;
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    DesktopNetTrayClose();
    if (!HalBluetoothReady()) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "not ready");
    } else if (HalBluetoothLeIsConnected()) {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "connected");
    } else {
        StrCopy(sStatusLine, (int)sizeof(sStatusLine), "ready");
    }
    DesktopBluetoothTraySetOpen(1);
    DesktopMarkBluetoothTrayPopup();
    DesktopMarkTaskbar();
    RequestRefresh();
    return 1;
}
