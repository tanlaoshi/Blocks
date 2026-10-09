/*
 * Gui.c — 桌面壳 + Shell 单窗 + K30 拖/焦点 + K31 关窗
 *
 * 【初学者】
 * 点标题栏按住拖；点 × 关窗并重画桌面；点顶栏再开窗。
 * 点窗内聚焦；点桌面失焦。
 */
#include "Gui.h"
#include "BootInfo.h"
#include "Console.h"
#include "HalPs2.h"
#include "HalPs2Kbd.h"
#include "HalPs2Mouse.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Font.h"
#include "FontTtf.h"
#include "Locale.h"
#include "Theme.h"
#include "ToySerialConfig.h"

#define GUI_BAR_H     28u
#define GUI_TITLE_H   24u
#define GUI_CLOSE_W   20u
#define GUI_CLOSE_BG  0x00B33A3Au
#define GUI_CUR_COLOR 0x00FFFFFFu
#define GUI_CUR_W     8u
#define GUI_CUR_H     14u

static int gDesktopReady;
static int gWinOn;
static int gWinFocus;
static int gDragging;
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
static UINT32 gWinX;
static UINT32 gWinY;
static UINT32 gWinW;
static UINT32 gWinH;
/* 底图采自背缓冲（RAM）；禁止读 GOP 前缓冲（QEMU 读屏极慢） */
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
 * 光标：底图采自背缓冲（快），像素双写背+前缓冲，不 PresentRect。
 * 每拍 Present 会拖死 Poll → 停鼠半包失步「一停就卡」。
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

static void CloseBtnGeom(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (W) {
        *W = GUI_CLOSE_W;
    }
    if (H) {
        *H = GUI_TITLE_H;
    }
    if (X) {
        *X = gWinX + gWinW - 1u - GUI_CLOSE_W;
    }
    if (Y) {
        *Y = gWinY + 1u;
    }
}

static int InCloseBtn(INT32 X, INT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (!gWinOn) {
        return 0;
    }
    CloseBtnGeom(&Bx, &By, &Bw, &Bh);
    if (X < (INT32)Bx || Y < (INT32)By) {
        return 0;
    }
    if (X >= (INT32)(Bx + Bw) || Y >= (INT32)(By + Bh)) {
        return 0;
    }
    return 1;
}

static int InTitleBar(INT32 X, INT32 Y) {
    if (!gWinOn) {
        return 0;
    }
    if (X < (INT32)gWinX || Y < (INT32)gWinY) {
        return 0;
    }
    if (X >= (INT32)(gWinX + gWinW) || Y >= (INT32)(gWinY + 1u + GUI_TITLE_H)) {
        return 0;
    }
    /* × 区不开始拖 */
    if (InCloseBtn(X, Y)) {
        return 0;
    }
    return 1;
}

static int InTaskbar(INT32 X, INT32 Y) {
    (void)X;
    return Y >= 0 && (UINT32)Y < GUI_BAR_H;
}

static int InWindow(INT32 X, INT32 Y) {
    if (!gWinOn) {
        return 0;
    }
    if (X < (INT32)gWinX || Y < (INT32)gWinY) {
        return 0;
    }
    if (X >= (INT32)(gWinX + gWinW) || Y >= (INT32)(gWinY + gWinH)) {
        return 0;
    }
    return 1;
}

