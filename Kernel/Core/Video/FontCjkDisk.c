/*
 * FontCjkDisk.c — 从 RootFs 读 CJK32.BIN（盘读 + 回退）
 *
 * 【初学者】
 * 不链进 Kernel.elf；格式见 Tools/gen-cjk32-bin.py。
 * 魔数 CJ32；码点表二分；点阵 Dim×Dim×4bpp（每字节两像素）。
 * 默认盘上 18×18（~1.2MiB，对标现网）；仍认 24/26/32 等。
 */
#include "FontCjkDisk.h"
#include "FatFile.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

#define CJK_PATH "CJK32.BIN"
#define CJK_MAX  (8u * 1024u * 1024u)
#define HDR_SIZE 32u

static UINT8 *gBlob;
static UINT32 gPages;
static UINT32 gCount;
static UINT32 gDim;
static UINT32 gGlyphBytes;
static const UINT32 *gCp;
static const UINT8 *gBits;
static int gOk;

static UINT32 Rd32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

static void DiskFree(void) {
    if (gBlob && gPages) {
        PhysicalMemoryFreePages(gBlob, gPages);
    }
    gBlob = 0;
    gPages = 0;
    gCount = 0;
    gDim = 0;
    gGlyphBytes = 0;
    gCp = 0;
    gBits = 0;
    gOk = 0;
}

int FontCjkDiskReady(void) {
    return gOk;
}

UINT32 FontCjkDiskDim(void) {
    return gOk ? gDim : 0;
}

const UINT8 *FontCjkDiskLookup(UINT32 Cp, UINT32 *OutBytes) {
    UINT32 Lo;
    UINT32 Hi;

    if (!gOk || gCp == 0 || gBits == 0) {
        return 0;
    }
    Lo = 0;
    Hi = gCount;
    while (Lo < Hi) {
        UINT32 Mid = Lo + (Hi - Lo) / 2u;
        UINT32 V = gCp[Mid];
        if (V == Cp) {
            if (OutBytes) {
                *OutBytes = gGlyphBytes;
            }
            return gBits + Mid * gGlyphBytes;
        }
        if (V < Cp) {
            Lo = Mid + 1u;
        } else {
            Hi = Mid;
        }
    }
    return 0;
}

int FontCjkDiskLoad(void) {
    UINT32 Size = 0;
    int N;
    UINT32 Need;
    UINT32 Bpp;

    DiskFree();
    gPages = (CJK_MAX + 4095u) / 4096u;
    gBlob = (UINT8 *)PhysicalMemoryAllocatePages(gPages);
    if (!gBlob) {
        gPages = 0;
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: cjk32 oom\n");
        return -1;
    }
    N = FatFileReadPath(CJK_PATH, gBlob, CJK_MAX, &Size);
    if (N < 0 || Size < HDR_SIZE) {
        DiskFree();
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: cjk32 miss\n");
        return -1;
    }
    if (gBlob[0] != 'C' || gBlob[1] != 'J' || gBlob[2] != '3' ||
        gBlob[3] != '2') {
        DiskFree();
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: cjk32 bad magic\n");
        return -1;
    }
    gCount = Rd32(gBlob + 4);
    gDim = Rd32(gBlob + 8);
    Bpp = Rd32(gBlob + 12);
    gGlyphBytes = Rd32(gBlob + 16);
    {
        UINT32 Expect = gDim * ((gDim + 1u) / 2u); /* 4bpp 行字节 */

        if (gCount == 0 || gCount > 20000u || Bpp != 4u ||
            (gDim != 18u && gDim != 20u && gDim != 24u && gDim != 26u &&
             gDim != 28u && gDim != 32u) ||
            gGlyphBytes != Expect) {
            DiskFree();
            HalSerialWriteChannel(TOY_SLOG_GUI, "Font: cjk32 bad hdr\n");
            return -1;
        }
    }
    Need = HDR_SIZE + gCount * 4u + gCount * gGlyphBytes;
    if (Size < Need) {
        DiskFree();
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: cjk32 short\n");
        return -1;
    }
    gCp = (const UINT32 *)(gBlob + HDR_SIZE);
    gBits = gBlob + HDR_SIZE + gCount * 4u;
    gOk = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Font: cjk32 disk ok\n");
    return 0;
}
