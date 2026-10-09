#ifndef HAL_SYSCALL_H
#define HAL_SYSCALL_H

#include "BootTypes.h"

/* 装 int 0x80 门；保存/恢复用户返回点 */
int HalSyscallInit(void);
/* 进入 Image.Entry，栈为 StackTop；exit 后返回 0 */
int HalSyscallRun(UINT64 Entry, UINT64 StackTop);

#endif
