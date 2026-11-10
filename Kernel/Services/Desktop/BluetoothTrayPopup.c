/*
 * BluetoothTrayPopup.c — 蓝牙托盘弹层：Scan / Connect / Disconnect
 *
 * Scan/Connect 后台（BluetoothTrayScan.c）；Disconnect 仍同步。
 */
#include "DesktopPrivate.h"
#include "HalBluetooth.h"

#define BT_ICON 26u
#define BT_POP_PAD 8u
#define BT_POP_MIN_W 220u

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
    return 4u + (UINT32)DesktopBluetoothTrayScanCount();
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

void DesktopBluetoothTrayDrawPopup(void) {
    UINT32 Px;
    UINT32 Py;
    UINT32 Pw;
    UINT32 Ph;
    UINT32 Ty;
    UINTN Index;
    UINTN ScanCount;
    char Row[56];
    const char *Status;

    if (!DesktopBluetoothTrayIsOpen()) {
        return;
    }
    PopupGeom(&Px, &Py, &Pw, &Ph);
    GraphicsFillRectangle(Px, Py, Pw, Ph, ThemeControlFace());
    GraphicsDrawRectangle(Px, Py, Pw, Ph, ThemeWindowBorderFocus());
    Ty = Py + BT_POP_PAD;
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, "Bluetooth", ThemeWindowTitleText());
    Ty += FontCellH();
    Status = DesktopBluetoothTrayStatusGet();
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, Status[0] ? Status : "(idle)",
                         ThemeTextMuted());
    Ty += FontCellH();
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, "Scan", ThemeClockText());
    Ty += FontCellH();
    HalVideoDrawStringAt(Px + BT_POP_PAD, Ty, "Disconnect", ThemeClockText());
    Ty += FontCellH();
    ScanCount = DesktopBluetoothTrayScanCount();
    for (Index = 1u; Index <= ScanCount; Index++) {
        DesktopBluetoothTrayFormatScanRow(Index, Row, (int)sizeof(Row));
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

static void RunDisconnect(void) {
    if (HalBluetoothMouseDisconnect() != 0) {
        DesktopBluetoothTrayStatusSet("disconnect fail");
        return;
    }
    DesktopBluetoothTrayStatusSet("disconnected");
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
            DesktopBluetoothTrayStatusSet("not ready");
        } else {
            DesktopBluetoothTrayScanRequest();
        }
    } else if (Row == 3) {
        if (DesktopBluetoothTrayScanIsBusy()) {
            DesktopBluetoothTrayStatusSet("busy");
        } else {
            RunDisconnect();
        }
    } else if (Row >= 4 &&
               (UINTN)(Row - 3) <= DesktopBluetoothTrayScanCount()) {
        DesktopBluetoothTrayConnectRequest((UINTN)(Row - 3));
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
        DesktopBluetoothTrayStatusSet("not ready");
    } else if (HalBluetoothLeIsConnected()) {
        DesktopBluetoothTrayStatusSet("connected");
    } else {
        DesktopBluetoothTrayStatusSet("ready");
    }
    DesktopBluetoothTraySetOpen(1);
    DesktopMarkBluetoothTrayPopup();
    DesktopMarkTaskbar();
    RequestRefresh();
    return 1;
}
