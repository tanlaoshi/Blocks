/*
 * unistd.c — File / Process / MemoryMap / brk（展览级命名；Blocks 门面）
 */
#include <errno.h>
#include <signal.h>
#include <Blocks/File.h>
#include <Blocks/Process.h>
#include <sys/mman.h>
#include <Blocks/syscall.h>

static int ErrnoFromNegated(long Result, int Fallback) {
    int Code = (int)(-Result);

    return (Code >= 1 && Code < 256) ? Code : Fallback;
}

int FileOpen(const char *Path) {
    long Result;

    if (!Path || Path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_open(Path);
    if (Result < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)Result;
}

ssize_t FileRead(int FileDescriptor, void *Buffer, size_t ByteCount) {
    long Result;

    if (FileDescriptor < 0 || !Buffer) {
        errno = EINVAL;
        return -1;
    }
    Result = (long)blocks_read(FileDescriptor, Buffer, ByteCount);
    if (Result < 0) {
        errno = ErrnoFromNegated(Result, EIO);
        return -1;
    }
    return (ssize_t)Result;
}

ssize_t FileWrite(int FileDescriptor, const void *Buffer, size_t ByteCount) {
    long Result;

    if (FileDescriptor < 0 || (!Buffer && ByteCount > 0)) {
        errno = EINVAL;
        return -1;
    }
    Result = (long)blocks_write(FileDescriptor, Buffer, ByteCount);
    if (Result < 0) {
        errno = ErrnoFromNegated(Result, EIO);
        return -1;
    }
    return (ssize_t)Result;
}

int FileClose(int FileDescriptor) {
    long Result;

    if (FileDescriptor < 0) {
        errno = EBADF;
        return -1;
    }
    Result = blocks_close(FileDescriptor);
    if (Result < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

off_t FileSeek(int FileDescriptor, off_t Offset, int Whence) {
    long Result;

    if (FileDescriptor < 0 ||
        (Whence != SEEK_SET && Whence != SEEK_CUR && Whence != SEEK_END)) {
        errno = EINVAL;
        return (off_t)-1;
    }
    Result = blocks_lseek(FileDescriptor, (long)Offset, (long)Whence);
    if (Result < 0) {
        errno = ErrnoFromNegated(Result, EINVAL);
        return (off_t)-1;
    }
    return (off_t)Result;
}

int ProcessExec(const char *Path, char *const ArgumentVector[],
                char *const EnvironmentVector[]) {
    long Result;

    if (!Path || Path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_execve(Path, ArgumentVector, EnvironmentVector);
    errno = (Result < 0) ? ENOENT : EIO;
    return -1;
}

int ProcessPipe(int PipeEnds[2]) {
    long Result;

    if (!PipeEnds) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_pipe(PipeEnds);
    if (Result < 0) {
        errno = EMFILE;
        return -1;
    }
    return 0;
}

int ProcessDup(int FileDescriptor) {
    long Result;

    if (FileDescriptor < 0) {
        errno = EBADF;
        return -1;
    }
    Result = blocks_dup(FileDescriptor);
    if (Result < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)Result;
}

long ProcessFork(void) {
    long Result = blocks_fork();

    if (Result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return Result;
}

long ProcessWait(int *StatusOut) {
    BLOCKS_RET2 Result = blocks_syscall2(SYS_PROCESS_WAIT, 0, 0, 0);

    if (Result.A < 0) {
        errno = ECHILD;
        return -1;
    }
    if (StatusOut) {
        *StatusOut = ((int)Result.B & 0xff) << 8;
    }
    return Result.A;
}

void *brk(void *Address) {
    long Result = blocks_brk((long)Address);

    if (Result < 0) {
        errno = ENOMEM;
        return (void *)(long)-1;
    }
    return (void *)Result;
}

void *MemoryMap(void *Address, size_t Length, int Protection, int Flags,
                int FileDescriptor, off_t Offset) {
    long Result;
    long PackedFlags;

    if (Address != 0 || Length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if ((Protection & (PROT_READ | PROT_WRITE)) == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (Flags & MAP_SHARED) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (Flags & MAP_ANONYMOUS) {
        PackedFlags = (long)(Flags & 0xffff);
        (void)FileDescriptor;
        (void)Offset;
    } else {
        if ((Flags & MAP_PRIVATE) == 0 || FileDescriptor < 0 || Offset != 0) {
            errno = EINVAL;
            return MAP_FAILED;
        }
        PackedFlags = (long)((Flags & 0xffff) |
                             (((unsigned)FileDescriptor & 0xffffu) << 16));
    }

    Result = blocks_mmap((long)Length, (long)Protection, PackedFlags);
    if (Result < 0) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void *)Result;
}

int MemoryUnmap(void *Address, size_t Length) {
    long Result;

    if (!Address || Length == 0) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_munmap((long)Address, (long)Length);
    if (Result < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int ProcessKill(pid_t ProcessId, int Signal) {
    long Result;

    if (ProcessId <= 0 ||
        (Signal != SIGKILL && Signal != SIGTERM && Signal != SIGINT)) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_kill(ProcessId, Signal);
    if (Result < 0) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}
