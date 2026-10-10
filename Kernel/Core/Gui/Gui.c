/*
 * Gui.c — 桌面壳编排：初始化 / 标签刷新；光标见 Cursor，输入见 Pointer
 *
 * 对标现网 Gui 分模块；Blocks 目录即命名空间（夹内不叠 Gui）。
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
        (HalPs2MouseInit() == 0 && HalPs2MouseReady())) {
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
