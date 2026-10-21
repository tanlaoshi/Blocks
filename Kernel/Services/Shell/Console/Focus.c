/*
 * Focus.c — Shell 焦点 / 开窗 / 重画（PR-S3-console-2）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Locale.h"
#include "SettingsUi.h"
#include "LibWrite.h"
#include "CoreOps.h"

/* 将控制台输出限制在当前焦点窗口客户区内（不重置光标） */
static void FocusSave(void) {
    int Idx = GuiFocusIndex();

    if (Idx >= 0 && GuiShellWindowActive(Idx)) {
        ConsoleSbWinSave(Idx);
    }
    GuiConsolePush(gLine, gLen, gWaitPrompt);
}

static void FocusLoad(void) {
    int Idx = GuiFocusIndex();
    int Hold = gConsole.StdinUserHold();

    /*
     * ↑↓ 回填期间勿 Pull：EnsureShell/Raise 会 FocusLoad，
     * 否则窗槽旧行盖住 gLine，串口已回显与缓冲分叉（BOX-1）。
     */
    if (!gConsole.HistIsBusy() && !gConsole.HistIgnoreRx()) {
        GuiConsolePull(gLine, &gLen, &gWaitPrompt);
    }
    /*
     * Compose/切窗会把窗内 WaitPrompt 清 0 再 Pull，导致 exec 中 shell 抢串口
     * （chat> 下 HI → unknown: HI）。有活用户时保持占 stdin。
     */
    if (Hold && gWaitPrompt == 0) {
        gWaitPrompt = 1;
    }
    if (Idx >= 0 && GuiShellWindowActive(Idx)) {
        ConsoleSbWinLoad(Idx);
    }
    /* 非 Shell 焦点（如 Settings）不碰控制台绘制 */
    if (!GuiShellAcceptsInput()) {
        return;
    }
    if (GuiConsoleHasDisplay()) {
        GuiFocusApplyClip();
        if (GuiConsoleNeedsPrompt()) {
            /*
             * RequestPrompt（tty/settings 开窗抢走焦点后 PromptAfterCommand）
             * 或空窗：须再打 blocks>。有 scrollback 时勿再打欢迎语。
             */
            if (!ConsoleSbHasContent()) {
                gConsole.Write(LocStr(MSG_CON_WELCOME));
                gConsole.Write("\n");
            } else {
                ConsoleSbRepaint();
            }
            Prompt();
            GuiConsoleMarkPrompt();
            GuiFocusSave();
        } else if (ConsoleSbHasContent()) {
            ConsoleSbRepaint();
        }
        return;
    }
    /*
     * 无客户区光标：主题清空后的 Shell，或尚未 OnShellOpened 的新窗。
     * 新窗 OpenShell 先 PromptShown=1 抑制此处；OnShellOpened 再画。
     */
    if (GuiConsoleNeedsPrompt()) {
        if (!ConsoleSbHasContent()) {
            GuiFocusHome();
            gConsole.Write(LocStr(MSG_CON_WELCOME));
            gConsole.Write("\n");
        }
        Prompt();
        GuiConsoleMarkPrompt();
        GuiFocusSave();
    }
}

static void BindFocus(void) {
    GuiFocusApply();
}

/* 初始化：无 Shell 时仅串口就绪文案；开窗 / gConsole.SerialRun 再画欢迎语与 blocks> */
static void Initialize(void) {
    GUI_CONSOLE_OPS Ops;

    Ops.FocusSave = gConsole.FocusSave;
    Ops.FocusLoad = gConsole.FocusLoad;
    Ops.OnShellOpened = gConsole.OnShellOpened;
    Ops.PaintShellWindow = gConsole.PaintShellWindow;
    GuiRegisterConsoleOps(&Ops);
    LibWriteRegister(gConsole.Write);
    {
        CONSOLE_IO_OPS Io;

        Io.WriteLen = gConsole.WriteLen;
        Io.ReadChar = gConsole.StdinGetChar;
        Io.ShowPrompt = gConsole.ShowPrompt;
        ConsoleIoRegister(&Io);
    }

    gLen = 0;
    gWaitPrompt = 0;
    gAtLineStart = 1;
    /*
     * 勿在此打 ready / blocks>：iwl 在 Worker 里 Claim+BgPump，会插到提示符后面。
     * 桌面：Worker 在 iwl 后台、首轮 DHCP、以及之后一小段 iwl 黄字静默后再 Announce。
     * ConsoleOnly：gConsole.SerialRun 里 Announce。
     */
}

