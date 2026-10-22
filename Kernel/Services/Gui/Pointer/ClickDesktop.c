/*
 * ClickDesktop.c — 桌面/菜单/托盘 → DESKTOP_ACTION → 开窗（story-s1）
 *
 * GuiClickRunDesktopAction：
 *   SHELL/SETTINGS/FILES/STORE/DEVICES → GuiOpen*（Open/Apps.c 等）
 *   EXEC → ProcessExec + WaitPrompt（与 shell exec 同契约；双 blocks> 坑见下）
 *   SHUTDOWN/REBOOT → HalCpu*
 * 单例（Settings/Store/Edit/Files/Devices）在 Open.c FocusExistingKind；Shell 可多开。
 * 图标双击出 Action：Desktop/Click.c；栏/菜单单击：TaskbarClick.c。
 */
#include "GuiPrivate.h"
#include "Hal.h"
#include "Debug.h"
#include "Desktop.h"
#include "Console.h"
#include "Process.h"

static void GuiClickRunDesktopAction(DESKTOP_ACTION Act, const char *ExecPath) {
    if (Act == DESKTOP_ACTION_SHELL) {
        (void)GuiOpenShell();
    } else if (Act == DESKTOP_ACTION_SETTINGS) {
        (void)GuiOpenSettings();
    } else if (Act == DESKTOP_ACTION_FILES) {
        (void)GuiOpenFiles();
    } else if (Act == DESKTOP_ACTION_STORE) {
        (void)GuiOpenStore();
    } else if (Act == DESKTOP_ACTION_DEVICES) {
        (void)GuiOpenDevices();
    } else if (Act == DESKTOP_ACTION_EXEC) {
        if (ExecPath != NULL && ExecPath[0]) {
            DebugWrite("desktop: exec ");
            DebugWrite(ExecPath);
            DebugWrite("\n");
            /*
             * 无窗：CoopDrain 同步，exit 跳过 ShowPrompt → 此处补。
             * 开窗：WaitPrompt + 异步，exit 再 ShowPrompt（勿此处再打）。
             */
            {
                int WindowApp = ProcessAppTakesWindow(ExecPath);

                if (WindowApp && !HalPlatformIsVirtSerialConsole()) {
                    gConsole.WaitPrompt();
                }
                if (ProcessExec(ExecPath) != 0) {
                    if (WindowApp && !HalPlatformIsVirtSerialConsole()) {
                        gConsole.ShowPrompt();
                    }
                } else if (!WindowApp || HalPlatformIsVirtSerialConsole()) {
                    gConsole.ShowPrompt();
                }
            }
        }
    } else if (Act == DESKTOP_ACTION_SHUTDOWN) {
        HalCpuShutdown();
    } else if (Act == DESKTOP_ACTION_REBOOT) {
        HalCpuReboot();
    }
}

int GuiClickTryContextMenu(UINT32 X, UINT32 Y) {
    DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;

    if (!DesktopContextMenuHandleClick(X, Y, &Act)) {
        return 0;
    }
    if (Act != DESKTOP_ACTION_NONE) {
        GuiClickRunDesktopAction(Act, 0);
    }
    return 1;
}

int GuiClickTryDesktopBar(UINT32 X, UINT32 Y) {
    DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;
    char ExecPath[96];
    int DoDesktop = 0;

    ExecPath[0] = 0;
    /*
     * 开始菜单 / 网络托盘聚焦优先级：
     * 1) 点在菜单/flyout/开始钮/托盘 → DesktopHandleClick
     * 2) 点在菜单外且落在窗上 → HandleTaskbarClick 已收起，再 fall through 聚焦置顶
     * 3) 菜单未开时任务栏仍优先于窗（开始钮）
     */
    if (DesktopStartMenuIsOpen() || DesktopNetTrayIsOpen()) {
        if (DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
            DoDesktop = 1;
        }
        /* 未命中：已收起；继续下面 Raise 窗 */
    } else if (DesktopClickOnTaskbar(X, Y) &&
               DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
        DoDesktop = 1;
    }
    if (!DoDesktop) {
        return 0;
    }
    if (DesktopIconDragActive()) {
        GraphicsIrqEnter();
        CursorRestore();
        GraphicsIrqLeave();
    }
    GuiClickRunDesktopAction(Act, ExecPath);
    return 1;
}

int GuiClickTryDesktopMiss(UINT32 X, UINT32 Y) {
    DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;
    char ExecPath[96];

    ExecPath[0] = 0;
    if (!DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
        return 0;
    }
    /* PR-G-desk-1：与窗标题拖一致，武装拖放前先擦光标，避免留下光标脏块 */
    if (DesktopIconDragActive()) {
        GraphicsIrqEnter();
        CursorRestore();
        GraphicsIrqLeave();
    }
    GuiClickRunDesktopAction(Act, ExecPath);
    return 1;
}
