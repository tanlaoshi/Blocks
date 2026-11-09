/*
 * VolumeQuery.c — 卷表只读查询（下标 / GUID / BLOCKS.ID / 后端名）
 *
 * 自 FileSystem.c 纯搬。
 */
#include "FileSystemPrivate.h"
#include "BootHandoff.h"
#include "CString.h"

int FileSystemVolCount(void) {
    return gVolCount;
}

int FileSystemDefaultVol(void) {
    return gDefaultVol;
}

int FileSystemVolInfo(int Idx, char *Name, int NameMax, UINT32 *Drive,
                      UINT32 *StartLba, int *ReadOnly) {
    if (Idx < 0 || Idx >= gVolCount) {
        return -1;
    }
    if (Name && NameMax > 0) {
        StringCopy(Name, NameMax, gVols[Idx].Name);
    }
    if (Drive) {
        *Drive = gVols[Idx].Drive;
    }
    if (StartLba) {
        *StartLba = gVols[Idx].StartLba;
    }
    if (ReadOnly) {
        *ReadOnly = gVols[Idx].ReadOnly;
    }
    return 0;
}

int FileSystemVolDiskGuidStr(int Idx, char *Buf, int BufMax) {
    char Uuid[37];

    if (Idx < 0 || Idx >= gVolCount || !Buf || BufMax < 37) {
        return -1;
    }
    if (!gVols[Idx].DiskGuidOk) {
        return -1;
    }
    GptFormatGuidUuid(gVols[Idx].DiskGuid, Uuid);
    StringCopy(Buf, BufMax, Uuid);
    return 0;
}

int FileSystemVolOnBootDisk(int Idx) {
    const BOOT_HANDOFF *Info;
    int i;

    if (Idx < 0 || Idx >= gVolCount || !gVols[Idx].DiskGuidOk) {
        return -1;
    }
    Info = BootHandoffGet();
    if (!Info || !Info->BootDiskGuidOk) {
        return -1;
    }
    for (i = 0; i < 16; i++) {
        if (gVols[Idx].DiskGuid[i] != Info->BootDiskGuid[i]) {
            return 0;
        }
    }
    return 1;
}

int FileSystemVolHasBlocksId(int Idx) {
    if (Idx < 0 || Idx >= gVolCount) {
        return 0;
    }
    return gVols[Idx].HasBlocksId ? 1 : 0;
}

/* PR-F3：后端名（如 fat / res）；Out 可 NULL */
int FileSystemVolBackend(int Idx, const char **OutName) {
    const FS_OPS *Ops;
    if (Idx < 0 || Idx >= gVolCount) {
        return -1;
    }
    Ops = gVols[Idx].Ops ? gVols[Idx].Ops : FatFsOps();
    if (OutName) {
        *OutName = Ops->Name ? Ops->Name : "?";
    }
    return 0;
}
