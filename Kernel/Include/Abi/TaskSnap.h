/*
 * TaskSnap.h — 任务快照（SYS_TASK_SNAP）
 *
 * 内核和用户共用这一份。字段顺序和宽度不变；改字段须升 TASK_SNAP_VER。
 */
#ifndef TASK_SNAP_H
#define TASK_SNAP_H

#include <stdint.h>

#define TASK_SNAP_MAGIC 0x31535054u /* 'TPS1' LE */
#define TASK_SNAP_VER   1
#define TASK_SNAP_MAX   16
#define TASK_NAME_MAX   16

#define TASK_F_USER    0x1u
#define TASK_F_ZOMBIE  0x2u
#define TASK_F_BLOCKED 0x4u
#define TASK_F_CURRENT 0x8u

typedef struct {
    int32_t  Pid;      /* 与 Shell ps / kill 一致：槽位+1 */
    int32_t  Parent;   /* 父 pid；无则 0 */
    uint32_t Flags;    /* TASK_F_* */
    int32_t  Priority;
    int32_t  OnCpu;
    int32_t  HomeCpu;
    uint32_t Ticks;
    char     Name[TASK_NAME_MAX];
} TASK_ENTRY;

typedef struct {
    uint32_t       Magic;
    uint32_t       Version;
    uint32_t       Count;
    uint32_t       FreePages;   /* PhysicalMemory 空闲页数 */
    uint64_t       CpuTicks;    /* HalCpuTicks(0) */
    uint32_t       WorkerLoops;
    uint32_t       Pad0;
    uint64_t       StealCount;
    int32_t        SelfPid;
    int32_t        Pad1;
    TASK_ENTRY Tasks[TASK_SNAP_MAX];
} TASK_SNAP;

_Static_assert(sizeof(TASK_ENTRY) == 44, "task entry width");
_Static_assert(sizeof(TASK_SNAP) == 752, "task snap width");

#endif
