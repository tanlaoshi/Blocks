/*
 * unistd.c — File / Process / MemoryMap / brk（全称下划线；Blocks 门面）
 */
#include <errno.h>
#include <signal.h>
#include <Blocks/File.h>
#include <Blocks/Process.h>
#include <sys/mman.h>
#include <Blocks/syscall.h>

static int errno_from_negated(long result, int fallback) {
    int code = (int)(-result);

    return (code >= 1 && code < 256) ? code : fallback;
}

int FileOpen(const char *path) {
    long result;

    if (!path || path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_open(path);
    if (result < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)result;
}

ssize_t FileRead(int file_descriptor, void *buffer, size_t byte_count) {
    long result;

    if (file_descriptor < 0 || !buffer) {
        errno = EINVAL;
        return -1;
    }
    result = (long)blocks_read(file_descriptor, buffer, byte_count);
    if (result < 0) {
        errno = errno_from_negated(result, EIO);
        return -1;
    }
    return (ssize_t)result;
}

ssize_t FileWrite(int file_descriptor, const void *buffer, size_t byte_count) {
    long result;

    if (file_descriptor < 0 || (!buffer && byte_count > 0)) {
        errno = EINVAL;
        return -1;
    }
    result = (long)blocks_write(file_descriptor, buffer, byte_count);
    if (result < 0) {
        errno = errno_from_negated(result, EIO);
        return -1;
    }
    return (ssize_t)result;
}

int FileClose(int file_descriptor) {
    long result;

    if (file_descriptor < 0) {
        errno = EBADF;
        return -1;
    }
    result = blocks_close(file_descriptor);
    if (result < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

off_t FileSeek(int file_descriptor, off_t offset, int whence) {
    long result;

    if (file_descriptor < 0 ||
        (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)) {
        errno = EINVAL;
        return (off_t)-1;
    }
    result = blocks_lseek(file_descriptor, (long)offset, (long)whence);
    if (result < 0) {
        errno = errno_from_negated(result, EINVAL);
        return (off_t)-1;
    }
    return (off_t)result;
}

int ProcessExec(const char *path, char *const argument_vector[],
                char *const environment_vector[]) {
    long result;

    if (!path || path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_execve(path, argument_vector, environment_vector);
    errno = (result < 0) ? ENOENT : EIO;
    return -1;
}

int ProcessPipe(int pipe_ends[2]) {
    long result;

    if (!pipe_ends) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_pipe(pipe_ends);
    if (result < 0) {
        errno = EMFILE;
        return -1;
    }
    return 0;
}

int ProcessDup(int file_descriptor) {
    long result;

    if (file_descriptor < 0) {
        errno = EBADF;
        return -1;
    }
    result = blocks_dup(file_descriptor);
    if (result < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)result;
}

long ProcessFork(void) {
    long result = blocks_fork();

    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return result;
}

long ProcessWait(int *status_out) {
    BLOCKS_RET2 result = blocks_syscall2(SYS_PROCESS_WAIT, 0, 0, 0);

    if (result.A < 0) {
        errno = ECHILD;
        return -1;
    }
    if (status_out) {
        *status_out = ((int)result.B & 0xff) << 8;
    }
    return result.A;
}

void *brk(void *address) {
    long result = blocks_brk((long)address);

    if (result < 0) {
        errno = ENOMEM;
        return (void *)(long)-1;
    }
    return (void *)result;
}

void *MemoryMap(void *address, size_t length, int protection, int flags,
                int file_descriptor, off_t offset) {
    long result;
    long packed_flags;

    if (address != 0 || length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if ((protection & (PROT_READ | PROT_WRITE)) == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (flags & MAP_SHARED) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (flags & MAP_ANONYMOUS) {
        packed_flags = (long)(flags & 0xffff);
        (void)file_descriptor;
        (void)offset;
    } else {
        if ((flags & MAP_PRIVATE) == 0 || file_descriptor < 0 || offset != 0) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        packed_flags = (long)((flags & 0xffff) |
                             (((unsigned)file_descriptor & 0xffffu) << 16));
    }

    result = blocks_mmap((long)length, (long)protection, packed_flags);
    if (result < 0) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void *)result;
}

int MemoryUnmap(void *address, size_t length) {
    long result;

    if (!address || length == 0) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_munmap((long)address, (long)length);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int ProcessKill(pid_t process_id, int signal) {
    long result;

    if (process_id <= 0 ||
        (signal != SIGKILL && signal != SIGTERM && signal != SIGINT)) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_kill(process_id, signal);
    if (result < 0) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}
