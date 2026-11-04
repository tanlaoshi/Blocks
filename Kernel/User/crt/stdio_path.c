/*
 * stdio_path.c — remove / rename（SYS_FILE_UNLINK / SYS_FILE_RENAME）
 */
#include <errno.h>
#include <stdio.h>
#include <Blocks/syscall.h>

int remove(const char *path) {
    long Result;

    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_unlink(path);
    if (Result < 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int rename(const char *old_path, const char *new_path) {
    long Result;

    if (!old_path || !old_path[0] || !new_path || !new_path[0]) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_rename(old_path, new_path);
    if (Result < 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}
