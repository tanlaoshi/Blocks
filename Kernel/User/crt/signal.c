/*
 * signal.c — signal / raise / kill（展览级命名；教学信号子集）
 */
#include <errno.h>
#include <signal.h>
#include <Blocks/Process.h>
#include <Blocks/syscall.h>

sighandler_t signal(int SignalNumber, sighandler_t Handler) {
    long Result;

    if (SignalNumber != SIGINT && SignalNumber != SIGKILL &&
        SignalNumber != SIGTERM) {
        errno = EINVAL;
        return SIG_ERR;
    }
    if (SignalNumber == SIGKILL && Handler != SIG_DFL) {
        errno = EINVAL;
        return SIG_ERR;
    }
    Result = blocks_signal((long)SignalNumber, (long)Handler);
    if (Result < 0) {
        errno = EINVAL;
        return SIG_ERR;
    }
    return (sighandler_t)Result;
}

int kill(pid_t ProcessId, int SignalNumber) {
    return ProcessKill(ProcessId, SignalNumber);
}

int raise(int SignalNumber) {
    return kill(ProcessGetPid(), SignalNumber);
}
