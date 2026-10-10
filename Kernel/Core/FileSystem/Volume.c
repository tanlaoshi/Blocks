/*
 * Volume.c — K44：挂载表、默认卷、路径前缀
 *
 * 【初学者】
 * - 分层：Core/FileSystem；GPT 见 Gpt.c
 * - 对外：VolumeMountAll / VolumeResolve / VolumeOpenActive
 * - `ls BLOCKS:` = 解析前缀 → 激活卷；无前缀用默认 BLOCKS
 */
#include "Volume.h"
#include "Gpt.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static VOLUME gVolumes[VOLUME_MAX];
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

static void CopyName(char *Destination, const char *Source, int Capacity) {
    int Index;
    if (Destination == 0 || Capacity <= 0) {
        return;
    }
    for (Index = 0; Index + 1 < Capacity && Source && Source[Index]; Index++) {
        Destination[Index] = Source[Index];
    }
    Destination[Index] = 0;
}

typedef struct {
    int HasBlocks;
    int HasToyos;
    int HasEfi;
} MARK_CONTEXT;

static int OnMark(const UINT8 *Entry, void *Context) {
    MARK_CONTEXT *Mark = (MARK_CONTEXT *)Context;
    char Name[13];
    FatName83ToDisplay(Entry, Name);
    if (StringsEqual(Name, "BLOCKS.ID")) {
        Mark->HasBlocks = 1;
    }
    if (StringsEqual(Name, "TOYOS.ID")) {
        Mark->HasToyos = 1;
    }
    if (StringsEqual(Name, "EFI") && (Entry[11] & FAT_ATTR_DIR)) {
        Mark->HasEfi = 1;
    }
    return 0;
}

static void PickName(VOLUME *Volume, const MARK_CONTEXT *Mark) {
    if (Mark->HasBlocks || Mark->HasToyos) {
        /* 旧盘 TOYOS.ID 也挂名为 BLOCKS，不再提供 TOYOS: 前缀 */
        CopyName(Volume->Name, "BLOCKS", VOLUME_NAME_MAX);
        Volume->ReadOnly = 0;
    } else if (Volume->IsEsp || Mark->HasEfi) {
        CopyName(Volume->Name, "ESP", VOLUME_NAME_MAX);
        Volume->ReadOnly = 1;
        Volume->IsEsp = 1;
    } else {
        Volume->Name[0] = Volume->Letter;
        Volume->Name[1] = 0;
    }
}

static int AddVolume(int Drive, UINT32 PartLba, int IsEsp) {
    VOLUME *Volume;
    FAT_VOLUME Fat;
    MARK_CONTEXT Mark;

    if (gCount >= VOLUME_MAX) {
        return -1;
    }
    if (HalBlockSelect(Drive) != 0) {
        return -1;
    }
    if (FatVolumeOpenAt(&Fat, PartLba) != 0) {
        return -1;
    }
    Mark.HasBlocks = 0;
    Mark.HasToyos = 0;
    Mark.HasEfi = 0;
    (void)FatVolumeWalkRoot(&Fat, OnMark, &Mark);

    Volume = &gVolumes[gCount];
    Volume->Used = 1;
    Volume->Drive = Drive;
    Volume->PartLba = PartLba;
    Volume->IsEsp = IsEsp;
    Volume->ReadOnly = IsEsp ? 1 : 0;
    Volume->Letter = (char)('A' + gCount);
    PickName(Volume, &Mark);
    gCount++;
    return 0;
}

/*
 * VolumeMountAll — 扫所有驱动器 FAT 分区并建挂载表
 *
 * 做什么：GptFindFatParts + AddVolume；默认卷优先 BLOCKS 名。
 * 谁调用：FileSystemInitialize / VolumeResolve 懒挂载。
 * 返回：0 至少一卷；非 0 无 FAT
 */
int VolumeMountAll(void) {
    int Drives;
    int D;
    int i;

    gCount = 0;
    gActive = 0;
    gDefault = 0;
    for (i = 0; i < VOLUME_MAX; i++) {
        gVolumes[i].Used = 0;
    }
    if (!HalBlockReady() && HalBlockInitialize() != 0) {
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
            (void)AddVolume(D, Parts[P].StartLba, Parts[P].IsEsp);
        }
    }
    gDefault = 0;
    for (i = 0; i < gCount; i++) {
        if (StringsEqual(gVolumes[i].Name, "BLOCKS")) {
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
        HalSerialWriteChannel(SLOG_FS, gVolumes[gDefault].Name);
        HalSerialWriteChannel(SLOG_FS, "\n");
    }
    return gCount > 0 ? 0 : -1;
}

/* VolumeCount / VolumeGet / VolumeDefaultIndex — 查表；谁调用：Shell `vols`、Files UI */
int VolumeCount(void) {
    return gCount;
}

const VOLUME *VolumeGet(int Index) {
    if (Index < 0 || Index >= gCount || !gVolumes[Index].Used) {
        return 0;
    }
    return &gVolumes[Index];
}

int VolumeDefaultIndex(void) {
    return gDefault;
}

int VolumeActivate(int Index) {
    if (Index < 0 || Index >= gCount) {
        return -1;
    }
    if (HalBlockSelect(gVolumes[Index].Drive) != 0) {
        return -1;
    }
    gActive = Index;
    return 0;
}

int VolumeOpenActive(FAT_VOLUME *V) {
    if (gCount <= 0 || gActive < 0 || gActive >= gCount) {
        return -1;
    }
    if (HalBlockSelect(gVolumes[gActive].Drive) != 0) {
        return -1;
    }
    return FatVolumeOpenAt(V, gVolumes[gActive].PartLba);
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

/*
 * VolumeResolve — 解析 BLOCKS:/ESP:/A: 前缀并激活卷
 *
 * 做什么：MatchPrefix；HalBlockSelect；*OutPath 指向卷内相对路径。
 * 谁调用：FatFileReadPath / FatPathResolve83 / Shell 路径参数。
 * 返回：0 成功；非 0 无卷或激活失败
 */
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
        if (MatchPrefix(Path, gVolumes[i].Name, &Rest)) {
            if (VolumeActivate(i) != 0) {
                return -1;
            }
            *OutPath = Rest;
            return 0;
        }
        /* A: / B: */
        {
            char Let[2];
            Let[0] = gVolumes[i].Letter;
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
