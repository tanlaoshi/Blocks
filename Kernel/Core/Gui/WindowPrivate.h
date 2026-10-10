/*
 * WindowPrivate.h — Window.c 与 WindowManage.c 共享的窗状态
 */
#ifndef WINDOW_PRIVATE_H
#define WINDOW_PRIVATE_H

#include "Window.h"
#include "Locale.h"

typedef struct {
    int On;
    int Focus;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    LOC_MSG Title;
} GUI_WIN;

extern GUI_WIN gWindows[GUI_WIN_COUNT];
extern UINT8 gWindowZOrder[GUI_WIN_COUNT];
extern UINT32 gWindowFbW;
extern UINT32 gWindowFbH;

void WindowCompose(void);
void WindowPresentFull(void);

#endif
