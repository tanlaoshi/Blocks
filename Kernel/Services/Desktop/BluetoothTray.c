/*
 * BluetoothTray.c — 网托左侧蓝牙图标（PR-BT-TRAY-0）
 *
 * 资产：Assets/Icons/BLUETOOTH.BMP（黑底白符；黑=透明键）。
 * 状态色：已连=绿；Ready=主题色；未 Ready=暗红。
 */
#include "DesktopPrivate.h"
#include "HalBluetooth.h"

#define BT_TRAY_GAP 12u
#define BT_ICON 26u

static int sBluetoothTrayOpen;
static int sReadyCache;
static int sConnectedCache;

static void Fill(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color) {
    if (W && H) {
        GraphicsFillRectangle(X, Y, W, H, Color);
    }
}

/* 白符按状态色重绘；黑键跳过（对表 ICON_BITMAP_KEY_COLOR） */
static void DrawBluetoothIcon(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (!gBluetoothBmpReady || !gBluetoothBmp.Pixels) {
        return;
    }
    for (Row = 0; Row < BT_ICON; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = 0; Col <= BT_ICON; Col++) {
            int Opaque = 0;

            if (Col < BT_ICON) {
                Opaque = (BmpSampleScaled(&gBluetoothBmp, Col, Row, BT_ICON,
                                          BT_ICON) != 0u)
                             ? 1
                             : 0;
            }
            if (Opaque && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (!Opaque && InRun) {
                Fill(X + RunStart, Y + Row, Col - RunStart, 1u, Color);
                InRun = 0;
            }
        }
    }
}

void DesktopBluetoothTrayClose(void) {
    if (!sBluetoothTrayOpen) {
        return;
    }
    DesktopMarkBluetoothTrayPopup();
    DesktopMarkTaskbar();
    sBluetoothTrayOpen = 0;
    RequestRefresh();
}

void DesktopBluetoothTraySetOpen(int Open) {
    sBluetoothTrayOpen = Open ? 1 : 0;
}

int DesktopBluetoothTrayIsOpen(void) {
    return sBluetoothTrayOpen;
}

void DesktopBluetoothTrayGeom(UINT32 ClockX, UINT32 *OutX, UINT32 *OutW) {
    UINT32 NetX = 0;
    UINT32 NetW = 0;

    DesktopNetTrayGeom(ClockX, &NetX, &NetW);
    if (OutW) {
        *OutW = BT_ICON;
    }
    if (OutX) {
        *OutX = (NetX > BT_ICON + BT_TRAY_GAP) ? (NetX - BT_ICON - BT_TRAY_GAP)
                                               : 0;
    }
}

void DesktopBluetoothTrayDraw(UINT32 ClockX, UINT32 TextY) {
    UINT32 X = 0;
    UINT32 W = 0;
    UINT32 IconY;
    UINT32 Color;
    int Ready;
    int Connected;

    Ready = HalBluetoothReady();
    Connected = Ready ? HalBluetoothLeIsConnected() : 0;
    sReadyCache = Ready;
    sConnectedCache = Connected;

    DesktopBluetoothTrayGeom(ClockX, &X, &W);
    if (BT_ICON > FontCellH()) {
        IconY = (TextY > (BT_ICON - FontCellH()) / 2u)
                    ? (TextY - (BT_ICON - FontCellH()) / 2u)
                    : 0;
    } else {
        IconY = TextY + (FontCellH() - BT_ICON) / 2u;
    }
    if (Connected) {
        Color = 0x0024D024u;
    } else if (Ready) {
        Color = ThemeClockText();
    } else {
        Color = 0x00D02020u;
    }
    DrawBluetoothIcon(X, IconY, Color);
}

int DesktopBluetoothTrayLabelChanged(void) {
    int Ready = HalBluetoothReady();
    int Connected = Ready ? HalBluetoothLeIsConnected() : 0;

    if (Ready == sReadyCache && Connected == sConnectedCache) {
        return 0;
    }
    sReadyCache = Ready;
    sConnectedCache = Connected;
    return 1;
}
