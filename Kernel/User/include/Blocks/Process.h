/*
 * Blocks/Process.h — 进程 API（selfhost；非 fork/wait/execve 眼熟名）
 */
#ifndef BLOCKS_PROCESS_H
#define BLOCKS_PROCESS_H

#include <sys/types.h>

#ifndef WEXITSTATUS
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#endif

long ProcessFork(void);
long ProcessWait(int *status);
int ProcessExec(const char *path, char *const argv[], char *const envp[]);
pid_t ProcessGetPid(void);
pid_t ProcessGetPpid(void);
int ProcessKill(pid_t pid, int sig);
int ProcessPipe(int pipefd[2]);
int ProcessDup(int fd);

#endif
