/*
 * Gui.c — 桌面壳编排：初始化 / 标签刷新
 *
 * 【初学者】
 * - 分层：Core/Gui 主文件；光标 Cursor.c，输入 Pointer.c
 * - 对外：GuiInitialize / GuiRefreshLabels / DesktopReady / GuiShell*
 * - 不做：逐像素光标、点击分发（见 Cursor/Pointer）
 */
#include "Gui.h"
#include "GuiPrivate.h"
#include "Desktop.h"
#include "Layout.h"
#include "Start.h"
#include "Window.h"
#include "BootInfo.h"
#include "HalPs2Mouse.h"
#include "HalUsbHid.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Font.h"
#include "FontTtf.h"
#include "Locale.h"
#include "Theme.h"
#include "SerialConfig.h"

static int gDesktopReady;
static UINT32 gFbW;
static UINT32 gFbH;

/*
 * DesktopReady — 桌面是否已初始化成功
 *
 * 谁调用：GuiPoll；Console/其它模块判断可否画 UI。
 */
int DesktopReady(void) {
    return gDesktopReady;
}

/*
 * GuiShellWindowReady — Shell 窗是否打开
 *
 * 谁调用：Console 判断客户区输出。
 */
int GuiShellWindowReady(void) {
    return WindowIsOn(GUI_WIN_SHELL);
}

/*
 * GuiShellFocused — Shell 窗是否有焦点（才吃键）
 *
 * 谁调用：Console 键入路径。
 */
int GuiShellFocused(void) {
    return WindowFocused(GUI_WIN_SHELL);
}

/*
 * GuiShellClientRect — Shell 客户区矩形
 *
 * 谁调用：Console 文本布局。
 */
int GuiShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    return WindowShellClientRect(X, Y, W, H);
}

/*
 * GuiInitialize — 主题/布局/字体/窗体桌面 + 可选鼠标
 *
 * 做什么：无 FB 则跳过；成功则 DesktopReady=1；X64 上试 HID/PS2 鼠。
 * 谁调用：Modules 表 Gui 项 / Kernel 启动序列。
 * 前后文：前 — Video；后 — GuiPoll 循环；兄弟 — CursorEnableAt / PointerReset。
 * 返回：0（跳过或成功均 0；失败不致命）
 */
int GuiInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gDesktopReady = 0;
    PointerReset();
    gFbW = 0;
    gFbH = 0;
    HalVideoGetSize(&gFbW, &gFbH);
    CursorSetFramebuffer(gFbW, gFbH);
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
    if (HalUsbHidMouseReady() ||
        (HalPs2MouseInitialize() == 0 && HalPs2MouseReady())) {
        CursorEnableAt((INT32)(gFbW / 2u), (INT32)(gFbH / 2u));
        HalSerialWriteChannel(SLOG_GUI,
                              HalUsbHidMouseReady() ? "Gui: mouse ok (hid)\n"
                                                    : "Gui: mouse ok\n");
    } else {
        HalSerialWriteChannel(SLOG_GUI, "Gui: mouse skip\n");
    }
#endif
    return 0;
}

/*
 * GuiRefreshLabels — 语言等变更后重画桌面标签
 *
 * 谁调用：Shell `lang`（LocaleSet 之后）。
 * 前后文：配对 GuiCursorHide/Show，避免残影。
 */
void GuiRefreshLabels(void) {
    if (!gDesktopReady || gFbW == 0) {
        return;
    }
    GuiCursorHide();
    WindowPaintDesktop();
    WindowCompose();
    WindowPresentFull();
    GuiCursorShow();
}
