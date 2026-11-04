/*
 * cwd.c — getcwd / chdir（展览级命名）
 */
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <Blocks/syscall.h>

char *getcwd(char *Buffer, size_t Capacity) {
    if (!Buffer || Capacity < 2) {
        errno = EINVAL;
        return 0;
    }
    if (blocks_syscall(SYS_PATH_GETCWD, (long)Buffer, (long)Capacity, 0) < 0) {
        errno = EINVAL;
        return 0;
    }
    return Buffer;
}

int chdir(const char *Path) {
    if (!Path || Path[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    if (blocks_syscall(SYS_PATH_CHDIR, (long)Path, 0, 0) < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
