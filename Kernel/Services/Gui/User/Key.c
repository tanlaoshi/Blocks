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
    if (gGuiWindowManager.UserSlot[Idx].UserKeyCount >= GUI_USER_KEY_Q) {
        /* 满则丢最旧，保最新（游戏/连打更要新键） */
        {
            int I;
            for (I = 1; I < GUI_USER_KEY_Q; I++) {
                gGuiWindowManager.UserSlot[Idx].UserKeyQ[I - 1] = gGuiWindowManager.UserSlot[Idx].UserKeyQ[I];
            }
            gGuiWindowManager.UserSlot[Idx].UserKeyCount = GUI_USER_KEY_Q - 1;
        }
    }
    gGuiWindowManager.UserSlot[Idx].UserKeyQ[gGuiWindowManager.UserSlot[Idx].UserKeyCount++] = HidKey;
}

int GuiUserDequeueKeyEvent(void) {
    int I;
    UINT8 Hid;

    for (I = 0; I < MAX_WINS; I++) {
        if (!gGuiWindowManager.Windows[I].Active || gGuiWindowManager.Windows[I].Kind != GUI_WIN_USER) {
            continue;
        }
        if (gGuiWindowManager.UserSlot[I].UserKeyCount == 0) {
            continue;
        }
        Hid = gGuiWindowManager.UserSlot[I].UserKeyQ[0];
        {
            int J;
            for (J = 1; J < gGuiWindowManager.UserSlot[I].UserKeyCount; J++) {
                gGuiWindowManager.UserSlot[I].UserKeyQ[J - 1] = gGuiWindowManager.UserSlot[I].UserKeyQ[J];
            }
        }
        gGuiWindowManager.UserSlot[I].UserKeyCount--;
        return GUI_USER_KEY_BASE + (int)Hid;
    }
    return -1;
}
