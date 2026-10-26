/*
 * Home.c — 首页（策略 A 1000–1099；导航 9000 独立）
 */
#include <Ui.h>
#include "Ids.h"
#include "Home.h"

void HomeCreate(int Wid) {
    UiSetLabel(Wid, "Home 1000  nav=9000");
    UiAddButton(Wid, 0, "Ping");
    UiAddButton(Wid, 1, "->Set");
    UiAddButton(Wid, 2, "->About");
}

void HomeDestroy(int Wid) {
    /* 切页前清标签；按钮槽由下页 Create 覆盖 */
    UiSetLabel(Wid, "");
}

void HomeDispatch(int Wid, int Id) {
    if (Id == ID_HOME_PING) {
        UiSetLabel(Wid, "Home ping 1000");
    }
}
