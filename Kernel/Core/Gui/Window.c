/*
 * Window.c — K32：Shell + About 双窗，Z 序/命中/开关/拖（对标现网 Raise 薄）
 *
 * 【初学者】gZ[0]=底、末槽=顶；画底→顶，点顶→底。重叠无 alpha。
 */
#include "Window.h"
#include "WindowPaint.h"
#include "Desktop.h"
#include "Files.h"
#include "Layout.h"
#include "Start.h"
#include "StoreUi.h"
#include "Console.h"
#include "HalPs2.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Locale.h"
#include "SerialConfig.h"

typedef struct {
    int On;
    int Focus;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    LOC_MSG Title;
} GUI_WIN;

static GUI_WIN gW[GUI_WIN_COUNT];
static UINT8 gZ[GUI_WIN_COUNT];
static UINT32 gFbW;
static UINT32 gFbH;

static void PaintOne(int Id) {
    if (!gW[Id].On) {
        return;
    }
    WindowPaint_Frame(gW[Id].X, gW[Id].Y, gW[Id].W, gW[Id].H, gW[Id].Focus,
                      gW[Id].Title, Id);
}

static void ClampPos(int Id, INT32 *X, INT32 *Y) {
    INT32 MinY = (INT32)LayoutContentTop();
    INT32 MaxX = (INT32)gFbW - (INT32)gW[Id].W;
    /* 底栏不可被窗盖住拖入 */
    INT32 MaxY = (INT32)LayoutContentBottom() - (INT32)gW[Id].H;

    if (*X < 0) {
        *X = 0;
    }
    if (*Y < MinY) {
        *Y = MinY;
    }
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

void WindowSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
    LayoutSetFb(W, H);
}

void WindowPaintDesktop(void) {
    WindowPaint_Desktop(gFbW, gFbH);
}

void WindowCompose(void) {
    int i;
    /* 先图标后窗；底栏与菜单盖在最上 */
    DesktopPaintIcons();
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        PaintOne((int)gZ[i]);
    }
    StartPaintBar();
    StartPaintMenu();
}

void WindowPresentFull(void) {
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }
}

void WindowLayoutAll(void) {
    int i;
    static const LOC_MSG Titles[GUI_WIN_COUNT] = {
        MSG_WIN_SHELL, MSG_WIN_ABOUT, MSG_WIN_SETTINGS, MSG_WIN_FILES,
        MSG_WIN_STORE
    };

    for (i = 0; i < GUI_WIN_COUNT; i++) {
        const GUI_WIN_LAYOUT *D = LayoutWindowDesc(i);
        LayoutResolveWindow(i, &gW[i].X, &gW[i].Y, &gW[i].W, &gW[i].H);
        gW[i].Title = Titles[i];
        gW[i].On = (D != 0 && D->OpenByDefault) ? 1 : 0;
        gW[i].Focus = (i == GUI_WIN_SHELL) ? 1 : 0;
    }
    /* 底→顶；须含全部 GUI_WIN_COUNT（漏槽则该窗 Open 了也不画） */
    gZ[0] = (UINT8)GUI_WIN_ABOUT;
    gZ[1] = (UINT8)GUI_WIN_SETTINGS;
    gZ[2] = (UINT8)GUI_WIN_FILES;
    gZ[3] = (UINT8)GUI_WIN_STORE;
    gZ[4] = (UINT8)GUI_WIN_SHELL;
}

int WindowIsOn(int Id) {
    return (Id >= 0 && Id < GUI_WIN_COUNT) ? gW[Id].On : 0;
}

int WindowFocused(int Id) {
    return (Id >= 0 && Id < GUI_WIN_COUNT) ? (gW[Id].On && gW[Id].Focus) : 0;
}

int WindowHit(INT32 X, INT32 Y) {
    int i;
    for (i = GUI_WIN_COUNT - 1; i >= 0; i--) {
        int Id = (int)gZ[i];
        if (!gW[Id].On) {
            continue;
        }
        if (X < (INT32)gW[Id].X || Y < (INT32)gW[Id].Y) {
            continue;
        }
        if (X >= (INT32)(gW[Id].X + gW[Id].W) ||
            Y >= (INT32)(gW[Id].Y + gW[Id].H)) {
            continue;
        }
        return Id;
    }
    return -1;
}

int WindowInClose(int Id, INT32 X, INT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return 0;
    }
    WindowPaint_CloseBtn(gW[Id].X, gW[Id].Y, gW[Id].W, &Bx, &By, &Bw, &Bh);
    if (X < (INT32)Bx || Y < (INT32)By) {
        return 0;
    }
    if (X >= (INT32)(Bx + Bw) || Y >= (INT32)(By + Bh)) {
        return 0;
    }
    return 1;
}

