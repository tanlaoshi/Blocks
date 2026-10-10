/*
 * FontTtfLoad.c — K27：读根目录 CJK.TTF，校验 sfnt
 *
 * 【初学者】
 * - Core/Video：PMM 驻留 sfnt  blob；栅格见 FontTtfRaster.c。
 * - 入口：FontTtfLoad、FontTtfBlob。
 * - 边界：字体在 RootFs，不链进 Kernel.elf；失败时仍用 Terminus/CJK 点阵。
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

/*
 * FontTtfBlob — 已加载 TTF 字节视图
 *
 * 做什么：成功加载后返回 gBlob 与 gSize；否则 NULL。
 * 谁调用：FontTtfInit（FontTtfRaster.c）。
 * 返回：只读指针；OutSize 可选。
 */
const UINT8 *FontTtfBlob(UINT32 *OutSize) {
    if (OutSize) {
        *OutSize = gSize;
    }
    return gOk ? gBlob : 0;
}

/*
 * FontTtfLoad — Fat 读 CJK.TTF 进 PMM
 *
 * 做什么：AllocatePages、FatFileReadPath、校验 sfnt 魔数。
 * 谁调用：FontInitialize（X64）。
 * 前后文：后 — FontTtfInit → FontTtfCacheGet。
 * 返回：0 成功；-1 缺失/OOM/非 sfnt。
 */
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
