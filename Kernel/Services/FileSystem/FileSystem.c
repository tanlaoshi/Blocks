/*
 * FileSystem.c — 卷激活与路径解析（PR-S-filesystem-1）
 * 路径操作：Operations.c；挂卷：Mount.c；初始化：Initialize.c
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "Gpt.h"
#include "VirtualFileSystem.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "CoreOps.h"

FS_VOLUME gVols[FS_MAX_VOLUMES];
int gVolCount;
int gDefaultVol;
int gActiveVol = -1;
UINT32 gActiveDrive = 0xFFFFFFFFu;
UINT32 gActiveLba = 0xFFFFFFFFu;
const FS_OPS *gActiveOps;

int FileSystemActivate(int VolIdx) {
    const FS_OPS *Ops;

    if (VolIdx < 0 || VolIdx >= gVolCount) {
        return FAT_ERR_INVAL;
    }
    Ops = gVols[VolIdx].Ops ? gVols[VolIdx].Ops : FatFsOps();
    if (gActiveVol == VolIdx &&
        gActiveOps == Ops &&
        gActiveDrive == gVols[VolIdx].Drive &&
        gActiveLba == gVols[VolIdx].StartLba) {
        return FAT_OK;
    }
    if (gVirtualFileSystem.Select(Ops) != 0) {
        return FAT_ERR_IO;
    }
    if (!Ops->Synthetic) {
        if (!BlockSelect(gVols[VolIdx].Drive)) {
            gActiveVol = -1;
            gActiveOps = 0;
            return FAT_ERR_IO;
        }
    }
    if (gVirtualFileSystem.Mount(gVols[VolIdx].StartLba) != FAT_OK) {
        gActiveVol = -1;
        gActiveOps = 0;
        return FAT_ERR_IO;
    }
    gActiveVol = VolIdx;
    gActiveOps = Ops;
    gActiveDrive = gVols[VolIdx].Drive;
    gActiveLba = gVols[VolIdx].StartLba;
    return FAT_OK;
}

/*
 * 解析前缀：
 *   TOYOS:path / ESP:path / A:path / B:path
 *   0:path / 1:path（卷下标）
 * 无冒号 → 默认卷，整串为相对路径。
 */
int FileSystemResolve(const char *Path, int *OutVol, const char **OutRel) {
    const char *Colon;
    char Pref[FS_VOL_NAME_MAX];
    int PrefLen;
    int i;
    int Vol;

    if (!Path || !OutVol || !OutRel) {
        return FAT_ERR_INVAL;
    }
    if (gVolCount <= 0) {
        return FAT_ERR_IO;
    }

    Colon = Path;
    while (*Colon && *Colon != ':') {
        Colon++;
    }
    if (*Colon != ':') {
        *OutVol = gDefaultVol;
        *OutRel = Path;
        return FAT_OK;
    }

    PrefLen = (int)(Colon - Path);
    if (PrefLen <= 0 || PrefLen >= FS_VOL_NAME_MAX) {
        return FAT_ERR_INVAL;
    }
    for (i = 0; i < PrefLen; i++) {
        Pref[i] = Path[i];
    }
    Pref[PrefLen] = 0;

    Vol = -1;
    if (PrefLen == 1 && Pref[0] >= '0' && Pref[0] <= '9') {
        Vol = Pref[0] - '0';
    } else if (PrefLen == 1 &&
               ((Pref[0] >= 'A' && Pref[0] <= 'Z') ||
                (Pref[0] >= 'a' && Pref[0] <= 'z'))) {
        char L = Pref[0];
        if (L >= 'a') {
            L = (char)(L - 'a' + 'A');
        }
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].Letter == L) {
                Vol = i;
                break;
            }
        }
    } else {
        for (i = 0; i < gVolCount; i++) {
            if (StrEqIgnoreCase(Pref, gVols[i].Name)) {
                Vol = i;
                break;
            }
        }
    }
    if (Vol < 0 || Vol >= gVolCount) {
        return FAT_ERR_NOENT;
    }

    *OutVol = Vol;
    *OutRel = Colon + 1;
    if (**OutRel == '/' || **OutRel == '\\') {
        (*OutRel)++;
    }
    return FAT_OK;
}

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

static int (*gPathBlocked)(const char *Path);

void FileSystemRegisterPathGuard(int (*Blocked)(const char *Path)) {
    gPathBlocked = Blocked;
}

int FileSystemPathBlocked(const char *Path) {
    if (!gPathBlocked) {
        return 0;
    }
    return gPathBlocked(Path);
}
