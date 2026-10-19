/*
 * VirtualFileSystem.c — FsOps 注册与分发（PR-F1；PR-F3 多后端）
 */
#include "VirtualFileSystem.h"

static const FS_OPS *gOps;
static const FS_OPS *gBackends[VIRTUAL_FILE_SYSTEM_MAX_BACKENDS];
static int gBackendCount;

static int Register(const FS_OPS *Ops) {
    int i;

    if (!Ops || !Ops->Mount || !Ops->ReadFile || !Ops->WriteFile) {
        return -1;
    }
    for (i = 0; i < gBackendCount; i++) {
        if (gBackends[i] == Ops) {
            if (!gOps) {
                gOps = Ops;
            }
            return 0;
        }
    }
    if (gBackendCount >= VIRTUAL_FILE_SYSTEM_MAX_BACKENDS) {
        return -1;
    }
    gBackends[gBackendCount++] = Ops;
    if (!gOps) {
        gOps = Ops;
    }
    return 0;
}

static int Select(const FS_OPS *Ops) {
    int i;

    if (!Ops) {
        return -1;
    }
    for (i = 0; i < gBackendCount; i++) {
        if (gBackends[i] == Ops) {
            gOps = Ops;
            return 0;
        }
    }
    /* 未预先 Register 时仍允许 Select（Activate 路径） */
    gOps = Ops;
    return 0;
}

static const FS_OPS *Ops(void) {
    return gOps;
}

static int NoBackend(void) {
    return FAT_ERR_IO;
}

static int Mount(UINT32 StartLba) {
    if (!gOps || !gOps->Mount) {
        return NoBackend();
    }
    return gOps->Mount(StartLba);
}

static int ListDir(const char *Path) {
    if (!gOps || !gOps->ListDir) {
        return NoBackend();
    }
    return gOps->ListDir(Path);
}

static int ListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount) {
    if (!gOps || !gOps->ListEntries) {
        return NoBackend();
    }
    return gOps->ListEntries(Path, Out, Max, OutCount);
}

static int ReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    if (!gOps || !gOps->ReadFile) {
        return NoBackend();
    }
    return gOps->ReadFile(Path, Buffer, MaxSize, OutSize);
}

static int WriteFile(const char *Path, const void *Buffer, UINTN Size) {
    if (!gOps || !gOps->WriteFile) {
        return NoBackend();
    }
    return gOps->WriteFile(Path, Buffer, Size);
}

static int DeleteFile(const char *Path) {
    if (!gOps || !gOps->DeleteFile) {
        return NoBackend();
    }
    return gOps->DeleteFile(Path);
}

static int Mkdir(const char *Path) {
    if (!gOps || !gOps->Mkdir) {
        return NoBackend();
    }
    return gOps->Mkdir(Path);
}

static int Rmdir(const char *Path) {
    if (!gOps || !gOps->Rmdir) {
        return NoBackend();
    }
    return gOps->Rmdir(Path);
}

static int Rename(const char *OldPath, const char *NewPath) {
    if (!gOps || !gOps->Rename) {
        return NoBackend();
    }
    return gOps->Rename(OldPath, NewPath);
}

static int FileStat(const char *Path, FAT_FILE_STAT *Out) {
    if (!gOps || !gOps->FileStat) {
        return NoBackend();
    }
    return gOps->FileStat(Path, Out);
}

static int FileSync(void) {
    if (!gOps || !gOps->FileSync) {
        return NoBackend();
    }
    return gOps->FileSync();
}

static int ReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN) {
    if (!gOps || !gOps->ReadFileAt) {
        return FAT_ERR_INVAL;
    }
    return gOps->ReadFileAt(Path, Offset, Buffer, Len, OutN);
}

static int WriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN) {
    if (!gOps || !gOps->WriteFileAt) {
        return FAT_ERR_INVAL;
    }
    return gOps->WriteFileAt(Path, Offset, Buffer, Len, OutN);
}

VIRTUAL_FILE_SYSTEM_OPS gVirtualFileSystem = {
    .Register = Register,
    .Select = Select,
    .Ops = Ops,
    .Mount = Mount,
    .ListDir = ListDir,
    .ListEntries = ListEntries,
    .ReadFile = ReadFile,
    .WriteFile = WriteFile,
    .DeleteFile = DeleteFile,
    .Mkdir = Mkdir,
    .Rmdir = Rmdir,
    .Rename = Rename,
    .FileStat = FileStat,
    .FileSync = FileSync,
    .ReadFileAt = ReadFileAt,
    .WriteFileAt = WriteFileAt,
};
