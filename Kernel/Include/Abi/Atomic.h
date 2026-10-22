/*
 * Atomic.h — 原子与屏障垫片（c99-atomic）
 *
 * 业务写 BLOCKS_ATOMIC_*，不写 __sync_*。实现仍走 GCC/Clang 内建；
 * 换编译器时优先改本头。不引入 C11 <stdatomic.h>（自有树锁 -std=c99）。
 */
#ifndef ABI_ATOMIC_H
#define ABI_ATOMIC_H

#if defined(__GNUC__) || defined(__clang__)

/* 返回旧值；非 0 表示曾被占用（自旋锁 TAS） */
#define BLOCKS_ATOMIC_TAS(Ptr, Val) \
    __sync_lock_test_and_set((Ptr), (Val))

#define BLOCKS_ATOMIC_RELEASE(Ptr) \
    __sync_lock_release(Ptr)

/* 返回加之前的旧值 */
#define BLOCKS_ATOMIC_FETCH_ADD(Ptr, Val) \
    __sync_fetch_and_add((Ptr), (Val))

/* 成功返回非 0 */
#define BLOCKS_ATOMIC_CAS(Ptr, Expected, Desired) \
    __sync_bool_compare_and_swap((Ptr), (Expected), (Desired))

#define BLOCKS_ATOMIC_FENCE() \
    __sync_synchronize()

#else
#error "Atomic.h: define BLOCKS_ATOMIC_* for this compiler"
#endif

#endif /* ABI_ATOMIC_H */
