/*
 * UiActions — 动作桩（与当前 MyApp.uitxt / 设计器导出 ID 对齐）
 */
#include <ToyUi.h>
#include "MyAppUi.h"

void OnIdBtn1(int Wid) {
    UiSetLabel(Wid, "Button");
}

void OnIdLbl1(int Wid) {
    (void)Wid;
}

void OnIdChk1(int Wid) {
    (void)Wid;
}

void OnIdTxt1(int Wid) {
    (void)Wid;
}

void UiDispatch(int Wid, int Ev) {
    if (Ev == UI_EVENT_CLOSE) {
        return;
    }
    if (Ev == UI_BUTTON_EVENT(ID_BTN1)) {
        OnIdBtn1(Wid);
    } else if (Ev == UI_CHECK_EVENT(ID_CHK1)) {
        OnIdChk1(Wid);
    } else if (Ev == UI_EVENT_CLICK) {
        OnIdLbl1(Wid);
    } else if (Ev == UI_TEXT_EVENT(ID_TXT1)) {
        OnIdTxt1(Wid);
    }
}
