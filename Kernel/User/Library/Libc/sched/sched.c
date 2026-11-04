/*
 * sched.c — sched_yield（PR-U-sched-yield）
 * 直接 blocks_syscall(SYS_THREAD_YIELD)；勿调 blocks_yield 宏（会递归回本函数）。
 */
#include <sched.h>
#include <Blocks/syscall.h>

int sched_yield(void) {
    return (int)blocks_syscall(SYS_THREAD_YIELD, 0, 0, 0);
}
