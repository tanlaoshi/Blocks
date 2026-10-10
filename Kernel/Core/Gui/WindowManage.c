/*
 * WindowManage.c — 窗命中、Z 提升、开/关/拖与焦点
 *
 * 【初学者】
 * - 分层：Core/Gui；状态在 Window.c（WindowPrivate.h）
 * - 对外：WindowHit / WindowOpen / WindowMoveTo 等 Window.h 交互 API
 * - 合成仍调 WindowCompose + WindowPresentFull
 */
#include "Window.h"
#include "WindowPrivate.h"
#include "WindowPaint.h"
#include "Files.h"
#include "Layout.h"
#include "StoreUi.h"
#include "Console.h"
#include "HalPs2.h"
#include "HalSerial.h"
#include "SerialConfig.h"

/*
 * ClampWindowPosition — 拖窗时限制在内容区且不占底栏
 *
 * 谁调用：WindowMoveTo。
 */
static void ClampWindowPosition(int Id, INT32 *X, INT32 *Y) {
    INT32 MinY = (INT32)LayoutContentTop();
    INT32 MaxX = (INT32)gWindowFbW - (INT32)gWindows[Id].W;
    INT32 MaxY = (INT32)LayoutContentBottom() - (INT32)gWindows[Id].H;

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

/*
 * WindowIsOn — 槽是否打开
 *
 * 谁调用：GuiShellWindowReady；Desktop；StartActivate；Pointer StoreJob 刷新。
 */
int WindowIsOn(int Id) {
    return (Id >= 0 && Id < GUI_WIN_COUNT) ? gWindows[Id].On : 0;
}

/*
 * WindowFocused — 槽打开且持有焦点
 *
 * 谁调用：GuiShellFocused。
 */
int WindowFocused(int Id) {
    return (Id >= 0 && Id < GUI_WIN_COUNT) ? (gWindows[Id].On && gWindows[Id].Focus) : 0;
}

/*
 * WindowHit — 自顶向下命中第一个可见窗
 *
 * 谁调用：Pointer 左键分发。
 * 返回：GUI_WIN_* 或 -1
 */
int WindowHit(INT32 X, INT32 Y) {
    int i;
    for (i = GUI_WIN_COUNT - 1; i >= 0; i--) {
        int Id = (int)gWindowZOrder[i];
        if (!gWindows[Id].On) {
            continue;
        }
        if (X < (INT32)gWindows[Id].X || Y < (INT32)gWindows[Id].Y) {
            continue;
        }
        if (X >= (INT32)(gWindows[Id].X + gWindows[Id].W) ||
            Y >= (INT32)(gWindows[Id].Y + gWindows[Id].H)) {
            continue;
        }
        return Id;
    }
    return -1;
}

/*
 * WindowInClose — 点是否在关闭钮内
 *
 * 谁调用：WindowInTitle；Pointer 关窗。
 */
int WindowInClose(int Id, INT32 X, INT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gWindows[Id].On) {
        return 0;
    }
    WindowPaintCloseButton(gWindows[Id].X, gWindows[Id].Y, gWindows[Id].W, &Bx, &By, &Bw, &Bh);
    if (X < (INT32)Bx || Y < (INT32)By) {
        return 0;
    }
    if (X >= (INT32)(Bx + Bw) || Y >= (INT32)(By + Bh)) {
        return 0;
    }
    return 1;
}

/*
 * WindowInTitle — 点是否在可拖标题栏（不含关闭钮）
 *
 * 谁调用：Pointer 开始拖窗。
 */
int WindowInTitle(int Id, INT32 X, INT32 Y) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gWindows[Id].On) {
        return 0;
    }
    if (X < (INT32)gWindows[Id].X || Y < (INT32)gWindows[Id].Y) {
        return 0;
    }
    if (X >= (INT32)(gWindows[Id].X + gWindows[Id].W) ||
        Y >= (INT32)(gWindows[Id].Y + 1u + LayoutTitleH())) {
        return 0;
    }
    return !WindowInClose(Id, X, Y);
}

/*
 * WindowRaise — 把 Id 移到 Z 表顶槽（不画屏）
 *
 * 谁调用：WindowFocus。
 */
void WindowRaise(int Id) {
    int i;
    int j;
    int Found = -1;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gWindows[Id].On) {
        return;
    }
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if ((int)gWindowZOrder[i] == Id) {
            Found = i;
            break;
        }
    }
    if (Found < 0) {
        gWindowZOrder[GUI_WIN_COUNT - 1] = (UINT8)Id;
        return;
    }
    for (j = Found; j < GUI_WIN_COUNT - 1; j++) {
        gWindowZOrder[j] = gWindowZOrder[j + 1];
    }
    gWindowZOrder[GUI_WIN_COUNT - 1] = (UINT8)Id;
}

