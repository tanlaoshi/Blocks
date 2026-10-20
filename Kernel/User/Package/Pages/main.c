/*
 * Pages — 2 页 App（PR-UI-page-1：页面表 + 导航 9000 + Destroy/Create）
 */
#include <stdio.h>
#include <sched.h>
#include <ToyUi.h>
#include "Route.h"

int main(void) {
    int Wid;
    int Ev;

    Wid = UiCreateWindow("Pages", 480, 280);
    if (Wid < 0) {
        printf("pages: create fail\n");
        return 1;
    }
    RouteInit(Wid);
    for (;;) {
        Ev = UiPoll(Wid);
        if (RouteDispatch(Wid, Ev) != 0) {
            break;
        }
        if (Ev == UI_EVENT_NONE || Ev == UI_EVENT_CLICK) {
            sched_yield();
        }
    }
    return 0;
}
