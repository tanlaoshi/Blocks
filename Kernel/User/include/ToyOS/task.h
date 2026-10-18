/*
 * toyos/task.h — 任务快照（SYS_TASK_SNAP；与 Shell `ps` 同源字段）
 *
 * 布局钉死：改字段须升 TOY_TASK_SNAP_VER，并改开课 ABI / 应用开发指南。
 */
#ifndef TOYOS_TASK_H
#define TOYOS_TASK_H

#include <ToyOS/syscall.h>
#include "TaskSnap.h"

/* 成功 0（Out 已填）；失败 -1 */
static inline int toy_task_snap(TOY_TASK_SNAP *Out) {
    long R;

    if (!Out) {
        return -1;
    }
    R = toy_syscall(SYS_TASK_SNAP, (long)Out, (long)sizeof(*Out), 0);
    return (R == 0) ? 0 : -1;
}

#endif