/*
 * WindowFocus — 单窗焦点 + 置顶 + 全帧重画
 *
 * 谁调用：Pointer；Desktop 双击；StartActivate；WindowOpen。
 */
void WindowFocus(int Id) {
    int i;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gWindows[Id].On) {
        return;
    }
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        gWindows[i].Focus = (i == Id) ? 1 : 0;
    }
    WindowRaise(Id);
    WindowCompose();
    WindowPresentFull();
    HalPs2Poll();
}

/*
 * WindowUnfocusAll — 全部去焦点并重画（标题栏变暗）
 *
 * 谁调用：Pointer 点桌面空白。
 */
void WindowUnfocusAll(void) {
    int i;
    int Changed = 0;

    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if (gWindows[i].Focus) {
            gWindows[i].Focus = 0;
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

/*
 * WindowClose — 关窗：擦旧矩形、合成、Present
 *
 * 谁调用：Pointer 点关闭钮。
 */
void WindowClose(int Id) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gWindows[Id].On) {
        return;
    }
    WindowPaintErase(gWindowFbW, gWindowFbH, gWindows[Id].X, gWindows[Id].Y, gWindows[Id].W, gWindows[Id].H);
    gWindows[Id].On = 0;
    gWindows[Id].Focus = 0;
    WindowCompose();
    WindowPresentFull();
    HalPs2Poll();
    HalSerialWriteChannel(SLOG_GUI, (Id == GUI_WIN_SHELL)
                                              ? "Gui: shell closed\n"
                                              : "Gui: about closed\n");
}

/*
 * WindowOpen — 开槽并 WindowFocus；Files/Store 先刷新数据
 *
 * 谁调用：Desktop 图标；StartActivate；WindowOpenMissing。
 */
void WindowOpen(int Id) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || gWindows[Id].On || gWindowFbW == 0) {
        return;
    }
    if (Id == GUI_WIN_FILES) {
        FilesRefresh();
    }
    if (Id == GUI_WIN_STORE) {
        StoreUiRefresh();
    }
    gWindows[Id].On = 1;
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

/*
 * WindowOpenMissing — 依次打开所有当前关着的窗
 *
 * 谁调用：暂无（Window.h 预留）；将来顶栏菜单或调试命令。
 */
void WindowOpenMissing(void) {
    int i;
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if (!gWindows[i].On) {
            WindowOpen(i);
        }
    }
}

/*
 * WindowMoveTo — 拖标题栏移动；可选刷新 Shell 横幅
 *
 * 做什么：先擦旧区、改坐标、合成、脏区 PresentMove。
 * 谁调用：Pointer 拖；RefreshShellText=0（释放时再 ConsoleRefreshBanner）。
 */
void WindowMoveTo(int Id, INT32 X, INT32 Y, int RefreshShellText) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gWindows[Id].On) {
        return;
    }
    ClampWindowPosition(Id, &X, &Y);
    if ((UINT32)X == gWindows[Id].X && (UINT32)Y == gWindows[Id].Y) {
        return;
    }
    Ox = gWindows[Id].X;
    Oy = gWindows[Id].Y;
    Ow = gWindows[Id].W;
    Oh = gWindows[Id].H;
    WindowPaintErase(gWindowFbW, gWindowFbH, Ox, Oy, Ow, Oh);
    gWindows[Id].X = (UINT32)X;
    gWindows[Id].Y = (UINT32)Y;
    WindowCompose();
    WindowPaintPresentMove(gWindowFbW, gWindowFbH, Ox, Oy, Ow, Oh, gWindows[Id].X, gWindows[Id].Y,
                            gWindows[Id].W, gWindows[Id].H);
    HalPs2Poll();
    if (RefreshShellText && Id == GUI_WIN_SHELL) {
        ConsoleRefreshBanner();
    }
}

/*
 * WindowGetPos — 读窗左上角（开且合法 Id）
 *
 * 谁调用：Pointer 计算拖偏移。
 * 返回：0 成功；-1 无效
 */
int WindowGetPos(int Id, INT32 *X, INT32 *Y) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gWindows[Id].On) {
        return -1;
    }
    if (X) {
        *X = (INT32)gWindows[Id].X;
    }
    if (Y) {
        *Y = (INT32)gWindows[Id].Y;
    }
    return 0;
}
