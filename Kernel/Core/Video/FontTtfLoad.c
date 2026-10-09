/*
 * FontTtfLoad.c — K27：读根目录 CJK.TTF，校验 sfnt
 *
 * 【初学者】
 * 字体放 RootFs，不链进 Kernel.elf。缺文件/坏魔数 → 打日志返回，桌面仍用点阵。
 */
#include "FontTtf.h"
#include "FatFile.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define TTF_PATH "CJK.TTF"
#define TTF_MAX  (2u * 1024u * 1024u)

static UINT8 *gBlob;
static UINT32 gPages;
static UINT32 gSize;
static int gOk;

static UINT32 Be32(const UINT8 *P) {
    return ((UINT32)P[0] << 24) | ((UINT32)P[1] << 16) | ((UINT32)P[2] << 8) |
           (UINT32)P[3];
}

static int SfntOk(UINT32 Mag) {
    return Mag == 0x00010000u || Mag == 0x74727565u || Mag == 0x4F54544Fu;
}

static void TtfFree(void) {
    if (gBlob && gPages) {
        PhysicalMemoryFreePages(gBlob, gPages);
    }
    gBlob = 0;
    gPages = 0;
    gSize = 0;
    gOk = 0;
}

const UINT8 *FontTtfBlob(UINT32 *OutSize) {
    if (OutSize) {
        *OutSize = gSize;
    }
    return gOk ? gBlob : 0;
}

int FontTtfLoad(void) {
    UINT32 Size = 0;
    int N;
    UINT32 Mag;

    TtfFree();
    gPages = (TTF_MAX + 4095u) / 4096u;
    gBlob = (UINT8 *)PhysicalMemoryAllocatePages(gPages);
    if (!gBlob) {
        gPages = 0;
        HalSerialWriteChannel(SLOG_GUI, "Font: ttf oom\n");
        return -1;
    }
    N = FatFileReadPath(TTF_PATH, gBlob, TTF_MAX, &Size);
    if (N < 0 || Size < 12u) {
        TtfFree();
        HalSerialWriteChannel(SLOG_GUI, "Font: ttf miss\n");
        return -1;
    }
    Mag = Be32(gBlob);
    if (!SfntOk(Mag)) {
        TtfFree();
        HalSerialWriteChannel(SLOG_GUI, "Font: ttf not sfnt\n");
        return -1;
    }
    gSize = Size;
    gOk = 1;
    HalSerialWriteChannel(SLOG_GUI, "Font: ttf sfnt ok\n");
    return 0;
}
