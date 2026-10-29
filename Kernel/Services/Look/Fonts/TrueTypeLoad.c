/*
 * FontTtfLoad.c — 读 Assets/Fonts/CJK*.TTF 与 MONO.TTF，校验 sfnt。
 * font-unified-ttf / font-latin-mono；不挂钩绘制。
 */
#include "Font.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "SerialLog.h"

/* 文件名跟字体名；旧盘 CJK.TTF / CJK2.TTF 仍可读 */
#define TTF_UI0_PATH    "Assets/Fonts/UMING.TTF"
#define TTF_UI0_LEGACY  "Assets/Fonts/CJK.TTF"
#define TTF_UI1_PATH    "Assets/Fonts/DROID.TTF"
#define TTF_UI1_LEGACY  "Assets/Fonts/CJK2.TTF"
#define TTF_MONO_PATH   "Assets/Fonts/MONO.TTF"
#define TTF_MAX         (8u * 1024u * 1024u)

typedef struct {
    UINT8 *Blob;
    UINT32 Pages;
    UINT32 Size;
    int Ok;
} TTF_BLOB;

static TTF_BLOB gUi[FONT_TTF_UI_MAX];
static TTF_BLOB gMono;

static UINT16 Be16(const UINT8 *P) {
    return (UINT16)((UINT16)P[0] << 8) | (UINT16)P[1];
}

static UINT32 Be32(const UINT8 *P) {
    return ((UINT32)P[0] << 24) | ((UINT32)P[1] << 16) | ((UINT32)P[2] << 8) |
           (UINT32)P[3];
}

static int SfntOk(UINT32 Mag) {
    if (Mag == 0x00010000u || Mag == 0x74727565u || Mag == 0x4F54544Fu) {
        return 1;
    }
    return 0;
}

static void BlobFree(TTF_BLOB *B) {
    if (B->Blob && B->Pages) {
        PhysicalMemoryFreePages(B->Blob, B->Pages);
    }
    B->Blob = 0;
    B->Pages = 0;
    B->Size = 0;
    B->Ok = 0;
}

static void LogMiss(const char *Tag, const char *Why, int Err, const char *Path) {
    LogDrv("Boot: ");
    LogDrv(Tag);
    LogDrv(" ");
    LogDrv(Why);
    if (Err != 0) {
        LogDrv(" err=");
        LogDrvHex32((UINT32)(Err < 0 ? -Err : Err));
    }
    LogDrv(" path=");
    LogDrv(Path ? Path : "?");
    LogDrv("\n");
}

/* 默认卷或显式 BLOCKS:（防 default 不是 BLOCKS 时误 miss） */
static int BlobStat(const char *Path, const char **UsedPath, FAT_FILE_STAT *St) {
    static char Prefixed[96];
    int Err;
    UINTN N;
    UINTN I;

    *UsedPath = Path;
    Err = FileSystemFileStat(Path, St);
    if (Err == FAT_OK) {
        return FAT_OK;
    }
    Prefixed[0] = 'B';
    Prefixed[1] = 'L';
    Prefixed[2] = 'O';
    Prefixed[3] = 'C';
    Prefixed[4] = 'K';
    Prefixed[5] = 'S';
    Prefixed[6] = ':';
    N = 7;
    for (I = 0; Path[I] != 0 && N + 1 < sizeof(Prefixed); I++) {
        Prefixed[N++] = Path[I];
    }
    Prefixed[N] = 0;
    *UsedPath = Prefixed;
    return FileSystemFileStat(Prefixed, St);
}

static int BlobLoad(TTF_BLOB *B, const char *Path, const char *Tag) {
    FAT_FILE_STAT St;
    const char *Used;
    UINTN Got;
    UINT32 Mag;
    UINT16 Tables;
    int Err;

    BlobFree(B);
    Got = 0;
    Used = Path;
    Err = BlobStat(Path, &Used, &St);
    if (Err != FAT_OK || (St.Attr & FAT_ATTR_DIR) || St.Size < 12u) {
        LogMiss(Tag, "miss", Err, Used);
        return -1;
    }
    if (St.Size > TTF_MAX) {
        LogDrv("Boot: ");
        LogDrv(Tag);
        LogDrv(" too big\n");
        return -1;
    }
    B->Pages = (St.Size + 4095u) / 4096u;
    if (B->Pages == 0) {
        B->Pages = 1;
    }
    B->Blob = (UINT8 *)PhysicalMemoryAllocatePages(B->Pages);
    if (!B->Blob) {
        B->Pages = 0;
        LogDrv("Boot: ");
        LogDrv(Tag);
        LogDrv(" oom\n");
        return -1;
    }
    Err = FileSystemReadFile(Used, B->Blob, St.Size, &Got);
    if (Err != FAT_OK || Got < 12) {
        BlobFree(B);
        LogMiss(Tag, "read fail", Err, Used);
        return -1;
    }
    Mag = Be32(B->Blob);
    if (!SfntOk(Mag)) {
        BlobFree(B);
        LogDrv("Boot: ");
        LogDrv(Tag);
        LogDrv(" not sfnt\n");
        return -1;
    }
    B->Size = (UINT32)Got;
    B->Ok = 1;
    Tables = Be16(B->Blob + 4);
    LogDrv("Boot: ");
    LogDrv(Tag);
    LogDrv(" sfnt ok bytes=");
    LogDrvHex32(B->Size);
    LogDrv(" tables=");
    LogDrvHex32((UINT32)Tables);
    LogDrv("\n");
    return 0;
}

const UINT8 *FontTtfUiBlob(UINT32 Slot, UINT32 *OutSize) {
    if (Slot >= FONT_TTF_UI_MAX || !gUi[Slot].Ok) {
        if (OutSize) {
            *OutSize = 0;
        }
        return 0;
    }
    if (OutSize) {
        *OutSize = gUi[Slot].Size;
    }
    return gUi[Slot].Blob;
}

UINT32 FontTtfUiCount(void) {
    UINT32 N = 0;
    UINT32 i;

    for (i = 0; i < FONT_TTF_UI_MAX; i++) {
        if (gUi[i].Ok) {
            N++;
        }
    }
    return N;
}

const UINT8 *FontTtfBlob(UINT32 *OutSize) {
    return FontTtfUiBlob(0, OutSize);
}

const UINT8 *FontTtfMonoBlob(UINT32 *OutSize) {
    if (OutSize) {
        *OutSize = gMono.Size;
    }
    return gMono.Ok ? gMono.Blob : 0;
}

static int BlobLoadFirst(TTF_BLOB *B, const char *Path, const char *Legacy,
                         const char *Tag) {
    if (BlobLoad(B, Path, Tag) == 0) {
        return 0;
    }
    if (Legacy && Legacy[0] != 0) {
        return BlobLoad(B, Legacy, Tag);
    }
    return -1;
}

int FontTtfLoad(void) {
    int Ui;

    Ui = BlobLoadFirst(&gUi[0], TTF_UI0_PATH, TTF_UI0_LEGACY, "ttf-uming");
    (void)BlobLoadFirst(&gUi[1], TTF_UI1_PATH, TTF_UI1_LEGACY, "ttf-droid");
    (void)BlobLoad(&gMono, TTF_MONO_PATH, "ttf-mono");
    if (Ui != 0) {
        LogDrv("Boot: ttf need BLOCKS:Assets/Fonts/UMING.TTF (host copy onto LABEL=BLOCKS)\n");
    }
    return Ui;
}
