/*
 * stdio_path.c — remove / rename（全称下划线）
 */
#include <errno.h>
#include <stdio.h>
#include <Blocks/syscall.h>

int remove(const char *path) {
    long result;

    if (!path || path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_unlink(path);
    if (result < 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int rename(const char *old_path, const char *new_path) {
    long result;

    if (!old_path || old_path[0] == 0 || !new_path || new_path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_rename(old_path, new_path);
    if (result < 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}
