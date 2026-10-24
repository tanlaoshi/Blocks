/*
 * Gui — 开窗 + 按钮；链 libUi + libGraphics
 */
#include <stdio.h>
#include <Ui.h>
#include <Blocks/thread.h>

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

    for (;;) {
        Ev = UiPoll(Wid);
        if (Ev == UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (Ev == UI_BUTTON_EVENT(0)) {
            UiSetLabel(Wid, "OK!");
        } else if (Ev == UI_EVENT_NONE) {
            ThreadYield();
        }
    }
    return 0;
}
