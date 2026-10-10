/*
 * FontTtfCache.c — TTF 字形 LRU 缓存（K27 子模块）
 *
 * 【初学者】
 * - Core/Video：FontTtfRaster 查缓存 miss 再栅格化。
 * - 入口：FontTtfCacheGet、FontTtfPreheatUtf8。
 * - 边界：不读 .ttf 文件；加载在 FontTtfLoad.c。
 */
#include "FontTtf.h"
#include "Utf8.h"

#define TTF_CACHE_N 128u
#define TTF_PROBE   8u
#define TTF_CELL    18u
#define TTF_PIX     (TTF_CELL * TTF_CELL)
#define TTF_EMPTY   0
#define TTF_HIT     1
#define TTF_MISS    2

typedef struct {
    UINT32 Cp;
    int State;
    UINT8 Pix[TTF_PIX];
} TTF_SLOT;

static TTF_SLOT gSlot[TTF_CACHE_N];

static TTF_SLOT *SlotLookup(UINT32 Cp) {
    UINT32 i0 = Cp % TTF_CACHE_N;
    UINT32 k;
    for (k = 0; k < TTF_PROBE; k++) {
        TTF_SLOT *S = &gSlot[(i0 + k) % TTF_CACHE_N];
        if (S->State != TTF_EMPTY && S->Cp == Cp) {
            return S;
        }
    }
    return 0;
}

static TTF_SLOT *SlotAlloc(UINT32 Cp) {
    UINT32 i0 = Cp % TTF_CACHE_N;
    UINT32 k;
    TTF_SLOT *Empty = 0;
    for (k = 0; k < TTF_PROBE; k++) {
        TTF_SLOT *S = &gSlot[(i0 + k) % TTF_CACHE_N];
        if (S->State != TTF_EMPTY && S->Cp == Cp) {
            return S;
        }
        if (S->State == TTF_EMPTY && !Empty) {
            Empty = S;
        }
    }
    return Empty ? Empty : &gSlot[i0];
}

static void FillSlot(UINT32 Cp) {
    TTF_SLOT *Slot;
    if (Cp < 128u) {
        return;
    }
    Slot = SlotAlloc(Cp);
    if (Slot->State != TTF_EMPTY && Slot->Cp == Cp) {
        return;
    }
    if (FontTtfRasterCp(Cp, Slot->Pix) != 0) {
        Slot->Cp = Cp;
        Slot->State = TTF_MISS;
    } else {
        Slot->Cp = Cp;
        Slot->State = TTF_HIT;
    }
}

/*
 * FontTtfCacheGet — 18×18 灰度栅格（LRU 探测）
 *
 * 做什么：命中返回 Pix；miss 调 FontTtfRasterCp 填入槽。
 * 谁调用：FontDrawCodepointAt（CJK 点阵未覆盖时）。
 * 前后文：前 — FontTtfInit；兄弟 — FontTtfPreheatUtf8。
 * 返回：TTF_CELL² 字节 alpha；失败 NULL。
 */
const UINT8 *FontTtfCacheGet(UINT32 Cp) {
    TTF_SLOT *S;
    if (Cp < 128u) {
        return 0;
    }
    S = SlotLookup(Cp);
    if (S) {
        return (S->State == TTF_HIT) ? S->Pix : 0;
    }
    /* Blocks 无 Worker：首次绘制时栅格 */
    FillSlot(Cp);
    S = SlotLookup(Cp);
    return (S && S->State == TTF_HIT) ? S->Pix : 0;
}

/*
 * FontTtfPreheatUtf8 — 启动前预热常用 UTF-8 串
 *
 * 做什么：逐码点 FillSlot；遇 \\n 或非法 UTF-8 停止。
 * 谁调用：GuiInitialize（固定中文 UI 串）。
 * 前后文：前 — FontTtfInit；减少首帧 miss 卡顿。
 */
void FontTtfPreheatUtf8(const char *S) {
    while (S && *S) {
        UINT32 Cp;
        UINTN N;
        if (*S == '\n') {
            break;
        }
        N = Utf8Decode(S, &Cp);
        if (!N) {
            break;
        }
        if (Cp >= 128u) {
            FillSlot(Cp);
        }
        S += N;
    }
}
