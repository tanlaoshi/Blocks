/*
 * Click.c — 按下命中编排（story-s1 / PR-F-guiclick-1）
 *
 * 稳态故事 S1 入口之一：GuiPollMouse 边沿 → 这里 → 分发或开窗。
 *
 * 序（先到先得，改序要手测关钮被挡 / 菜单外点窗）：
 *   1 ContextMenu → 2 全窗 ×（CloseWindow）→ 3 任务栏/菜单（TryDesktopBar）
 *   → 4 USER 客户区 → 5 置顶已有窗 → 6 桌面落空（图标双击 / 菜单）
 * Action→开窗：ClickDesktop.c GuiClickRunDesktopAction。
 * 上游：Pointer.c；桌面几何：Desktop/Click.c（图标要双击）。
 */
#include "GuiPrivate.h"

int GuiHandleClick(UINT32 X, UINT32 Y) {
    int i;

    if (GuiClickTryContextMenu(X, Y)) {
        return 1;
    }
    /* 关闭钮可能被其它窗口挡住；先扫一遍所有窗口的 × 区域 */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gGuiWindowManager.Windows[i].Active && PointInClose(&gGuiWindowManager.Windows[i], X, Y)) {
            CloseWindow(i);
            return 1;
        }
    }

    if (GuiClickTryDesktopBar(X, Y)) {
        return 1;
    }
    if (GuiClickTryUserClient(X, Y)) {
        return 1;
    }
    if (GuiClickTryRaiseWindow(X, Y)) {
        return 1;
    }
    /* 未点中窗口：桌面图标（双击打开） / 开始菜单 */
    return GuiClickTryDesktopMiss(X, Y);
}
