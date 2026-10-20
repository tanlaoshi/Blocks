/*
 * UiActions — 动作桩；装载见 UiLayout.c
 * ID 须与 MyApp.uitxt → MyAppUi.h 一致（改 uitxt 后同步本文件）
 */
#include <Ui.h>
#include "MyAppUi.h"

void OnIdHello(int Wid) {
    (void)Wid;
}

void OnIdOk(int Wid) {
    UiSetLabel(Wid, "OK");
}

void OnIdCancel(int Wid) {
    UiSetLabel(Wid, "Cancel");
}

void OnIdChk(int Wid) {
    (void)Wid;
}

void OnIdGo(int Wid) {
    UiSetLabel(Wid, "Go");
}

void UiDispatch(int Wid, int Ev) {
    if (Ev == UI_EVENT_CLOSE) {
        return;
    }
    if (Ev == UI_BUTTON_EVENT(ID_OK)) {
        OnIdOk(Wid);
    } else if (Ev == UI_BUTTON_EVENT(ID_CANCEL)) {
        OnIdCancel(Wid);
    } else if (Ev == UI_BUTTON_EVENT(ID_GO)) {
        OnIdGo(Wid);
    } else if (Ev == UI_CHECK_EVENT(ID_CHK)) {
        OnIdChk(Wid);
    } else if (Ev == UI_EVENT_CLICK) {
        OnIdHello(Wid);
    }
}
