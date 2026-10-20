/*
 * Batch — GraphicsBatch 范例（PR-GFX-batch-0）
 * 排空开窗竞态后画一帧；点 Redraw 重画。
 */
#include <stdio.h>
#include <sched.h>
#include <ToyUi.h>
#include <GraphicsBatch.h>

static void Draw(int Wid) {
    GraphicsBatchBegin(Wid);
    GraphicsBatchFillRect(16, 28, 200, 80, 0x00202060u);
    GraphicsBatchDrawRect(16, 28, 200, 80, 0x00FFFFFFu);
    GraphicsBatchDrawLine(16, 28, 216, 108, 0x00FFFF00u);
    GraphicsBatchFillRect(240, 28, 80, 80, 0x00602020u);
    GraphicsBatchDrawRect(240, 28, 80, 80, 0x00FFFFFFu);
    GraphicsBatchEnd();
}

int main(void) {
    int Wid;
    int Ev;

    Wid = UiCreateWindow("Batch", 480, 280);
    if (Wid < 0) {
        printf("batch: create fail\n");
        return 1;
    }
    UiAddButton(Wid, 0, "Redraw");
    int First = 1;
    for (;;) {
        Ev = UiPoll(Wid);
        if (Ev == UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (First && (Ev == UI_EVENT_NONE || Ev == UI_EVENT_CLICK)) {
            Draw(Wid);
            First = 0;
        }
        if (Ev == UI_BUTTON_EVENT(0)) {
            Draw(Wid);
        }
        sched_yield();
    }
    return 0;
}
