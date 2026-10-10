/*
 * HalSyscallStub.c — 非 X64：无 int 0x80 用户态门
 *
 * 【初学者】
 * - Hal 占位；Console Process/ELF 在 Arm/RiscV 不装 syscall 向量。
 * - 真实现：Hal/X64/HalSyscall.c。
 */
#include "HalSyscall.h"

int HalSyscallInitialize(void) {
    return -1;
}

int HalSyscallRun(UINT64 Entry, UINT64 StackTop) {
    (void)Entry;
    (void)StackTop;
    return -1;
}
