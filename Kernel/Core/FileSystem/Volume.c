/*
 * Volume.c — K44：挂载表、默认卷、路径前缀
 *
 * 【初学者】`ls BLOCKS:` = 解析前缀 → 激活卷 → 列根；无前缀用默认卷（BLOCKS）。
 */
#include "Volume.h"
#include "Gpt.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static VOLUME gVols[VOLUME_MAX];
static int gCount;
static int gActive;
static int gDefault;

static int StringsEqual(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void CopyName(char *Dst, const char *Src, int Cap) {
    int i;
    if (Dst == 0 || Cap <= 0) {
        return;
    }
    for (i = 0; i + 1 < Cap && Src && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

typedef struct {
    int HasBlocks;
    int HasToyos;
    int HasEfi;
} MARK_CTX;

static int OnMark(const UINT8 *Ent, void *Ctx) {
    MARK_CTX *M = (MARK_CTX *)Ctx;
    char Name[13];
    FatName83ToDisplay(Ent, Name);
    if (StringsEqual(Name, "BLOCKS.ID")) {
        M->HasBlocks = 1;
    }
    if (StringsEqual(Name, "TOYOS.ID")) {
        M->HasToyos = 1;
    }
    if (StringsEqual(Name, "EFI") && (Ent[11] & FAT_ATTR_DIR)) {
        M->HasEfi = 1;
    }
    return 0;
}

static void PickName(VOLUME *V, const MARK_CTX *M) {
    if (M->HasBlocks || M->HasToyos) {
        /* 旧盘 TOYOS.ID 也挂名为 BLOCKS，不再提供 TOYOS: 前缀 */
        CopyName(V->Name, "BLOCKS", VOLUME_NAME_MAX);
        V->ReadOnly = 0;
    } else if (V->IsEsp || M->HasEfi) {
        CopyName(V->Name, "ESP", VOLUME_NAME_MAX);
        V->ReadOnly = 1;
        V->IsEsp = 1;
    } else {
        V->Name[0] = V->Letter;
        V->Name[1] = 0;
    }
}

static int AddVol(int Drive, UINT32 PartLba, int IsEsp) {
    VOLUME *V;
    FAT_VOLUME Fat;
    MARK_CTX M;

    if (gCount >= VOLUME_MAX) {
        return -1;
    }
    if (HalBlockSelect(Drive) != 0) {
        return -1;
    }
    if (FatVolumeOpenAt(&Fat, PartLba) != 0) {
        return -1;
    }
    M.HasBlocks = 0;
    M.HasToyos = 0;
    M.HasEfi = 0;
    (void)FatVolumeWalkRoot(&Fat, OnMark, &M);

    V = &gVols[gCount];
    V->Used = 1;
    V->Drive = Drive;
    V->PartLba = PartLba;
    V->IsEsp = IsEsp;
    V->ReadOnly = IsEsp ? 1 : 0;
    V->Letter = (char)('A' + gCount);
    PickName(V, &M);
    gCount++;
    return 0;
}

int VolumeMountAll(void) {
    int Drives;
    int D;
    int i;

    gCount = 0;
    gActive = 0;
    gDefault = 0;
    for (i = 0; i < VOLUME_MAX; i++) {
        gVols[i].Used = 0;
    }
    if (!HalBlockReady() && HalBlockInit() != 0) {
        return -1;
    }
    Drives = HalBlockDriveCount();
    for (D = 0; D < Drives && gCount < VOLUME_MAX; D++) {
        GPT_FAT_PART Parts[GPT_FAT_MAX];
        int Pn = 0;
        int P;

        if (HalBlockSelect(D) != 0) {
            continue;
        }
        if (GptFindFatParts(Parts, GPT_FAT_MAX, &Pn) != 0 || Pn <= 0) {
            continue;
        }
        for (P = 0; P < Pn && gCount < VOLUME_MAX; P++) {
            (void)AddVol(D, Parts[P].StartLba, Parts[P].IsEsp);
        }
    }
    gDefault = 0;
    for (i = 0; i < gCount; i++) {
        if (StringsEqual(gVols[i].Name, "BLOCKS")) {
            gDefault = i;
            break;
        }
    }
    gActive = gDefault;
    if (gCount > 0) {
        HalSerialWriteChannel(SLOG_FS, "Fs: vols=");
        {
            char Dig[2];
            Dig[0] = (char)('0' + (gCount % 10));
            Dig[1] = 0;
            HalSerialWriteChannel(SLOG_FS, Dig);
        }
        HalSerialWriteChannel(SLOG_FS, " default=");
        HalSerialWriteChannel(SLOG_FS, gVols[gDefault].Name);
        HalSerialWriteChannel(SLOG_FS, "\n");
    }
    return gCount > 0 ? 0 : -1;
}

int VolumeCount(void) {
    return gCount;
}

const VOLUME *VolumeGet(int Index) {
    if (Index < 0 || Index >= gCount || !gVols[Index].Used) {
        return 0;
    }
    return &gVols[Index];
}

int VolumeDefaultIndex(void) {
    return gDefault;
}

int VolumeActivate(int Index) {
    if (Index < 0 || Index >= gCount) {
        return -1;
    }
    if (HalBlockSelect(gVols[Index].Drive) != 0) {
        return -1;
    }
    gActive = Index;
    return 0;
}

int VolumeOpenActive(FAT_VOLUME *V) {
    if (gCount <= 0 || gActive < 0 || gActive >= gCount) {
        return -1;
    }
    if (HalBlockSelect(gVols[gActive].Drive) != 0) {
        return -1;
    }
    return FatVolumeOpenAt(V, gVols[gActive].PartLba);
}

int VolumeActiveIndex(void) {
    return gActive;
}

static int MatchPrefix(const char *Path, const char *Name, const char **Rest) {
    int i = 0;
    while (Name[i] && Path[i]) {
        char A = Path[i];
        char B = Name[i];
        if (A >= 'a' && A <= 'z') {
            A = (char)(A - 'a' + 'A');
        }
        if (B >= 'a' && B <= 'z') {
            B = (char)(B - 'a' + 'A');
        }
        if (A != B) {
            return 0;
        }
        i++;
    }
    if (Name[i] != 0) {
        return 0;
    }
    /* 允许 "ESP" / "ESP:" / "ESP:/"；勿匹配 "ESPX" */
    if (Path[i] == 0) {
        *Rest = Path + i;
        return 1;
    }
    if (Path[i] != ':') {
        return 0;
    }
    *Rest = Path + i + 1;
    while (**Rest == '/' || **Rest == '\\') {
        (*Rest)++;
    }
    return 1;
}

int VolumeResolve(const char *Path, const char **OutPath) {
    int i;
    const char *Rest;

    if (Path == 0 || OutPath == 0) {
        return -1;
    }
    if (gCount <= 0 && VolumeMountAll() != 0) {
        return -1;
    }
    for (i = 0; i < gCount; i++) {
        if (MatchPrefix(Path, gVols[i].Name, &Rest)) {
            if (VolumeActivate(i) != 0) {
                return -1;
            }
            *OutPath = Rest;
            return 0;
        }
        /* A: / B: */
        {
            char Let[2];
            Let[0] = gVols[i].Letter;
            Let[1] = 0;
            if (MatchPrefix(Path, Let, &Rest)) {
                if (VolumeActivate(i) != 0) {
                    return -1;
                }
                *OutPath = Rest;
                return 0;
            }
        }
    }
    /* 无前缀 → 默认卷 */
    if (VolumeActivate(gDefault) != 0) {
        return -1;
    }
    *OutPath = Path;
    while (**OutPath == '/' || **OutPath == '\\') {
        (*OutPath)++;
    }
    return 0;
}
