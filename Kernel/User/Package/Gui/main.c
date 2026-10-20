/*
 * Pkg/Gui — 第一个 GUI 程序模板（PR-L3）
 * 构建：make -C User/Package/Gui
 * 文档：Documents/技术手册.md
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <Ui.h>
#include <Blocks/syscall.h>

int main(void) {
    int Wid;
    int Ev;

    Wid = UiCreateWindow("MyGui", 400, 240);
    if (Wid < 0) {
        printf("mygui: create fail\n");
        return 1;
    }
    UiSetLabel(Wid, "Hello GUI");
    UiAddButton(Wid, 0, "OK");
    printf("mygui: wid=%d\n", Wid);

    for (;;) {
        Ev = UiPoll(Wid);
        if (Ev == UI_EVENT_CLOSE) {
            break;
        }
        if (Ev == UI_BUTTON_EVENT(0)) {
            UiSetLabel(Wid, "OK!");
        } else if (Ev == UI_EVENT_NONE) {
            sched_yield();
        }
    }
    return 0;
}
