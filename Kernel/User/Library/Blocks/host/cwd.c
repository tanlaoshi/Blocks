/*
 * cwd.c — getcwd / chdir（全称下划线）
 */
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <Blocks/syscall.h>

char *getcwd(char *buffer, size_t capacity) {
    if (!buffer || capacity < 2) {
        errno = EINVAL;
        return 0;
    }
    if (blocks_syscall(SYS_PATH_GETCWD, (long)buffer, (long)capacity, 0) < 0) {
        errno = EINVAL;
        return 0;
    }
    return buffer;
}

int chdir(const char *path) {
    if (!path || path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    if (blocks_syscall(SYS_PATH_CHDIR, (long)path, 0, 0) < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
