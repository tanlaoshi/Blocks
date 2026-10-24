/*
 * dirent.c — DirOpen / DirRead / DirClose / FileStat（selfhost）
 */
#include <Blocks/Dir.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <Blocks/syscall.h>

struct FsDirectory {
    int Fd;
};

FS_DIR *DirOpen(const char *path) {
    FS_DIR *Dir;
    long Fd;

    Dir = (FS_DIR *)malloc(sizeof(*Dir));
    if (!Dir) {
        errno = ENOMEM;
        return 0;
    }
    Fd = blocks_open_directory(path ? path : "");
    if (Fd < 0) {
        free(Dir);
        errno = ENOTDIR;
        return 0;
    }
    Dir->Fd = (int)Fd;
    return Dir;
}

int DirRead(FS_DIR *dir, FS_DIR_ENT *out) {
    long R;

    if (!dir || !out) {
        errno = EINVAL;
        return -1;
    }
    R = blocks_read_directory(dir->Fd, out);
    if (R < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)R;
}

int DirClose(FS_DIR *dir) {
    long R;

    if (!dir) {
        errno = EINVAL;
        return -1;
    }
    R = blocks_close(dir->Fd);
    free(dir);
    if (R < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int FileStat(const char *path, FS_FILE_STAT *out) {
    long R;

    if (!out) {
        errno = EINVAL;
        return -1;
    }
    R = blocks_file_stat(path ? path : "", out);
    if (R < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
