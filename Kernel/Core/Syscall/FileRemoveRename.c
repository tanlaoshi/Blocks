/*
 * FileRemoveRename.c — SYS_FILE_UNLINK / SYS_FILE_RENAME
 *
 * 经 VfsService*（Core 不直调 Services/FileSystem）。
 * unlink：先删文件；若路径为目录则尝试空目录 RemoveDirectory。
 */
#include "SyscallPrivate.h"
#include "CoreOps.h"
#include "Fat.h"
#include "Scheduler.h"

int SysFileUnlink(UINT64 UserPath) {
    char Path[PATH_MAX_LEN + 1];
    TASK *T = SchedulerCurrent();
    int Error;

    if (!T || !T->IsUser) {
        return -1;
    }
    if (CopyUserPath(Path, UserPath) != 0) {
        return -1;
    }
    if (CwdResolve(T, Path, (int)sizeof(Path)) != 0) {
        return -1;
    }
    Error = VfsServiceDeleteFile(Path);
    if (Error == FAT_ERR_ISDIR) {
        Error = VfsServiceRemoveDirectory(Path);
    }
    return (Error == FAT_OK) ? 0 : -1;
}

int SysFileRename(UINT64 UserOldPath, UINT64 UserNewPath) {
    char OldPath[PATH_MAX_LEN + 1];
    char NewPath[PATH_MAX_LEN + 1];
    TASK *T = SchedulerCurrent();
    int Error;

    if (!T || !T->IsUser) {
        return -1;
    }
    if (CopyUserPath(OldPath, UserOldPath) != 0) {
        return -1;
    }
    if (CopyUserPath(NewPath, UserNewPath) != 0) {
        return -1;
    }
    if (CwdResolve(T, OldPath, (int)sizeof(OldPath)) != 0) {
        return -1;
    }
    if (CwdResolve(T, NewPath, (int)sizeof(NewPath)) != 0) {
        return -1;
    }
    Error = VfsServiceRename(OldPath, NewPath);
    return (Error == FAT_OK) ? 0 : -1;
}
