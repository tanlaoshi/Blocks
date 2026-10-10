/*
 * ProcessFork.h — K50：最小 fork/wait（子先跑完再恢复父）
 */
#ifndef PROCESS_FORK_H
#define PROCESS_FORK_H

#include "BootTypes.h"
#include "VirtualMemory.h"

/* ProcessExecPath 进出用户时挂接当前 Space */
void ProcessForkAttach(VIRTUAL_ADDRESS_SPACE *Space);
void ProcessForkDetach(void);

/* 系统调用：Regs[0]=rax…；返回 1=已处理 */
int ProcessForkSyscall(UINT64 *Regs);
int ProcessWaitSyscall(UINT64 *Regs);
/* exit：若为子进程则恢复父并返回 1（勿结束 HalSyscallRun） */
int ProcessExitSyscall(UINT64 *Regs);

#endif