static void PaintShellWindow(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 TitleBg;

    if (!gWinOn) {
        return;
    }
    HalVideoFillRect(gWinX, gWinY, gWinW, gWinH, ThemeWindowBorder());
    TitleBg = gWinFocus ? ThemeWindowTitleBar() : ThemeWindowTitleBarDim();
    HalVideoFillRect(gWinX + 1u, gWinY + 1u, gWinW - 2u, GUI_TITLE_H, TitleBg);
    FontDrawStringAt(gWinX + 10u, gWinY + 5u, LocStr(MSG_WIN_SHELL),
                     ThemeWindowTitleText());
    {
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;
        CloseBtnGeom(&Bx, &By, &Bw, &Bh);
        HalVideoFillRect(Bx, By, Bw, Bh, GUI_CLOSE_BG);
        FontDrawStringAt(Bx + 6u, By + 4u, "x", ThemeWindowTitleText());
    }
    Cx = gWinX + 1u;
    Cy = gWinY + 1u + GUI_TITLE_H;
    Cw = gWinW - 2u;
    Ch = gWinH - 2u - GUI_TITLE_H;
    if (Ch > 0) {
        HalVideoFillRect(Cx, Cy, Cw, Ch, ThemeWindowClient());
    }
}

#define WIN_MOVE_PAD 2u

static void EraseShellWindow(void) {
    UINT32 L;
    UINT32 T;
    UINT32 R;
    UINT32 B;

    if (!gWinOn || gFbW == 0) {
        return;
    }
    /* 多擦一圈：上下 1px 边框拖动时易在前缓冲留横线 */
    L = (gWinX > WIN_MOVE_PAD) ? (gWinX - WIN_MOVE_PAD) : 0;
    T = (gWinY > WIN_MOVE_PAD) ? (gWinY - WIN_MOVE_PAD) : 0;
    R = gWinX + gWinW + WIN_MOVE_PAD;
    B = gWinY + gWinH + WIN_MOVE_PAD;
    if (R > gFbW) {
        R = gFbW;
    }
    if (B > gFbH) {
        B = gFbH;
    }
    if (R > L && B > T) {
        HalVideoFillRect(L, T, R - L, B - T, ThemeDesktopBackground());
    }
    if (T < GUI_BAR_H) {
        HalVideoFillRect(0, 0, gFbW, GUI_BAR_H, ThemeTaskbarBackground());
        FontDrawStringAt(12, 8, LocStr(MSG_DESKTOP_TITLE), ThemeWindowTitleText());
    }
}

/* 旧∪新并外扩，一次 Present，避免边框行漏拷 */
static void PresentMoveDirty(UINT32 X0, UINT32 Y0, UINT32 W0, UINT32 H0,
                             UINT32 X1, UINT32 Y1, UINT32 W1, UINT32 H1) {
    UINT32 L;
    UINT32 T;
    UINT32 R;
    UINT32 B;
    UINT32 R0 = X0 + W0;
    UINT32 R1 = X1 + W1;
    UINT32 B0 = Y0 + H0;
    UINT32 B1 = Y1 + H1;

    L = (X0 < X1) ? X0 : X1;
    T = (Y0 < Y1) ? Y0 : Y1;
    R = (R0 > R1) ? R0 : R1;
    B = (B0 > B1) ? B0 : B1;
    if (L > WIN_MOVE_PAD) {
        L -= WIN_MOVE_PAD;
    } else {
        L = 0;
    }
    if (T > WIN_MOVE_PAD) {
        T -= WIN_MOVE_PAD;
    } else {
        T = 0;
    }
    R += WIN_MOVE_PAD;
    B += WIN_MOVE_PAD;
    if (R > gFbW) {
        R = gFbW;
    }
    if (B > gFbH) {
        B = gFbH;
    }
    if (R > L && B > T) {
        HalVideoPresentRect(L, T, R - L, B - T);
    }
}

static void ClampWinPos(INT32 *X, INT32 *Y) {
    INT32 MinY = (INT32)GUI_BAR_H;
    INT32 MaxX;
    INT32 MaxY;

    if (*X < 0) {
        *X = 0;
    }
    if (*Y < MinY) {
        *Y = MinY;
    }
    MaxX = (INT32)gFbW - (INT32)gWinW;
    MaxY = (INT32)gFbH - (INT32)gWinH;
    if (MaxX < 0) {
        MaxX = 0;
    }
    if (MaxY < MinY) {
        MaxY = MinY;
    }
    if (*X > MaxX) {
        *X = MaxX;
    }
    if (*Y > MaxY) {
        *Y = MaxY;
    }
}

