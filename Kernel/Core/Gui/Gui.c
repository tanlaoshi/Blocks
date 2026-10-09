/*
 * Gui.c — 桌面壳：光标 + 鼠轮询；窗体见 GuiWin（K32 双窗 Z 序）
 *
 * 【初学者】
 * 点窗抬升；点 × 关；点顶栏重开已关窗；Shell 焦点才吃键（Console）。
 */
#include "Gui.h"
#include "GuiDesktop.h"
#include "GuiFiles.h"
#include "GuiLayout.h"
#include "GuiSettings.h"
#include "GuiStart.h"
#include "GuiWin.h"
#include "BootInfo.h"
#include "Console.h"
#include "HalPs2.h"
#include "HalPs2Mouse.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Font.h"
#include "FontTtf.h"
#include "Locale.h"
#include "Theme.h"
#include "ToySerialConfig.h"

#define GUI_CUR_COLOR 0x00FFFFFFu
#define GUI_CUR_W     8u
#define GUI_CUR_H     14u

static int gDesktopReady;
static int gDragging;
static int gDragWin;
static INT32 gDragOffX;
static INT32 gDragOffY;
static int gCursorOn;
static int gCursorShown;
static INT32 gCurX;
static INT32 gCurY;
static UINT8 gPrevButtons;
static UINT32 gDragIdle;
static UINT32 gFbW;
static UINT32 gFbH;
static UINT32 gUnder[GUI_CUR_H][GUI_CUR_W];
static INT32 gSaveX;
static INT32 gSaveY;
static UINT32 gSaveW;
static UINT32 gSaveH;

static const UINT8 gArrow[GUI_CUR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xF8, 0xDC, 0x8E, 0x06, 0x03, 0x01
};

/*
 * 光标：底图采自背缓冲，像素双写背+前，不 PresentRect（避停鼠半包）。
 */
void GuiCursorHide(void) {
    UINT32 Row;
    UINT32 Col;

    if (!gCursorShown) {
        return;
    }
    for (Row = 0; Row < gSaveH && Row < GUI_CUR_H; Row++) {
        for (Col = 0; Col < gSaveW && Col < GUI_CUR_W; Col++) {
            INT32 Px = gSaveX + (INT32)Col;
            INT32 Py = gSaveY + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                UINT32 C = gUnder[Row][Col];
                HalVideoDrawPixel((UINT32)Px, (UINT32)Py, C);
                HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py, C);
            }
        }
    }
    gCursorShown = 0;
}

void GuiCursorShow(void) {
    UINT32 Row;
    UINT32 Col;

    if (!gCursorOn || gCursorShown || gFbW == 0) {
        return;
    }
    if (!HalVideoBackbufferEnabled()) {
        return;
    }
    gSaveX = gCurX;
    gSaveY = gCurY;
    gSaveW = GUI_CUR_W;
    gSaveH = GUI_CUR_H;
    for (Row = 0; Row < GUI_CUR_H; Row++) {
        UINT8 Bits = gArrow[Row];
        for (Col = 0; Col < GUI_CUR_W; Col++) {
            INT32 Px = gSaveX + (INT32)Col;
            INT32 Py = gSaveY + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                UINT32 Under = HalVideoReadPixel((UINT32)Px, (UINT32)Py);
                gUnder[Row][Col] = Under;
                if (Bits & (UINT8)(0x80u >> Col)) {
                    HalVideoDrawPixel((UINT32)Px, (UINT32)Py, GUI_CUR_COLOR);
                    HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py, GUI_CUR_COLOR);
                } else {
                    HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py, Under);
                }
            } else {
                gUnder[Row][Col] = 0;
            }
        }
    }
    gCursorShown = 1;
}

static void CursorHide(void) {
    GuiCursorHide();
}

static void CursorShow(void) {
    GuiCursorShow();
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

int GuiShellWindowReady(void) {
    return GuiWinIsOn(GUI_WIN_SHELL);
}

int GuiShellFocused(void) {
    return GuiWinFocused(GUI_WIN_SHELL);
}

int GuiShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    return GuiWinShellClientRect(X, Y, W, H);
}

int GuiInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gDesktopReady = 0;
    gDragging = 0;
    gDragWin = -1;
    gCursorOn = 0;
    gCursorShown = 0;
    gPrevButtons = 0;
    gFbW = 0;
    gFbH = 0;
    HalVideoGetSize(&gFbW, &gFbH);
    if (Info == 0 || Info->FrameBufferSize == 0 || gFbW < 160 || gFbH < 80) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: skip (no FB)\n");
        return 0;
    }

    ThemeInitialize();
    (void)ThemeLoadCfg(); /* K37：色/mode；无文件则出厂色 */
    GuiLayoutSetFb(gFbW, gFbH);
    (void)GuiLayoutLoadCfg(); /* 窗几何描述；无则内建表 */
    FontInitialize();
    LocaleInitialize();
    FontTtfPreheatUtf8("积木系统已就绪命令窗说明设置点色块改主题文件运行开始");
    GuiWinSetFb(gFbW, gFbH);
    GuiDesktopSetFb(gFbW, gFbH);
    GuiStartSetFb(gFbW, gFbH);
    GuiWinPaintDesktop();
    GuiWinLayoutAll();
    GuiWinCompose();
    GuiWinPresentFull();

    gDesktopReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: desktop ok\n");
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: start menu ready\n");

