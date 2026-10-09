/*
 * Gui.c — K12 桌面壳 + K17 光标/顶栏点击
 *
 * 【初学者】
 * 桌面：底色 + 顶栏 + 标题（色来自 Theme）。
 * 指针：PS/2 相对鼠 → XOR 箭头光标；左键点顶栏打串口日志。
 */
#include "Gui.h"
#include "BootInfo.h"
#include "HalPs2Mouse.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Font.h"
#include "Theme.h"
#include "ToySerialConfig.h"

#define GUI_BAR_H     28u
#define GUI_CUR_XOR   0x00FFFFFFu
#define GUI_CUR_W     8u
#define GUI_CUR_H     14u

static int gDesktopReady;
static int gCursorOn;
static INT32 gCurX;
static INT32 gCurY;
static UINT8 gPrevButtons;
static UINT32 gFbW;
static UINT32 gFbH;

/* 每行 8bit，高位在左 */
static const UINT8 gArrow[GUI_CUR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xF8, 0xDC, 0x8E, 0x06, 0x03, 0x01
};

static void CursorXorAt(INT32 X, INT32 Y) {
    UINT32 Row;
    UINT32 Col;

    if (!gCursorOn || gFbW == 0) {
        return;
    }
    for (Row = 0; Row < GUI_CUR_H; Row++) {
        UINT8 Bits = gArrow[Row];
        for (Col = 0; Col < GUI_CUR_W; Col++) {
            if (Bits & (UINT8)(0x80u >> Col)) {
                INT32 Px = X + (INT32)Col;
                INT32 Py = Y + (INT32)Row;
                if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW &&
                    (UINT32)Py < gFbH) {
                    HalVideoXorPixelRaw((UINT32)Px, (UINT32)Py, GUI_CUR_XOR);
                }
            }
        }
    }
}

static void CursorClamp(void) {
    if (gCurX < 0) {
        gCurX = 0;
    }
    if (gCurY < 0) {
        gCurY = 0;
    }
    if (gFbW > 0 && (UINT32)gCurX >= gFbW) {
        gCurX = (INT32)(gFbW - 1u);
    }
    if (gFbH > 0 && (UINT32)gCurY >= gFbH) {
        gCurY = (INT32)(gFbH - 1u);
    }
}

int GuiDesktopReady(void) {
    return gDesktopReady;
}

int GuiInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gDesktopReady = 0;
    gCursorOn = 0;
    gPrevButtons = 0;
    gFbW = 0;
    gFbH = 0;
    HalVideoGetSize(&gFbW, &gFbH);
    if (Info == 0 || Info->FrameBufferSize == 0 || gFbW < 160 || gFbH < 80) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: skip (no FB)\n");
        return 0;
    }

    ThemeInitialize();
    FontInitialize();
    HalVideoFillRect(0, 0, gFbW, gFbH, ThemeDesktopBackground());
    HalVideoFillRect(0, 0, gFbW, GUI_BAR_H, ThemeTaskbarBackground());
    FontDrawStringAt(12, 8, "积木", ThemeWindowTitleText()); /* 积木 */
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }

    gDesktopReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: desktop ok\n");

#if defined(__x86_64__) || defined(_M_X64)
    if (HalPs2MouseInit() == 0 && HalPs2MouseReady()) {
        gCurX = (INT32)(gFbW / 2u);
        gCurY = (INT32)(gFbH / 2u);
        gCursorOn = 1;
        CursorXorAt(gCurX, gCurY);
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: mouse ok\n");
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: cursor on\n");
    } else {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: mouse skip\n");
    }
#endif
    return 0;
}

void GuiPoll(void) {
    HAL_MOUSE_PACKET Pkt;
    INT32 Nx;
    INT32 Ny;
    int Moved;

    if (!gDesktopReady || !gCursorOn) {
        return;
    }
    while (HalPs2MousePoll(&Pkt)) {
        Nx = gCurX + Pkt.Dx;
        Ny = gCurY + Pkt.Dy;
        Moved = (Nx != gCurX || Ny != gCurY);
        if (Moved) {
            CursorXorAt(gCurX, gCurY);
            gCurX = Nx;
            gCurY = Ny;
            CursorClamp();
            CursorXorAt(gCurX, gCurY);
        }
        /* 左键按下沿 */
        if ((Pkt.Buttons & 0x1u) != 0 && (gPrevButtons & 0x1u) == 0) {
            if ((UINT32)gCurY < GUI_BAR_H) {
                HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: bar click @");
                HalSerialWriteChannelHex32(TOY_SLOG_GUI, (UINT32)gCurX);
                HalSerialWriteChannel(TOY_SLOG_GUI, ",");
                HalSerialWriteChannelHex32(TOY_SLOG_GUI, (UINT32)gCurY);
                HalSerialWriteChannel(TOY_SLOG_GUI, "\n");
            } else {
                HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: click @");
                HalSerialWriteChannelHex32(TOY_SLOG_GUI, (UINT32)gCurX);
                HalSerialWriteChannel(TOY_SLOG_GUI, ",");
                HalSerialWriteChannelHex32(TOY_SLOG_GUI, (UINT32)gCurY);
                HalSerialWriteChannel(TOY_SLOG_GUI, "\n");
            }
        }
        gPrevButtons = Pkt.Buttons;
    }
}