static void MoveShellWindowTo(INT32 Nx, INT32 Ny, int RefreshText) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    ClampWinPos(&Nx, &Ny);
    if ((UINT32)Nx == gWinX && (UINT32)Ny == gWinY) {
        return;
    }
    Ox = gWinX;
    Oy = gWinY;
    Ow = gWinW;
    Oh = gWinH;
    CursorHide();
    EraseShellWindow();
    gWinX = (UINT32)Nx;
    gWinY = (UINT32)Ny;
    PaintShellWindow();
    /* 拖动中也画客户区字（旧逻辑 RefreshText=0 会空窗） */
    ConsolePaintBannerBack();
    /* 旧∪新 + pad 一次 Present：专治上下边框横线残影 */
    PresentMoveDirty(Ox, Oy, Ow, Oh, gWinX, gWinY, gWinW, gWinH);
    HalPs2Poll();
    if (RefreshText) {
        ConsoleRefreshBanner();
    }
}

static void LayoutShellWindow(void);

static void SetShellFocus(int On) {
    if (!gWinOn) {
        return;
    }
    On = On ? 1 : 0;
    if (gWinFocus == On) {
        return;
    }
    gWinFocus = On;
    CursorHide();
    PaintShellWindow();
    ConsolePaintBannerBack();
    HalVideoPresentRect(gWinX, gWinY, gWinW, gWinH);
    HalPs2Poll();
}

static void PaintDesktopOnly(void) {
    HalVideoFillRect(0, 0, gFbW, gFbH, ThemeDesktopBackground());
    HalVideoFillRect(0, 0, gFbW, GUI_BAR_H, ThemeTaskbarBackground());
    FontDrawStringAt(12, 8, LocStr(MSG_DESKTOP_TITLE), ThemeWindowTitleText());
}

static void CloseShellWindow(void) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    if (!gWinOn) {
        return;
    }
    Ox = gWinX;
    Oy = gWinY;
    Ow = gWinW;
    Oh = gWinH;
    gDragging = 0;
    EraseShellWindow();
    gWinOn = 0;
    gWinFocus = 0;
    PaintDesktopOnly();
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    } else {
        HalVideoPresentRect(Ox, Oy, Ow, Oh);
    }
    HalPs2Poll();
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: shell closed (click taskbar)\n");
}

static void OpenShellWindow(void) {
    if (gWinOn || gFbW == 0) {
        return;
    }
    LayoutShellWindow();
    PaintShellWindow();
    ConsolePaintBannerBack();
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresentRect(gWinX, gWinY, gWinW, gWinH);
    }
    HalPs2Poll();
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: shell opened\n");
}

static void LayoutShellWindow(void) {
    gWinW = 520u;
    gWinH = 300u;
    if (gWinW + 40u > gFbW) {
        gWinW = (gFbW > 80u) ? (gFbW - 40u) : gFbW;
    }
    if (gWinH + GUI_BAR_H + 40u > gFbH) {
        gWinH = (gFbH > GUI_BAR_H + 40u) ? (gFbH - GUI_BAR_H - 40u) : 120u;
    }
    gWinX = (gFbW > gWinW) ? ((gFbW - gWinW) / 2u) : 0;
    gWinY = GUI_BAR_H + 24u;
    if (gWinY + gWinH > gFbH) {
        gWinY = GUI_BAR_H + 4u;
    }
    gWinOn = 1;
    gWinFocus = 1;
    gDragging = 0;
}

int GuiDesktopReady(void) {
    return gDesktopReady;
}

int GuiShellWindowReady(void) {
    return gWinOn;
}

int GuiShellFocused(void) {
    return gWinOn && gWinFocus;
}

int GuiShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (!gWinOn) {
        return -1;
    }
    if (X) {
        *X = gWinX + 1u;
    }
    if (Y) {
        *Y = gWinY + 1u + GUI_TITLE_H;
    }
    if (W) {
        *W = gWinW - 2u;
    }
    if (H) {
        *H = (gWinH > GUI_TITLE_H + 2u) ? (gWinH - 2u - GUI_TITLE_H) : 0;
    }
    return 0;
}

int GuiInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gDesktopReady = 0;
    gWinOn = 0;
    gWinFocus = 0;
    gDragging = 0;
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
    FontInitialize();
    LocaleInitialize();
    FontTtfPreheatUtf8("积木系统已就绪命令窗");
    HalVideoFillRect(0, 0, gFbW, gFbH, ThemeDesktopBackground());
    HalVideoFillRect(0, 0, gFbW, GUI_BAR_H, ThemeTaskbarBackground());
    FontDrawStringAt(12, 8, LocStr(MSG_DESKTOP_TITLE), ThemeWindowTitleText());
    LayoutShellWindow();
    PaintShellWindow();
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }

    gDesktopReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: desktop ok\n");
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: shell win ok\n");
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: drag/focus ready\n");

#if defined(__x86_64__) || defined(_M_X64)
    if (HalPs2MouseInit() == 0 && HalPs2MouseReady()) {
        gCurX = (INT32)(gFbW / 2u);
        gCurY = (INT32)(gFbH / 2u);
        gCursorOn = 1;
        gCursorShown = 0;
        CursorShow();
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: mouse ok\n");
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: cursor on\n");
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
    HalVideoFillRect(0, 0, gFbW, GUI_BAR_H, ThemeTaskbarBackground());
    FontDrawStringAt(12, 8, LocStr(MSG_DESKTOP_TITLE), ThemeWindowTitleText());
    if (gWinOn) {
        PaintShellWindow();
        ConsolePaintBannerBack();
    }
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }
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

    if (!gDesktopReady || !gCursorOn) {
        return 0;
    }
    /* 对标现网：先 HalInputPoll(demux)，再只从软件队列 Dequeue */
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
        /* 按住不动会 Got=0；仅连续空转很久才当松手包丢失 */
        if (gDragging) {
            gDragIdle++;
            if (gDragIdle >= 20000u) {
                gDragging = 0;
                gPrevButtons = 0;
                gDragIdle = 0;
                ConsoleRefreshBanner();
            }
        }
        return 0;
    }
    gDragIdle = 0;

    /* 整拍只 Hide→改状态→Show 一次，避免 XOR/Present 交错啃边框 */
    CursorHide();

    if (AccX != 0 || AccY != 0) {
        gCurX += AccX;
        gCurY += AccY;
        CursorClamp();
    }

    if (SawPress) {
        if (gWinOn && InCloseBtn(gCurX, gCurY)) {
            CloseShellWindow();
        } else if (!gWinOn && InTaskbar(gCurX, gCurY)) {
            OpenShellWindow();
        } else if (InTitleBar(gCurX, gCurY)) {
            gDragging = 1;
            gDragOffX = gCurX - (INT32)gWinX;
            gDragOffY = gCurY - (INT32)gWinY;
            SetShellFocus(1);
        } else if (InWindow(gCurX, gCurY)) {
            SetShellFocus(1);
        } else if (gWinOn && (UINT32)gCurY >= GUI_BAR_H) {
            SetShellFocus(0);
        }
    }

    WasDragging = gDragging;
    if (gWinOn && gDragging && (LastBtn & 0x1u) != 0 &&
        (AccX != 0 || AccY != 0)) {
        MoveShellWindowTo(gCurX - gDragOffX, gCurY - gDragOffY, 0);
    }

    if (SawRelease) {
        if (WasDragging) {
            ConsoleRefreshBanner();
        }
        gDragging = 0;
    }

    CursorShow();
    gPrevButtons = LastBtn;
    return 1;
}
