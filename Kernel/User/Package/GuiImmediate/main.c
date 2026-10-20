/*
 * GuiIm — UiIm Label/Check/Button 范例（PR-UI-im-1，≈ GuiDemo）
 */
#include <stdio.h>
#include <sched.h>
#include <ToyUi.h>
#include <ToyUiImmediate.h>

int main(void) {
    int Wid;
    int On;
    const char *Msg;

    Wid = UiCreateWindow("GuiIm", 480, 280);
    if (Wid < 0) {
        printf("guiim: create fail\n");
        return 1;
    }
    On = 0;
    Msg = "Click OK or check";
    for (;;) {
        if (UiImBegin(Wid, 0) != 0) {
            break;
        }
        if (UiImClosed()) {
            UiImEnd();
            break;
        }
        if (UiImCheck(&On)) {
            Msg = On ? "check on" : "check off";
        }
        if (UiImButton("OK")) {
            Msg = "OK clicked";
        }
        if (UiImButton("Cancel")) {
            Msg = "Cancel clicked";
        }
        UiImLabel(Msg);
        UiImEnd();
        sched_yield();
    }
    return 0;
}
