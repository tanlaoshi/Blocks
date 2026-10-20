/*
 * Settings.c — 设置页（策略 A 1100–1199；导航 9000 独立）
 */
#include <Ui.h>
#include "Ids.h"
#include "Settings.h"

void SettingsCreate(int Wid) {
    UiSetLabel(Wid, "Settings 1100  nav=9001");
    UiAddButton(Wid, 0, "OK");
    UiAddButton(Wid, 1, "->Home");
    UiAddButton(Wid, 2, "->About");
}

void SettingsDestroy(int Wid) {
    UiSetLabel(Wid, "");
}

void SettingsDispatch(int Wid, int Id) {
    if (Id == ID_SET_OK) {
        UiSetLabel(Wid, "Settings OK 1100");
    }
}
