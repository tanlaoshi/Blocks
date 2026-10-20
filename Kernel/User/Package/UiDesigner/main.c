/*
 * UiDesigner 模板 — 布局编译期生成（见 Documents/开发/拖控件设计器.md）
 */
#include <stdio.h>
#include <unistd.h>
#include <ToyUi.h>
#include <ToyUiLayout.h>
#include <Blocks/syscall.h>
#include "MyAppUi.h"

void UiDispatch(int Wid, int Ev);

int main(void) {
    int Wid;
    int Ev;

    Wid = UiLoadWindow(MYAPP_WIN_TITLE, MYAPP_WIN_W, MYAPP_WIN_H,
                          gMyAppWidgets, MYAPP_WIDGET_COUNT);
    if (Wid < 0) {
        printf("create window failed\n");
        return 1;
    }
    for (;;) {
        Ev = UiPoll(Wid);
        if (Ev == UI_EVENT_CLOSE) {
            break;
        }
        if (Ev == UI_EVENT_NONE) {
            toy_yield();
            continue;
        }
        UiDispatch(Wid, Ev);
    }
    return 0;
}
