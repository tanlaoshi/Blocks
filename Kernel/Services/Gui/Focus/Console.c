/*
 * GuiFocusConsole.c — Shell 输入行与是否接受输入
 * 核心：GuiFocus.c
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"

void GuiConsolePull(char *Line, int *Len, int *WaitPrompt) {
    GUI_WINDOW *Win;

    /*
     * 只从 SHELL 槽同步输入行 / WaitPrompt。
     * 开 USER 窗末尾 GuiFocusApply→FocusLoad 若 Pull 到 USER（槽内 WaitPrompt=0），
     * 会抹掉桌面/exec 已占的 gWaitPrompt，关窗后再 ShowPrompt 行为漂移。
     * 焦点无效或非 Shell：不动调用方缓冲（勿清 WaitPrompt）。
     */
    if (gGuiWindowManager.FocusWin < 0 || gGuiWindowManager.FocusWin >= MAX_WINS ||
        !gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active) {
        return;
    }
    Win = &gGuiWindowManager.Windows[gGuiWindowManager.FocusWin];
    if (Win->Kind != GUI_WIN_SHELL) {
        return;
    }
    if (Line) {
        int i;
        for (i = 0; i < Win->InputLen && i < GUI_INPUT_LINE_MAX - 1; i++) {
            Line[i] = Win->InputLine[i];
        }
        Line[i] = 0;
    }
    if (Len) {
        *Len = Win->InputLen;
    }
    if (WaitPrompt) {
        *WaitPrompt = Win->WaitPrompt;
    }
}


void GuiConsolePush(const char *Line, int Len, int WaitPrompt) {
    GUI_WINDOW *Win;
    int i;

    if (gGuiWindowManager.FocusWin < 0 || gGuiWindowManager.FocusWin >= MAX_WINS ||
        !gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active) {
        return;
    }
    Win = &gGuiWindowManager.Windows[gGuiWindowManager.FocusWin];
    if (Win->Kind != GUI_WIN_SHELL) {
        return;
    }
    if (Len >= GUI_INPUT_LINE_MAX) {
        Len = GUI_INPUT_LINE_MAX - 1;
    }
    Win->InputLen = Len;
    Win->WaitPrompt = WaitPrompt;
    for (i = 0; i < Len; i++) {
        Win->InputLine[i] = Line[i];
    }
    Win->InputLine[Len] = 0;
}

void GuiConsoleSetWaitPrompt(int Idx, int WaitPrompt) {
    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return;
    }
    gGuiWindowManager.Windows[Idx].WaitPrompt = WaitPrompt;
}


int GuiConsoleNeedsPrompt(void) {
    if (gGuiWindowManager.FocusWin < 0 || gGuiWindowManager.FocusWin >= MAX_WINS || !gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active) {
        return 0;
    }
    return !gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].PromptShown;
}


void GuiConsoleMarkPrompt(void) {
    if (gGuiWindowManager.FocusWin < 0 || gGuiWindowManager.FocusWin >= MAX_WINS || !gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active) {
        return;
    }
    gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].PromptShown = 1;
}


void GuiShellRequestPrompt(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_SHELL) {
            gGuiWindowManager.Windows[i].PromptShown = 0;
        }
    }
}


int GuiConsoleHasDisplay(void) {
    if (gGuiWindowManager.FocusWin < 0 || gGuiWindowManager.FocusWin >= MAX_WINS || !gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active) {
        return 0;
    }
    return gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].TermSet;
}

int GuiShellAcceptsInput(void) {
    return gGuiWindowManager.FocusWin >= 0 && gGuiWindowManager.FocusWin < MAX_WINS &&
           gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active &&
           gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Kind == GUI_WIN_SHELL &&
           !WindowOccludedByOther(gGuiWindowManager.FocusWin);
}


int GuiShellWindowActive(int Idx) {
    return Idx >= 0 && Idx < MAX_WINS && gGuiWindowManager.Windows[Idx].Active &&
           gGuiWindowManager.Windows[Idx].Kind == GUI_WIN_SHELL;
}
