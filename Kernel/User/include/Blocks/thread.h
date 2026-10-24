/*
 * Blocks/thread.h — 线程子集（selfhost：Thread* 本位名）
 */
#ifndef BLOCKS_THREAD_H
#define BLOCKS_THREAD_H

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <Blocks/syscall.h>

void ToyThreadRoot(void *(*Start)(void *), void *Arg);

static inline long ThreadCreate(void *(*Start)(void *), void *Arg) {
    return blocks_syscall(SYS_THREAD_CREATE, (long)ToyThreadRoot, (long)Start,
                          (long)Arg);
}

static inline long ThreadJoin(long tid, int *status) {
    BLOCKS_RET2 R = blocks_syscall2(SYS_THREAD_JOIN, tid, (long)status, 0);
    if (status && R.A == 0) {
        *status = (int)R.B;
    }
    return R.A;
}

static inline long ThreadExit(long code) {
    return blocks_syscall(SYS_THREAD_EXIT, code, 0, 0);
}

static inline long ThreadGetTid(void) {
    return blocks_syscall(SYS_THREAD_GET_TID, 0, 0, 0);
}

static inline long ThreadYield(void) {
    return blocks_syscall(SYS_THREAD_YIELD, 0, 0, 0);
}

static inline unsigned long ThreadTlsTid(void) {
#if defined(__x86_64__)
    unsigned long V;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(V));
    return V;
#elif defined(__aarch64__)
    unsigned long V;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(V));
    return *(unsigned long *)V;
#elif defined(__riscv)
    unsigned long V;
    __asm__ volatile("mv %0, tp" : "=r"(V));
    return *(unsigned long *)V;
#else
    return 0;
#endif
}

#endif /* !__ASSEMBLER__ */
#endif /* BLOCKS_THREAD_H */
