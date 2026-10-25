/*
 * FontTtfLoad.c — 读 Assets/Fonts/CJK.TTF 与 MONO.TTF，校验 sfnt。
 * font-unified-ttf / font-latin-mono；不挂钩绘制。
 */
#include "Font.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "SerialLog.h"

#define TTF_UI_PATH   "Assets/Fonts/CJK.TTF"
#define TTF_MONO_PATH "Assets/Fonts/MONO.TTF"
#define TTF_MAX       (8u * 1024u * 1024u)

typedef struct {
    UINT8 *Blob;
    UINT32 Pages;
    UINT32 Size;
    int Ok;
} TTF_BLOB;

static TTF_BLOB gUi;
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
    LogBoot("Boot: ");
    LogBoot(Tag);
    LogBoot(" ");
    LogBoot(Why);
    if (Err != 0) {
        LogBoot(" err=");
        LogBootHex32((UINT32)(Err < 0 ? -Err : Err));
    }
    LogBoot(" path=");
    LogBoot(Path ? Path : "?");
    LogBoot("\n");
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
        LogBoot("Boot: ");
        LogBoot(Tag);
        LogBoot(" too big\n");
        return -1;
    }
    B->Pages = (St.Size + 4095u) / 4096u;
    if (B->Pages == 0) {
        B->Pages = 1;
    }
    B->Blob = (UINT8 *)PhysicalMemoryAllocatePages(B->Pages);
    if (!B->Blob) {
        B->Pages = 0;
        LogBoot("Boot: ");
        LogBoot(Tag);
        LogBoot(" oom\n");
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
        LogBoot("Boot: ");
        LogBoot(Tag);
        LogBoot(" not sfnt\n");
        return -1;
    }
    B->Size = (UINT32)Got;
    B->Ok = 1;
    Tables = Be16(B->Blob + 4);
    LogBoot("Boot: ");
    LogBoot(Tag);
    LogBoot(" sfnt ok bytes=");
    LogBootHex32(B->Size);
    LogBoot(" tables=");
    LogBootHex32((UINT32)Tables);
    LogBoot("\n");
    return 0;
}

const UINT8 *FontTtfBlob(UINT32 *OutSize) {
    if (OutSize) {
        *OutSize = gUi.Size;
    }
    return gUi.Ok ? gUi.Blob : 0;
}

const UINT8 *FontTtfMonoBlob(UINT32 *OutSize) {
    if (OutSize) {
        *OutSize = gMono.Size;
    }
    return gMono.Ok ? gMono.Blob : 0;
}

int FontTtfLoad(void) {
    int Ui;

    Ui = BlobLoad(&gUi, TTF_UI_PATH, "ttf");
    (void)BlobLoad(&gMono, TTF_MONO_PATH, "ttf-mono");
    if (Ui != 0) {
        LogBoot("Boot: ttf need BLOCKS:Assets/Fonts/CJK.TTF (host copy onto LABEL=BLOCKS)\n");
    }
    return Ui;
}
