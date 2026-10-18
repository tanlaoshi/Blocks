/*
 * GuiUserKey.c — 用户窗键盘入队（焦点路由）
 */
#include "GuiPrivate.h"

void GuiUserEnqueueKey(UINT8 HidKey) {
    int Idx;

    if (HidKey == 0 || HidKey > GUI_USER_KEY_HID_MAX) {
        return;
    }
    Idx = GuiInputOwner();
    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active ||
        gGuiWindowManager.Windows[Idx].Kind != GUI_WIN_USER) {
        return;
    }
    if (gGuiWindowManager.Windows[Idx].UserKeyCount >= GUI_USER_KEY_Q) {
        /* 满则丢最旧，保最新（游戏/连打更要新键） */
        {
            int I;
            for (I = 1; I < GUI_USER_KEY_Q; I++) {
                gGuiWindowManager.Windows[Idx].UserKeyQ[I - 1] = gGuiWindowManager.Windows[Idx].UserKeyQ[I];
            }
            gGuiWindowManager.Windows[Idx].UserKeyCount = GUI_USER_KEY_Q - 1;
        }
    }
    gGuiWindowManager.Windows[Idx].UserKeyQ[gGuiWindowManager.Windows[Idx].UserKeyCount++] = HidKey;
}

int GuiUserDequeueKeyEvent(void) {
    int I;
    UINT8 Hid;

    for (I = 0; I < MAX_WINS; I++) {
        if (!gGuiWindowManager.Windows[I].Active || gGuiWindowManager.Windows[I].Kind != GUI_WIN_USER) {
            continue;
        }
        if (gGuiWindowManager.Windows[I].UserKeyCount == 0) {
            continue;
        }
        Hid = gGuiWindowManager.Windows[I].UserKeyQ[0];
        {
            int J;
            for (J = 1; J < gGuiWindowManager.Windows[I].UserKeyCount; J++) {
                gGuiWindowManager.Windows[I].UserKeyQ[J - 1] = gGuiWindowManager.Windows[I].UserKeyQ[J];
            }
        }
        gGuiWindowManager.Windows[I].UserKeyCount--;
        return GUI_USER_KEY_BASE + (int)Hid;
    }
    return -1;
}
