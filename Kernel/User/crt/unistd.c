/*
 * unistd.c — File / Process / MemoryMap + brk（selfhost）
 */
#include <errno.h>
#include <signal.h>
#include <Blocks/File.h>
#include <Blocks/Process.h>
#include <sys/mman.h>
#include <Blocks/syscall.h>

int FileOpen(const char *path) {
    long r;

    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    r = blocks_open(path);
    if (r < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)r;
}

ssize_t FileRead(int fd, void *buf, size_t count) {
    long r;

    if (fd < 0 || !buf) {
        errno = EINVAL;
        return -1;
    }
    r = (long)blocks_read(fd, buf, count);
    if (r < 0) {
        int E = (int)(-r);
        errno = (E >= 1 && E < 256) ? E : EIO;
        return -1;
    }
    return (ssize_t)r;
}

ssize_t FileWrite(int fd, const void *buf, size_t count) {
    long r;

    if (fd < 0 || (!buf && count > 0)) {
        errno = EINVAL;
        return -1;
    }
    r = (long)blocks_write(fd, buf, count);
    if (r < 0) {
        int E = (int)(-r);
        errno = (E >= 1 && E < 256) ? E : EIO;
        return -1;
    }
    return (ssize_t)r;
}

int FileClose(int fd) {
    long r;

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = blocks_close(fd);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

off_t FileSeek(int fd, off_t offset, int whence) {
    long r;

    if (fd < 0 || (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)) {
        errno = EINVAL;
        return (off_t)-1;
    }
    r = blocks_lseek(fd, (long)offset, (long)whence);
    if (r < 0) {
        int E = (int)(-r);
        errno = (E >= 1 && E < 256) ? E : EINVAL;
        return (off_t)-1;
    }
    return (off_t)r;
}

int ProcessExec(const char *path, char *const argv[], char *const envp[]) {
    long r;

    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    r = blocks_execve(path, argv, envp);
    errno = (r < 0) ? ENOENT : EIO;
    return -1;
}

int ProcessPipe(int pipefd[2]) {
    long r;

    if (!pipefd) {
        errno = EINVAL;
        return -1;
    }
    r = blocks_pipe(pipefd);
    if (r < 0) {
        errno = EMFILE;
        return -1;
    }
    return 0;
}

int ProcessDup(int fd) {
    long r;

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = blocks_dup(fd);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)r;
}

long ProcessFork(void) {
    long r = blocks_fork();
    if (r < 0) {
        errno = EAGAIN;
        return -1;
    }
    return r;
}

long ProcessWait(int *status) {
    BLOCKS_RET2 r = blocks_syscall2(SYS_PROCESS_WAIT, 0, 0, 0);
    if (r.A < 0) {
        errno = ECHILD;
        return -1;
    }
    if (status) {
        *status = ((int)r.B & 0xff) << 8;
    }
    return r.A;
}

void *brk(void *addr) {
    long r = blocks_brk((long)addr);
    if (r < 0) {
        errno = ENOMEM;
        return (void *)(long)-1;
    }
    return (void *)r;
}

void *MemoryMap(void *addr, size_t length, int prot, int flags, int fd,
                off_t offset) {
    long r;
    long PackedFlags;

    if (addr != 0 || length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if ((prot & (PROT_READ | PROT_WRITE)) == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (flags & MAP_SHARED) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (flags & MAP_ANONYMOUS) {
        PackedFlags = (long)(flags & 0xffff);
        (void)fd;
        (void)offset;
    } else {
        if ((flags & MAP_PRIVATE) == 0 || fd < 0 || offset != 0) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        PackedFlags = (long)((flags & 0xffff) | (((unsigned)fd & 0xffffu) << 16));
    }

    r = blocks_mmap((long)length, (long)prot, PackedFlags);
    if (r < 0) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void *)r;
}

int MemoryUnmap(void *addr, size_t length) {
    long r;

    if (!addr || length == 0) {
        errno = EINVAL;
        return -1;
    }
    r = blocks_munmap((long)addr, (long)length);
    if (r < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int ProcessKill(pid_t pid, int sig) {
    long r;

    if (pid <= 0 || (sig != SIGKILL && sig != SIGTERM && sig != SIGINT)) {
        errno = EINVAL;
        return -1;
    }
    r = blocks_kill(pid, sig);
    if (r < 0) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}
