/*
 * stdio_path.c — remove / rename（展览级命名）
 */
#include <errno.h>
#include <stdio.h>
#include <Blocks/syscall.h>

int remove(const char *Path) {
    long Result;

    if (!Path || Path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_unlink(Path);
    if (Result < 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int rename(const char *OldPath, const char *NewPath) {
    long Result;

    if (!OldPath || OldPath[0] == 0 || !NewPath || NewPath[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_rename(OldPath, NewPath);
    if (Result < 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}
