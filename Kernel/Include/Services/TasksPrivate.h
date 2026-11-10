/*
 * TasksPrivate.h — 常驻任务内部（仅 Services/Tasks；User 勿 include）
 */
#ifndef TASKS_PRIVATE_H
#define TASKS_PRIVATE_H

#include "Tasks.h"
#include "HalDevices.h"

/* YieldForPollInput → Tasks.h（Accept/Serve 亦调） */
void FeedHid(HAL_KEYBOARD_REPORT *Report, HAL_KEYBOARD_REPORT *Previous);
/* 用户态 poll/sleep 间也可抽键：不依赖 ShellTask 同核轮转 */
void TasksPumpKeyboard(void);
/* PR-19 修复：按住方向键的重复 tick；ShellTask 每圈无条件调（无新键也走） */
void TasksHidRepeatTick(void);
/* FeedHidFocus.c · PR-NORM-7 */
void HidRepeatArm(UINT8 Key);
int FeedHidTrySettings(UINT8 Key, const HAL_KEYBOARD_REPORT *Report);
int FeedHidTryEdit(UINT8 Key, const HAL_KEYBOARD_REPORT *Report);
int FeedHidTryTty(UINT8 Key, const HAL_KEYBOARD_REPORT *Report);
int FeedHidTryStore(UINT8 Key);
int FeedHidTryFiles(UINT8 Key, const HAL_KEYBOARD_REPORT *Report);
int FeedHidTryUserWindow(UINT8 Key);
int FeedHidTryStdinHold(UINT8 Key, const HAL_KEYBOARD_REPORT *Report);
void FeedHidShellDefault(UINT8 Key, const HAL_KEYBOARD_REPORT *Report);

#endif
