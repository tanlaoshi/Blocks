/*
 * Tasks.h — 内核常驻任务入口（SchedulerStart 后四环）
 *
 * Shell / Gui → 交互核；Worker → BSP；Input → CPU2（Count≥HAL_INPUT_CPU_MIN）。
 * drain 分域见 Tasks.c Yield 头；门闩清单 Tasks.c 头；钉核 sched-pin-doc。
 */
#ifndef TASKS_H
#define TASKS_H

#include "BootTypes.h"

void ShellTask(void);
void GuiTask(void);
void WorkerTask(void);
void InputTask(void);   /* Count≥HAL_INPUT_CPU_MIN → CPU2；独占 HalInputPoll */

UINT32 WorkerLoopCount(void);
/* 焦点 USER 时：dequeue → FeedHid（供 GuiPollUserInput，不靠 Shell 同核） */
void TasksPumpKeyboard(void);

#endif
