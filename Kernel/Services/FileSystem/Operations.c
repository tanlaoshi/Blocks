/*
 * Operations.c — 路径上的读写与目录操作（PR-S-filesystem-1）
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "Gpt.h"
#include "VirtualFileSystem.h"
#include "Debug.h"
#include "SerialLog.h"
#include "CoreOps.h"

static int FileSystemPreparePath(const char *Path, const char **RelOut, int NeedWrite) {
    int Vol;
    const char *Rel;
    int Err;

    Err = FileSystemResolve(Path, &Vol, &Rel);
    if (Err != FAT_OK) {
        return Err;
    }
    if (NeedWrite && gVols[Vol].ReadOnly && !FsVolumeIsSelected(Vol)) {
        return FAT_ERR_ROFS;
    }
    Err = FileSystemActivate(Vol);
    if (Err != FAT_OK) {
        return Err;
    }
    if (RelOut) {
        *RelOut = Rel;
    }
    return FAT_OK;
}

static int ImplFileSystemListDirectory(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return gVirtualFileSystem.ListDir(Rel && Rel[0] ? Rel : 0);
}

static int ImplFileSystemListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return gVirtualFileSystem.ListEntries(Rel && Rel[0] ? Rel : 0, Out, Max, OutCount);
}

static int ImplFileSystemReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    const char *Rel;
    int Err;

    if (!Path) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(Path, &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.ReadFile(Rel, Buffer, MaxSize, OutSize);
}

static int ImplFileSystemWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.WriteFile(Rel, Buffer, Size);
}

static int ImplFileSystemReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN) {
    const char *Rel;
    int Err;

    if (!Path) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(Path, &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.ReadFileAt(Rel, Offset, Buffer, Len, OutN);
}

static int ImplFileSystemWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.WriteFileAt(Rel, Offset, Buffer, Len, OutN);
}

static int ImplFileSystemDeleteFile(const char *Path) {
    const char *Rel;
    int Err;

    if (FileSystemPathBlocked(Path)) {
        return FAT_ERR_STORE;
    }
    Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.DeleteFile(Rel);
}

static int ImplFileSystemMakeDirectory(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.Mkdir(Rel);
}

static int ImplFileSystemRemoveDirectory(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.Rmdir(Rel);
}

static int ImplFileSystemRename(const char *OldPath, const char *NewPath) {
    const char *OldRel;
    const char *NewRel;
    int OldVol;
    int NewVol;
    int Err;

    if (!OldPath || !NewPath) {
        return FAT_ERR_INVAL;
    }
    if (FileSystemPathBlocked(OldPath) || FileSystemPathBlocked(NewPath)) {
        return FAT_ERR_STORE;
    }
    Err = FileSystemResolve(OldPath, &OldVol, &OldRel);
    if (Err != FAT_OK) {
        return Err;
    }
    Err = FileSystemResolve(NewPath, &NewVol, &NewRel);
    if (Err != FAT_OK) {
        return Err;
    }
    if (OldVol != NewVol) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(OldPath, &OldRel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    /* 同卷已 Activate；NewRel 相对路径 */
    Err = FileSystemResolve(NewPath, &NewVol, &NewRel);
    if (Err != FAT_OK) {
        return Err;
    }
    return gVirtualFileSystem.Rename(OldRel, NewRel);
}

static int ImplFileSystemFileStat(const char *Path, FAT_FILE_STAT *Out) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return gVirtualFileSystem.FileStat(Rel && Rel[0] ? Rel : 0, Out);
}

static int ImplFileSystemFileSync(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    (void)Rel;
    return gVirtualFileSystem.FileSync();
}

static int ImplFileSystemDirStress(const char *Path, int MaxFiles, int *OutCreated, int *OutGrew) {
    const char *Rel;
    const FS_OPS *Ops;
    int Err;

    if (!Path || !Path[0]) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    Ops = gVirtualFileSystem.Ops();
    if (!Ops || Ops->Synthetic) {
        return FAT_ERR_ROFS;
    }
    return FatDirStress(Rel && Rel[0] ? Rel : Path, MaxFiles, OutCreated, OutGrew);
}

/* —— 公开入口：统一加 FS 锁（PR-F5） —— */

int FileSystemListDirectory(const char *Path) {
    int R;
    FileSystemLock();
    R = ImplFileSystemListDirectory(Path);
    FileSystemUnlock();
    return R;
}

int FileSystemListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount) {
    int R;
    FileSystemLock();
    R = ImplFileSystemListEntries(Path, Out, Max, OutCount);
    FileSystemUnlock();
    return R;
}

int FileSystemReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    int R;
    FileSystemLock();
    R = ImplFileSystemReadFile(Path, Buffer, MaxSize, OutSize);
    FileSystemUnlock();
    return R;
}

int FileSystemWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    int R;
    FileSystemLock();
    R = ImplFileSystemWriteFile(Path, Buffer, Size);
    FileSystemUnlock();
    return R;
}

int FileSystemReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN) {
    int R;
    FileSystemLock();
    R = ImplFileSystemReadFileAt(Path, Offset, Buffer, Len, OutN);
    FileSystemUnlock();
    return R;
}

int FileSystemWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN) {
    int R;
    FileSystemLock();
    R = ImplFileSystemWriteFileAt(Path, Offset, Buffer, Len, OutN);
    FileSystemUnlock();
    return R;
}

int FileSystemDeleteFile(const char *Path) {
    int R;
    FileSystemLock();
    R = ImplFileSystemDeleteFile(Path);
    FileSystemUnlock();
    return R;
}

int FileSystemMakeDirectory(const char *Path) {
    int R;
    FileSystemLock();
    R = ImplFileSystemMakeDirectory(Path);
    FileSystemUnlock();
    return R;
}

int FileSystemRemoveDirectory(const char *Path) {
    int R;
    FileSystemLock();
    R = ImplFileSystemRemoveDirectory(Path);
    FileSystemUnlock();
    return R;
}

int FileSystemRename(const char *OldPath, const char *NewPath) {
    int R;
    FileSystemLock();
    R = ImplFileSystemRename(OldPath, NewPath);
    FileSystemUnlock();
    return R;
}

int FileSystemFileStat(const char *Path, FAT_FILE_STAT *Out) {
    int R;
    FileSystemLock();
    R = ImplFileSystemFileStat(Path, Out);
    FileSystemUnlock();
    return R;
}

int FileSystemFileSync(const char *Path) {
    int R;
    FileSystemLock();
    R = ImplFileSystemFileSync(Path);
    FileSystemUnlock();
    return R;
}

int FileSystemDirStress(const char *Path, int MaxFiles, int *OutCreated, int *OutGrew) {
    int R;
    FileSystemLock();
    R = ImplFileSystemDirStress(Path, MaxFiles, OutCreated, OutGrew);
    FileSystemUnlock();
    return R;
}

