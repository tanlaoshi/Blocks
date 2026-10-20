/*
 * GuiDemo.c — 链 libToyUi/libGraphics（课堂对照：Documents/技术手册.md）
 * Shell：exec GUIDEMO.ELF
 * 键盘入窗：焦点在本窗时按键 → UiPoll 返回 UI_KEY_EVENT(hid)
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <ToyUi.h>
#include <ToyOS/syscall.h>

int main(void) {
    int WindowId;
    int Event;
    char Line[48];

    WindowId = UiCreateWindow("GuiDemo", 480, 280);
    if (WindowId < 0) {
        printf("guidemo: create fail\n");
        return 1;
    }
    if (UiSetLabel(WindowId, "Click OK or type a key") != 0) {
        printf("guidemo: label fail\n");
        return 1;
    }
    if (UiAddButton(WindowId, 0, "OK") != 0 ||
        UiAddButton(WindowId, 1, "Cancel") != 0) {
        printf("guidemo: button fail\n");
        return 1;
    }
    /* 丢掉开窗/加按钮竞态里误入的点击，避免一进来就是 OK clicked */
    for (;;) {
        Event = UiPoll(WindowId);
        if (Event <= 0) {
            break;
        }
        if (Event == UI_EVENT_CLOSE) {
            printf("guidemo: closed during setup\n");
            return 0;
        }
    }
    /* 日志只走串口：焦点在 USER 时 printf 不再画进本窗 */
    printf("guidemo: wid=%d (ToyUi %s / Graphics %s)\n", WindowId,
           UI_ABI_VERSION_STRING, GRAPHICS_ABI_VERSION_STRING);
    for (;;) {
        Event = UiPoll(WindowId);
        if (Event == UI_EVENT_CLOSE || Event < 0) {
            break;
        }
        if (Event == UI_BUTTON_EVENT(0)) {
            UiSetLabel(WindowId, "OK clicked");
        } else if (Event == UI_BUTTON_EVENT(1)) {
            UiSetLabel(WindowId, "Cancel clicked");
        } else if (UI_IS_KEY(Event)) {
            snprintf(Line, sizeof(Line), "key hid=0x%02x",
                     UI_KEY_CODE(Event));
            UiSetLabel(WindowId, Line);
            printf("guidemo: %s\n", Line);
        } else {
            sched_yield();
        }
    }
    printf("guidemo: closed ok\n");
    return 0;
}