static void OnShellOpened(void) {
    int Idx;

    /*
     * 勿用 GuiShellAcceptsInput：刚 OpenChromeDefer 时 z-order 可能仍判遮挡，
     * 会整段跳过欢迎语。按窗是否为 Shell 即可。
     */
    Idx = GuiFocusIndex();
    if (!GuiShellWindowActive(Idx)) {
        return;
    }
    /* 新窗槽为空 → 欢迎语；已有槽（主题重画）→ 重绘本窗历史 */
    gConsole.PaintShellWindow(Idx);
}

/* PR-G8：主题合成时按窗下标画 Shell，不要求当前可输入/未遮挡 */
static void PaintShellWindow(int Idx) {
    int Saved;

    if (!GuiShellWindowActive(Idx)) {
        return;
    }
    Saved = GuiFocusIndex();
    if (Saved >= 0 && Saved != Idx && GuiShellWindowActive(Saved)) {
        ConsoleSbWinSave(Saved);
    }
    GuiSetFocusWindow(Idx);
    ConsoleSbWinLoad(Idx);
    /*
     * 开开始菜单等会走 GuiComposeThemeScene → 本函数。
     * 若仍 SbReset+欢迎语，ps/help 输出会被清掉；有行缓冲则重绘恢复。
     */
    if (ConsoleSbWinHasContent(Idx) || ConsoleSbHasContent()) {
        ConsoleSbRepaint();
        /* Theme 清过 PromptShown；重绘后勿让 FocusLoad 再打欢迎语 */
        GuiConsoleMarkPrompt();
        ConsoleSbWinSave(Idx);
        /* 与空窗路径一致：勿把客户区 clip 留给随后的开始菜单/任务栏 */
        HalVideoClearClip();
        if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
            GuiSetFocusWindow(Saved);
            if (GuiShellWindowActive(Saved)) {
                ConsoleSbWinLoad(Saved);
            }
        }
        return;
    }
    gLen = 0;
    gWaitPrompt = 0;
    gAtLineStart = 1;
    ConsoleSbReset();
    GuiFocusClearClient();
    GuiFocusHome();
    gConsole.Write(LocStr(MSG_CON_WELCOME));
    gConsole.Write("\n");
    Prompt();
    GuiConsoleMarkPrompt();
    ConsoleSbWinSave(Idx);
    GuiConsolePush(gLine, gLen, gWaitPrompt);
    HalVideoClearClip();
    GuiBackupFocusWindow();
    if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
        GuiSetFocusWindow(Saved);
        if (GuiShellWindowActive(Saved)) {
            ConsoleSbWinLoad(Saved);
        }
    }
}

static void RepaintShellWindows(void) {
    int Saved;
    int i;
    GUI_WIN_KIND SavedKind;

    Saved = GuiFocusIndex();
    SavedKind = GuiFocusKind();

    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) != GUI_WIN_SHELL) {
            continue;
        }
        /* 置顶后再画，避免欢迎语/prompt 写穿上层 Settings */
        GuiRaiseToFront(i);
        gConsole.OnShellOpened();
    }

    if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
        GuiRaiseToFront(Saved);
        if (SavedKind == GUI_WIN_SHELL) {
            gConsole.FocusLoad();
        } else if (SavedKind == GUI_WIN_SETTINGS) {
            SettingsUiRefresh();
        }
    }
    HalVideoClearClip();
}

void ConsolePublishFocus(CONSOLE_OPS *Ops) {
    Ops->Initialize = Initialize;
    Ops->BindFocus = BindFocus;
    Ops->OnShellOpened = OnShellOpened;
    Ops->PaintShellWindow = PaintShellWindow;
    Ops->RepaintShellWindows = RepaintShellWindows;
    Ops->FocusSave = FocusSave;
    Ops->FocusLoad = FocusLoad;
}
