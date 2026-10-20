/*
 * Style — UiStyle 范例（PR-UI-style-0）
 * 切换样式表，复选框颜色跟着变。
 */
#include <stdio.h>
#include <sched.h>
#include <ToyUi.h>
#include <ToyUiStyle.h>

static int On;
static int UseDark;

static void Draw(int Wid) {
    UiSetLabel(Wid, UseDark ? "dark style" : "light style");
    UiAddButton(Wid, 0, "Toggle");
    UiAddButton(Wid, 1, "Repaint");
    UiAddCheckBox(Wid, 0, 16, 28);
    UiSetCheckBox(Wid, 0, On);
}

int main(void) {
    int Wid;
    int Ev;

    Wid = UiCreateWindow("Style", 420, 280);
    if (Wid < 0) {
        printf("style: create fail\n");
        return 1;
    }
    Draw(Wid);
    for (;;) {
        Ev = UiPoll(Wid);
        if (Ev == UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (Ev == UI_BUTTON_EVENT(0)) {
            UseDark = !UseDark;
            if (UseDark) {
                UI_STYLE S = UiStyleDefault();
                S.CheckFillOn = 0x0000D0FFu;
                S.CheckFillOff = 0x00202020u;
                S.CheckBorder = 0x00FFFFFFu;
                UiSetStyle(&S);
            } else {
                UiSetStyle(0);
            }
            Draw(Wid);
        } else if (Ev == UI_BUTTON_EVENT(1)) {
            Draw(Wid);
        } else if (Ev == UI_CHECK_EVENT(0)) {
            On = UiGetCheckBox(Wid, 0);
        }
        sched_yield();
    }
    return 0;
}
