/*
 * proc.c — ProcessGetPid / ProcessGetPpid
 */
#include <Blocks/Process.h>
#include <Blocks/syscall.h>

pid_t ProcessGetPid(void) {
    return (pid_t)blocks_syscall(SYS_PROCESS_GET_PID, 0, 0, 0);
}

pid_t ProcessGetPpid(void) {
    return (pid_t)blocks_syscall(SYS_PROCESS_GET_PPID, 0, 0, 0);
}
