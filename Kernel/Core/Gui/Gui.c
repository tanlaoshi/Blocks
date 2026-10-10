/*
 * Gui.c — 桌面壳：光标 + 鼠轮询；窗体见 Window（K32 双窗 Z 序）
 *
 * 【初学者】
 * 点窗抬升；点 × 关；点顶栏重开已关窗；Shell 焦点才吃键（Console）。
 */
#include "Gui.h"
#include "Desktop.h"
#include "Files.h"
#include "Layout.h"
#include "Settings.h"
#include "Start.h"
#include "Window.h"
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
#include "SerialConfig.h"

#define GUI_CUR_COLOR   0x00FFFFFFu
#define GUI_CUR_OUTLINE 0x00000000u
#define GUI_CUR_W       11u
#define GUI_CUR_H       16u
/* 描边外扩 1px；热点仍在箭头尖 = (gCurX,gCurY) */
#define GUI_CUR_PAD     1
#define GUI_CUR_BOX_W   (GUI_CUR_W + 2u * (UINT32)GUI_CUR_PAD)
#define GUI_CUR_BOX_H   (GUI_CUR_H + 2u * (UINT32)GUI_CUR_PAD)

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
static UINT32 gUnder[GUI_CUR_BOX_H][GUI_CUR_BOX_W];
static INT32 gSaveX;
static INT32 gSaveY;
static UINT32 gSaveW;
static UINT32 gSaveH;

/* MSB=左；尖在 (0,0)，与点击坐标一致 */
static const UINT8 gArrow[GUI_CUR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xF8, 0xDC, 0x8E, 0x07, 0x03, 0x01, 0x00, 0x00
};

static int ArrowSolid(UINT32 Col, UINT32 Row) {
    if (Row >= GUI_CUR_H || Col >= GUI_CUR_W) {
        return 0;
    }
    return (gArrow[Row] & (UINT8)(0x80u >> Col)) != 0;
}

/*
 * 光标只叠前缓冲（LFB）。写背缓冲会在 Present 后把尖端「烤进」画面，
 * Hide 再拿旧 under 盖回去 → 看起来尖偏了、点不中。
 */
void GuiCursorHide(void) {
    UINT32 Row;
    UINT32 Col;

    if (!gCursorShown) {
        return;
    }
    for (Row = 0; Row < gSaveH && Row < GUI_CUR_BOX_H; Row++) {
        for (Col = 0; Col < gSaveW && Col < GUI_CUR_BOX_W; Col++) {
            INT32 Px = gSaveX + (INT32)Col;
            INT32 Py = gSaveY + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py, gUnder[Row][Col]);
            }
        }
    }
    gCursorShown = 0;
}

void GuiCursorShow(void) {
    UINT32 Row;
    UINT32 Col;
    INT32 Ox;
    INT32 Oy;

    if (!gCursorOn || gCursorShown || gFbW == 0) {
        return;
    }
    if (!HalVideoBackbufferEnabled()) {
        return;
    }
    Ox = gCurX - GUI_CUR_PAD;
    Oy = gCurY - GUI_CUR_PAD;
    gSaveX = Ox;
    gSaveY = Oy;
    gSaveW = GUI_CUR_BOX_W;
    gSaveH = GUI_CUR_BOX_H;

    for (Row = 0; Row < GUI_CUR_BOX_H; Row++) {
        for (Col = 0; Col < GUI_CUR_BOX_W; Col++) {
            INT32 Px = Ox + (INT32)Col;
            INT32 Py = Oy + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                gUnder[Row][Col] = HalVideoFrontReadPixel((UINT32)Px, (UINT32)Py);
            } else {
                gUnder[Row][Col] = 0;
            }
        }
    }

    /* 黑描边：实心邻域，尖在亮底也看得见 */
    for (Row = 0; Row < GUI_CUR_H; Row++) {
        for (Col = 0; Col < GUI_CUR_W; Col++) {
            INT32 dRow;
            INT32 dCol;
            if (!ArrowSolid(Col, Row)) {
                continue;
            }
            for (dRow = -1; dRow <= 1; dRow++) {
                for (dCol = -1; dCol <= 1; dCol++) {
                    INT32 Ac = (INT32)Col + dCol;
                    INT32 Ar = (INT32)Row + dRow;
                    INT32 Px;
                    INT32 Py;
                    if (Ac >= 0 && Ar >= 0 && ArrowSolid((UINT32)Ac, (UINT32)Ar)) {
                        continue;
                    }
                    Px = gCurX + Ac;
                    Py = gCurY + Ar;
                    if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW &&
                        (UINT32)Py < gFbH) {
                        HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py,
                                              GUI_CUR_OUTLINE);
                    }
                }
            }
        }
    }
    for (Row = 0; Row < GUI_CUR_H; Row++) {
        for (Col = 0; Col < GUI_CUR_W; Col++) {
            INT32 Px;
            INT32 Py;
            if (!ArrowSolid(Col, Row)) {
                continue;
            }
            Px = gCurX + (INT32)Col;
            Py = gCurY + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py, GUI_CUR_COLOR);
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

int DesktopReady(void) {
    return gDesktopReady;
}

int GuiShellWindowReady(void) {
    return WindowIsOn(GUI_WIN_SHELL);
}

int GuiShellFocused(void) {
    return WindowFocused(GUI_WIN_SHELL);
}

int GuiShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    return WindowShellClientRect(X, Y, W, H);
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
        HalSerialWriteChannel(SLOG_GUI, "Gui: skip (no FB)\n");
        return 0;
    }

    ThemeInitialize();
    (void)ThemeLoadConfiguration(); /* K37：色/mode；无文件则出厂色 */
    LayoutSetFb(gFbW, gFbH);
    (void)LayoutLoadConfiguration(); /* 窗几何描述；无则内建表 */
    FontInitialize();
    LocaleInitialize();
    FontTtfPreheatUtf8("积木系统已就绪命令窗说明设置点色块改主题文件运行开始");
    WindowSetFb(gFbW, gFbH);
    DesktopSetFb(gFbW, gFbH);
    StartSetFb(gFbW, gFbH);
    WindowPaintDesktop();
    WindowLayoutAll();
    WindowCompose();
    WindowPresentFull();

    gDesktopReady = 1;
    HalSerialWriteChannel(SLOG_GUI, "Gui: desktop ok\n");
    HalSerialWriteChannel(SLOG_GUI, "Gui: start menu ready\n");

