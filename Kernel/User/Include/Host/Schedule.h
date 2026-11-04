/*
 * Host/Schedule.h — ThreadYield 的 C 名（教学；实现见 Library/Blocks/thread/sched.c）
 * 走 SYS_THREAD_YIELD（150）；App 主路径更常用 <Blocks/Thread.h> 的 ThreadYield。
 */
#ifndef SCHED_H
#define SCHED_H

int sched_yield(void);

#endif
