/*
 * dirent.c — DirOpen / DirRead / DirClose / FileStat（全称下划线）
 */
#include <Blocks/Dir.h>
#include <errno.h>
#include <stdlib.h>
#include <Blocks/Syscall.h>

struct FsDirectory {
    int file_descriptor;
};

FS_DIR *DirOpen(const char *path) {
    FS_DIR *directory;
    long file_descriptor;

    directory = (FS_DIR *)malloc(sizeof(*directory));
    if (!directory) {
        errno = ENOMEM;
        return 0;
    }
    file_descriptor = blocks_open_directory(path ? path : "");
    if (file_descriptor < 0) {
        free(directory);
        errno = ENOTDIR;
        return 0;
    }
    directory->file_descriptor = (int)file_descriptor;
    return directory;
}

int DirRead(FS_DIR *directory, FS_DIR_ENT *entry_out) {
    long result;

    if (!directory || !entry_out) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_read_directory(directory->file_descriptor, entry_out);
    if (result < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)result;
}

int DirClose(FS_DIR *directory) {
    long result;

    if (!directory) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_close(directory->file_descriptor);
    free(directory);
    if (result < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int FileStat(const char *path, FS_FILE_STAT *status_out) {
    long result;

    if (!status_out) {
        errno = EINVAL;
        return -1;
    }
    result = blocks_file_stat(path ? path : "", status_out);
    if (result < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
