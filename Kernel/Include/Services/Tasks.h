/*
 * Tasks.h — 内核常驻任务入口（SchedulerStart 后四环）
 *
 * Shell / Gui → 交互核；Worker → BSP；Input → CPU2（仅 SMP≥3）。
 * 钉核见路线图新 sched-pin-doc；环序见 runtime-4 / Tasks.c·Shell.c 文件头。
 */
#ifndef TASKS_H
#define TASKS_H

#include "BootTypes.h"

void ShellTask(void);
void GuiTask(void);
void WorkerTask(void);
void InputTask(void);   /* SMP≥3 → CPU2；独占 HalInputPoll */

UINT32 WorkerLoopCount(void);
/* 焦点 USER 时：dequeue → FeedHid（供 GuiPollUserInput，不靠 Shell 同核） */
void TasksPumpKeyboard(void);

#endif
