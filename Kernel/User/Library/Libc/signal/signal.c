/*
 * signal.c — signal / raise / kill（全称下划线；教学信号子集）
 */
#include <errno.h>
#include <signal.h>
#include <Blocks/Process.h>
#include <Blocks/Syscall.h>

sighandler_t signal(int signal_number, sighandler_t handler) {
    long result;

    if (signal_number != SIGINT && signal_number != SIGKILL &&
        signal_number != SIGTERM) {
        errno = EINVAL;
        return SIG_ERR;
    }
    if (signal_number == SIGKILL && handler != SIG_DFL) {
        errno = EINVAL;
        return SIG_ERR;
    }
    result = blocks_signal((long)signal_number, (long)handler);
    if (result < 0) {
        errno = EINVAL;
        return SIG_ERR;
    }
    return (sighandler_t)result;
}

int kill(pid_t process_id, int signal_number) {
    return ProcessKill(process_id, signal_number);
}

int raise(int signal_number) {
    return kill(ProcessGetPid(), signal_number);
}
