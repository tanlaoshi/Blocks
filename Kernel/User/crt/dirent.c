/*
 * dirent.c — DirOpen / DirRead / DirClose / FileStat（展览级命名）
 */
#include <Blocks/Dir.h>
#include <errno.h>
#include <stdlib.h>
#include <Blocks/syscall.h>

struct FsDirectory {
    int FileDescriptor;
};

FS_DIR *DirOpen(const char *Path) {
    FS_DIR *Directory;
    long FileDescriptor;

    Directory = (FS_DIR *)malloc(sizeof(*Directory));
    if (!Directory) {
        errno = ENOMEM;
        return 0;
    }
    FileDescriptor = blocks_open_directory(Path ? Path : "");
    if (FileDescriptor < 0) {
        free(Directory);
        errno = ENOTDIR;
        return 0;
    }
    Directory->FileDescriptor = (int)FileDescriptor;
    return Directory;
}

int DirRead(FS_DIR *Directory, FS_DIR_ENT *EntryOut) {
    long Result;

    if (!Directory || !EntryOut) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_read_directory(Directory->FileDescriptor, EntryOut);
    if (Result < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)Result;
}

int DirClose(FS_DIR *Directory) {
    long Result;

    if (!Directory) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_close(Directory->FileDescriptor);
    free(Directory);
    if (Result < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int FileStat(const char *Path, FS_FILE_STAT *StatusOut) {
    long Result;

    if (!StatusOut) {
        errno = EINVAL;
        return -1;
    }
    Result = blocks_file_stat(Path ? Path : "", StatusOut);
    if (Result < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
