/*
 * Window.c — 窗状态、Z 序表、合成与 Present
 *
 * 【初学者】
 * - 分层：Core/Gui；像素在 WindowPaint.c；点击/拖在 WindowManage.c
 * - 对外：WindowSetFb / WindowCompose / WindowLayoutAll / WindowShellClientRect
 * - gWindowZOrder[0]=底、末槽=顶；画底→顶
 */
#include "Window.h"
#include "WindowPrivate.h"
#include "WindowPaint.h"
#include "Desktop.h"
#include "Layout.h"
#include "Start.h"
#include "HalVideo.h"
#include "Locale.h"

GUI_WIN gWindows[GUI_WIN_COUNT];
UINT8 gWindowZOrder[GUI_WIN_COUNT];
UINT32 gWindowFbW;
UINT32 gWindowFbH;

/*
 * PaintOne — 单槽窗框+客户区（开则画）
 *
 * 谁调用：WindowCompose。
 */
static void PaintOne(int Id) {
    if (!gWindows[Id].On) {
        return;
    }
    WindowPaintFrame(gWindows[Id].X, gWindows[Id].Y, gWindows[Id].W, gWindows[Id].H, gWindows[Id].Focus,
                      gWindows[Id].Title, Id);
}

/*
 * WindowSetFb — 记录 FB 并同步 Layout 缩放基
 *
 * 谁调用：GuiInitialize。
 */
void WindowSetFb(UINT32 W, UINT32 H) {
    gWindowFbW = W;
    gWindowFbH = H;
    LayoutSetFb(W, H);
}

/*
 * WindowPaintDesktop — 清屏+顶栏（不含窗）
 *
 * 谁调用：GuiInitialize / GuiRefreshLabels；Pointer 重绘路径。
 */
void WindowPaintDesktop(void) {
    WindowPaintDesktopFramebuffer(gWindowFbW, gWindowFbH);
}

/*
 * WindowCompose — 图标→窗（Z 序）→底栏与开始菜单
 *
 * 谁调用：GuiInitialize；WindowManage 焦点/关窗/拖后；Pointer 全帧重画。
 */
void WindowCompose(void) {
    int i;
    DesktopPaintIcons();
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        PaintOne((int)gWindowZOrder[i]);
    }
    StartPaintBar();
    StartPaintMenu();
}

/*
 * WindowPresentFull — 双缓冲时整屏 flip
 *
 * 谁调用：与 WindowCompose 成对（Gui / WindowManage / Pointer）。
 */
void WindowPresentFull(void) {
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }
}

/*
 * WindowLayoutAll — 从 Layout 表初始化各槽矩形、默认开/关、Z 序
 *
 * 做什么：OpenByDefault 决定 On；Shell 默认有焦点。
 * 谁调用：GuiInitialize（WindowPaintDesktop 之后）。
 * 前后文：前 — LayoutLoadConfiguration；后 — WindowCompose。
 */
void WindowLayoutAll(void) {
    int i;
    static const LOC_MSG Titles[GUI_WIN_COUNT] = {
        MSG_WIN_SHELL, MSG_WIN_ABOUT, MSG_WIN_SETTINGS, MSG_WIN_FILES,
        MSG_WIN_STORE
    };

    for (i = 0; i < GUI_WIN_COUNT; i++) {
        const GUI_WIN_LAYOUT *D = LayoutWindowDescription(i);
        LayoutResolveWindow(i, &gWindows[i].X, &gWindows[i].Y, &gWindows[i].W, &gWindows[i].H);
        gWindows[i].Title = Titles[i];
        gWindows[i].On = (D != 0 && D->OpenByDefault) ? 1 : 0;
        gWindows[i].Focus = (i == GUI_WIN_SHELL) ? 1 : 0;
    }
    gWindowZOrder[0] = (UINT8)GUI_WIN_ABOUT;
    gWindowZOrder[1] = (UINT8)GUI_WIN_SETTINGS;
    gWindowZOrder[2] = (UINT8)GUI_WIN_FILES;
    gWindowZOrder[3] = (UINT8)GUI_WIN_STORE;
    gWindowZOrder[4] = (UINT8)GUI_WIN_SHELL;
}

/*
 * WindowShellClientRect — Shell 窗客户区（Console 文本区）
 *
 * 做什么：Shell 未开则失败；含标题栏扣除与 1px 边框。
 * 谁调用：GuiShellClientRect → Console 布局。
 * 返回：0 成功；-1 Shell 关
 */
int WindowShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 TitleH = LayoutTitleH();
    if (!gWindows[GUI_WIN_SHELL].On) {
        return -1;
    }
    if (X) {
        *X = gWindows[GUI_WIN_SHELL].X + 1u;
    }
    if (Y) {
        *Y = gWindows[GUI_WIN_SHELL].Y + 1u + TitleH;
    }
    if (W) {
        *W = gWindows[GUI_WIN_SHELL].W - 2u;
    }
    if (H) {
        *H = (gWindows[GUI_WIN_SHELL].H > TitleH + 2u)
                 ? (gWindows[GUI_WIN_SHELL].H - 2u - TitleH)
                 : 0;
    }
    return 0;
}