#if defined(__x86_64__) || defined(_M_X64)
    if (HalPs2MouseInit() == 0 && HalPs2MouseReady()) {
        gCurX = (INT32)(gFbW / 2u);
        gCurY = (INT32)(gFbH / 2u);
        gCursorOn = 1;
        gCursorShown = 0;
        CursorShow();
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: mouse ok\n");
    } else {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: mouse skip\n");
    }
#endif
    return 0;
}

void GuiRefreshLabels(void) {
    if (!gDesktopReady || gFbW == 0) {
        return;
    }
    CursorHide();
    GuiWinPaintDesktop();
    GuiWinCompose();
    GuiWinPresentFull();
    CursorShow();
}

int GuiPoll(void) {
    HAL_MOUSE_PACKET Pkt;
    INT32 AccX = 0;
    INT32 AccY = 0;
    UINT8 LastBtn = gPrevButtons;
    int Got = 0;
    int SawPress = 0;
    int SawRelease = 0;
    int WasDragging;
    int Hit;

    if (!gDesktopReady || !gCursorOn) {
        return 0;
    }
    GuiDesktopPollTick();
    HalPs2Poll();
    while (HalPs2MousePoll(&Pkt)) {
        Got = 1;
        AccX += Pkt.Dx;
        AccY += Pkt.Dy;
        if ((Pkt.Buttons & 0x1u) != 0 && (LastBtn & 0x1u) == 0) {
            SawPress = 1;
        }
        if ((Pkt.Buttons & 0x1u) == 0 && (LastBtn & 0x1u) != 0) {
            SawRelease = 1;
        }
        LastBtn = Pkt.Buttons;
    }
    if (!Got) {
        if (gDragging) {
            gDragIdle++;
            if (gDragIdle >= 20000u) {
                gDragging = 0;
                gDragWin = -1;
                gPrevButtons = 0;
                gDragIdle = 0;
                ConsoleRefreshBanner();
            }
        }
        return 0;
    }
    gDragIdle = 0;
    CursorHide();

    if (AccX != 0 || AccY != 0) {
        gCurX += AccX;
        gCurY += AccY;
        CursorClamp();
    }

    if (SawPress) {
        int StartHandled = 0;

        if (GuiStartHitButton(gCurX, gCurY)) {
            (void)GuiStartToggle();
            StartHandled = 1;
        } else if (GuiStartMenuOpen()) {
            int M = GuiStartHitMenu(gCurX, gCurY);
            if (M >= 0) {
                (void)GuiStartActivate(M);
                StartHandled = 1;
            } else if (M == -2) {
                StartHandled = 1; /* 菜单内空白 */
            } else {
                GuiStartCloseMenu();
                StartHandled = 1; /* 先收起；再点一次点下方 */
            }
        }

        if (StartHandled) {
            gDragging = 0;
            gDragWin = -1;
            GuiWinPaintDesktop();
            GuiWinCompose();
            GuiWinPresentFull();
        } else {
            Hit = GuiWinHit(gCurX, gCurY);
            if (Hit >= 0 && GuiWinInClose(Hit, gCurX, gCurY)) {
                gDragging = 0;
                gDragWin = -1;
                GuiWinClose(Hit);
            } else if (Hit >= 0 && GuiWinInTitle(Hit, gCurX, gCurY)) {
                INT32 Wx = 0;
                INT32 Wy = 0;
                GuiWinFocus(Hit);
                (void)GuiWinGetPos(Hit, &Wx, &Wy);
                gDragging = 1;
                gDragWin = Hit;
                gDragOffX = gCurX - Wx;
                gDragOffY = gCurY - Wy;
            } else if (Hit == GUI_WIN_SETTINGS) {
                GuiWinFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
                if (GuiSettingsClick(gCurX, gCurY)) {
                    GuiWinPaintDesktop();
                    GuiWinCompose();
                    GuiWinPresentFull();
                }
            } else if (Hit == GUI_WIN_FILES) {
                GuiWinFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
                (void)GuiFilesClick(gCurX, gCurY);
            } else if (Hit >= 0) {
                GuiWinFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
            } else {
                int Icon = GuiDesktopHitIcon(gCurX, gCurY);
                if (Icon >= 0) {
                    (void)GuiDesktopClickIcon(Icon, gCurX, gCurY);
                    gDragging = 0;
                    gDragWin = -1;
                } else if ((UINT32)gCurY >= GuiLayoutContentTop() &&
                           (UINT32)gCurY < GuiLayoutContentBottom()) {
                    GuiWinUnfocusAll();
                    gDragging = 0;
                    gDragWin = -1;
                }
            }
        }
    }

    WasDragging = gDragging;
    if (gDragging && gDragWin >= 0 && (LastBtn & 0x1u) != 0 &&
        (AccX != 0 || AccY != 0)) {
        GuiWinMoveTo(gDragWin, gCurX - gDragOffX, gCurY - gDragOffY, 0);
    }

    if (SawRelease) {
        if (WasDragging && gDragWin == GUI_WIN_SHELL) {
            ConsoleRefreshBanner();
        }
        gDragging = 0;
        gDragWin = -1;
    }

    CursorShow();
    gPrevButtons = LastBtn;
    return 1;
}