int WindowInTitle(int Id, INT32 X, INT32 Y) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return 0;
    }
    if (X < (INT32)gW[Id].X || Y < (INT32)gW[Id].Y) {
        return 0;
    }
    if (X >= (INT32)(gW[Id].X + gW[Id].W) ||
        Y >= (INT32)(gW[Id].Y + 1u + LayoutTitleH())) {
        return 0;
    }
    return !WindowInClose(Id, X, Y);
}

void WindowRaise(int Id) {
    int i;
    int j;
    int Found = -1;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if ((int)gZ[i] == Id) {
            Found = i;
            break;
        }
    }
    if (Found < 0) {
        /* 未入 Z 表（扩窗漏初始化时）：挤掉重复后放到顶 */
        gZ[GUI_WIN_COUNT - 1] = (UINT8)Id;
        return;
    }
    for (j = Found; j < GUI_WIN_COUNT - 1; j++) {
        gZ[j] = gZ[j + 1];
    }
    gZ[GUI_WIN_COUNT - 1] = (UINT8)Id;
}

void WindowFocus(int Id) {
    int i;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        gW[i].Focus = (i == Id) ? 1 : 0;
    }
    WindowRaise(Id);
    WindowCompose();
    WindowPresentFull();
    HalPs2Poll();
}

void WindowUnfocusAll(void) {
    int i;
    int Changed = 0;

    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if (gW[i].Focus) {
            gW[i].Focus = 0;
            Changed = 1;
        }
    }
    if (!Changed) {
        return;
    }
    WindowCompose();
    WindowPresentFull();
    HalPs2Poll();
}

void WindowClose(int Id) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    WindowPaint_Erase(gFbW, gFbH, gW[Id].X, gW[Id].Y, gW[Id].W, gW[Id].H);
    gW[Id].On = 0;
    gW[Id].Focus = 0;
    WindowCompose();
    WindowPresentFull();
    HalPs2Poll();
    HalSerialWriteChannel(SLOG_GUI, (Id == GUI_WIN_SHELL)
                                              ? "Gui: shell closed\n"
                                              : "Gui: about closed\n");
}

void WindowOpen(int Id) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || gW[Id].On || gFbW == 0) {
        return;
    }
    if (Id == GUI_WIN_FILES) {
        FilesRefresh();
    }
    if (Id == GUI_WIN_STORE) {
        StoreUiRefresh();
    }
    gW[Id].On = 1;
    WindowFocus(Id);
    if (Id == GUI_WIN_SHELL) {
        HalSerialWriteChannel(SLOG_GUI, "Gui: shell opened\n");
    } else if (Id == GUI_WIN_FILES) {
        HalSerialWriteChannel(SLOG_GUI, "Gui: files opened\n");
    } else if (Id == GUI_WIN_SETTINGS) {
        HalSerialWriteChannel(SLOG_GUI, "Gui: settings opened\n");
    } else if (Id == GUI_WIN_STORE) {
        HalSerialWriteChannel(SLOG_GUI, "Gui: store opened\n");
    } else {
        HalSerialWriteChannel(SLOG_GUI, "Gui: about opened\n");
    }
}

void WindowOpenMissing(void) {
    int i;
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if (!gW[i].On) {
            WindowOpen(i);
        }
    }
}

void WindowMoveTo(int Id, INT32 X, INT32 Y, int RefreshShellText) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    ClampPos(Id, &X, &Y);
    if ((UINT32)X == gW[Id].X && (UINT32)Y == gW[Id].Y) {
        return;
    }
    Ox = gW[Id].X;
    Oy = gW[Id].Y;
    Ow = gW[Id].W;
    Oh = gW[Id].H;
    WindowPaint_Erase(gFbW, gFbH, Ox, Oy, Ow, Oh);
    gW[Id].X = (UINT32)X;
    gW[Id].Y = (UINT32)Y;
    WindowCompose();
    WindowPaint_PresentMove(gFbW, gFbH, Ox, Oy, Ow, Oh, gW[Id].X, gW[Id].Y,
                            gW[Id].W, gW[Id].H);
    HalPs2Poll();
    if (RefreshShellText && Id == GUI_WIN_SHELL) {
        ConsoleRefreshBanner();
    }
}

int WindowGetPos(int Id, INT32 *X, INT32 *Y) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return -1;
    }
    if (X) {
        *X = (INT32)gW[Id].X;
    }
    if (Y) {
        *Y = (INT32)gW[Id].Y;
    }
    return 0;
}

int WindowShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 TitleH = LayoutTitleH();
    if (!gW[GUI_WIN_SHELL].On) {
        return -1;
    }
    if (X) {
        *X = gW[GUI_WIN_SHELL].X + 1u;
    }
    if (Y) {
        *Y = gW[GUI_WIN_SHELL].Y + 1u + TitleH;
    }
    if (W) {
        *W = gW[GUI_WIN_SHELL].W - 2u;
    }
    if (H) {
        *H = (gW[GUI_WIN_SHELL].H > TitleH + 2u)
                 ? (gW[GUI_WIN_SHELL].H - 2u - TitleH)
                 : 0;
    }
    return 0;
}
