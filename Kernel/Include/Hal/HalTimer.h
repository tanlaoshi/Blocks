/*
 * HalTimer.h — 节拍门面（K18）
 *
 * X64：LAPIC 周期定时器；其它 Arch：stub（无 tick）。
 */
#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include "BootTypes.h"

/* 装 IDT 向量、开 LAPIC timer；成功 0 */
int HalTimerInit(void);
int HalTimerReady(void);
UINT64 HalTimerTicks(void);
/* 开 IF（sti）；无定时器时也可调用 */
void HalTimerIrqEnable(void);
/* 空闲路径可打 tick 日志；默认空实现（勿刷屏打断 Shell） */
void HalTimerPollLog(void);
/* 调试：当前 LAPIC Current Count（无 timer 则 0） */
UINT32 HalTimerCurCount(void);

#endif