#if defined(__x86_64__) || defined(_M_X64)
    if (HalPs2MouseInit() == 0 && HalPs2MouseReady()) {
        gCurX = (INT32)(gFbW / 2u);
        gCurY = (INT32)(gFbH / 2u);
        gCursorOn = 1;
        gCursorShown = 0;
        CursorShow();
        HalSerialWriteChannel(SLOG_GUI, "Gui: mouse ok\n");
    } else {
        HalSerialWriteChannel(SLOG_GUI, "Gui: mouse skip\n");
    }
#endif
    return 0;
}

void GuiRefreshLabels(void) {
    if (!gDesktopReady || gFbW == 0) {
        return;
    }
    CursorHide();
    WindowPaintDesktop();
    WindowCompose();
    WindowPresentFull();
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
    DesktopPollTick();
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

        if (StartHitButton(gCurX, gCurY)) {
            (void)StartToggle();
            StartHandled = 1;
        } else if (StartMenuOpen()) {
            int M = StartHitMenu(gCurX, gCurY);
            if (M >= 0) {
                (void)StartActivate(M);
                StartHandled = 1;
            } else if (M == -2) {
                StartHandled = 1; /* 菜单内空白 */
            } else {
                StartCloseMenu();
                StartHandled = 1; /* 先收起；再点一次点下方 */
            }
        }

        if (StartHandled) {
            gDragging = 0;
            gDragWin = -1;
            WindowPaintDesktop();
            WindowCompose();
            WindowPresentFull();
        } else {
            Hit = WindowHit(gCurX, gCurY);
            if (Hit >= 0 && WindowInClose(Hit, gCurX, gCurY)) {
                gDragging = 0;
                gDragWin = -1;
                WindowClose(Hit);
            } else if (Hit >= 0 && WindowInTitle(Hit, gCurX, gCurY)) {
                INT32 Wx = 0;
                INT32 Wy = 0;
                WindowFocus(Hit);
                (void)WindowGetPos(Hit, &Wx, &Wy);
                gDragging = 1;
                gDragWin = Hit;
                gDragOffX = gCurX - Wx;
                gDragOffY = gCurY - Wy;
            } else if (Hit == GUI_WIN_SETTINGS) {
                WindowFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
                if (SettingsClick(gCurX, gCurY)) {
                    WindowPaintDesktop();
                    WindowCompose();
                    WindowPresentFull();
                }
            } else if (Hit == GUI_WIN_FILES) {
                WindowFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
                /* 与 Settings 相同：点选/New/Del 改状态后必须重画，否则像「没反应」 */
                if (FilesClick(gCurX, gCurY)) {
                    WindowPaintDesktop();
                    WindowCompose();
                    WindowPresentFull();
                }
            } else if (Hit >= 0) {
                WindowFocus(Hit);
                gDragging = 0;
                gDragWin = -1;
            } else {
                int Icon = DesktopHitIcon(gCurX, gCurY);
                if (Icon >= 0) {
                    (void)DesktopClickIcon(Icon, gCurX, gCurY);
                    gDragging = 0;
                    gDragWin = -1;
                } else if ((UINT32)gCurY >= LayoutContentTop() &&
                           (UINT32)gCurY < LayoutContentBottom()) {
                    WindowUnfocusAll();
                    gDragging = 0;
                    gDragWin = -1;
                }
            }
        }
    }

    WasDragging = gDragging;
    if (gDragging && gDragWin >= 0 && (LastBtn & 0x1u) != 0 &&
        (AccX != 0 || AccY != 0)) {
        WindowMoveTo(gDragWin, gCurX - gDragOffX, gCurY - gDragOffY, 0);
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
