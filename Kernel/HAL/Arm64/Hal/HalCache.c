/*
 * HalCache.c — 数据 cache 维护（Arm64）
 *
 * ARMv8：dc civac 清到 Point of Coherency，供 DMA/VideoCore 见 CPU 写入。
 * Pi 真机：U-Boot 常开 D-cache；MMU 关闭后仍可能命中脏行 → FB 花屏。
 */
#include "BootTypes.h"

void HalDcacheCleanInvalidateRange(void *Va, UINTN Len) {
    UINT64 A;
    UINT64 End;

    if (Va == 0 || Len == 0) {
        return;
    }
    A = (UINT64)(UINTN)Va & ~63ULL;
    End = (UINT64)(UINTN)Va + Len;
    for (; A < End; A += 64ULL) {
        __asm__ volatile("dc civac, %0" ::"r"(A) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}
