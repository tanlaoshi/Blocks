/*
 * Tasks.h — 内核常驻任务入口（SchedulerStart 后四环）
 *
 * Shell / Gui → 交互核；Worker → BSP；Input → CPU2（仅逻辑 CPU≥3）。
 * ≥3=分出 0/1/2 槽，非「必须三核」。drain 分域见 Tasks.c YieldForPollInput 头；
 * 钉核 sched-pin-doc；环序 runtime-4 / Shell.c 头。
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
