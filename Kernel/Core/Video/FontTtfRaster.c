/*
 * FontTtfRaster.c — stb_truetype 栅格化单字（K27）
 *
 * 【初学者】
 * - Core/Video：FontTtfCache miss 时栅格；须在 HalFpuBegin 内（SSE）。
 * - 入口：FontTtfInit、FontTtfRasterCp。
 * - 边界：stb 实现本文件 #include；blob 来自 FontTtfLoad.c。
 */
#include "FontTtf.h"
#include "HalFpu.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)

#include <stddef.h>

/* stb 部分路径直接调 memset/memcpy；freestanding 自备 */
void *memset(void *D, int V, size_t N) {
    unsigned char *P = (unsigned char *)D;
    while (N--) {
        *P++ = (unsigned char)V;
    }
    return D;
}

void *memcpy(void *D, const void *S, size_t N) {
    unsigned char *A = (unsigned char *)D;
    const unsigned char *B = (const unsigned char *)S;
    while (N--) {
        *A++ = *B++;
    }
    return D;
}

size_t strlen(const char *S) {
    size_t N = 0;
    while (S[N]) {
        N++;
    }
    return N;
}

#define TTF_CELL 18
#define TTF_PIX  (TTF_CELL * TTF_CELL)
#define TTF_ARENA (64u * 1024u)

static unsigned char gArena[TTF_ARENA];
static UINT32 gArenaUsed;
static int gInit;

static float FontTtfSqrtf(float X) {
    float R;
    __asm__ volatile("sqrtss %1, %0" : "=x"(R) : "x"(X));
    return R;
}

static void *FontTtfMalloc(size_t N) {
    UINT32 Need = (UINT32)((N + 15u) & ~15u);
    if (gArenaUsed + Need > TTF_ARENA) {
        return 0;
    }
    {
        void *P = gArena + gArenaUsed;
        gArenaUsed += Need;
        return P;
    }
}

static float FontTtfPowf(float X, float Y) {
    (void)X; (void)Y;
    return 0.0f;
}

static float FontTtfCosf(float X) {
    (void)X;
    return 1.0f;
}

#define STBTT_ifloor(x) ((int)(x))
#define STBTT_iceil(x)  ((int)((x) + 0.999999f))
#define STBTT_sqrt(x)   FontTtfSqrtf(x)
#define STBTT_fabs(x)   ((x) < 0 ? -(x) : (x))
#define STBTT_pow(x, y) FontTtfPowf((x), (y))
#define STBTT_cos(x)    FontTtfCosf(x)
#define STBTT_acos(x)   (0.0f)
#define STBTT_fmod(x, y) ((x) - (float)((int)((x) / (y))) * (y))
#define STBTT_malloc(x, u) FontTtfMalloc(x)
#define STBTT_free(x, u)   ((void)(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_strlen(s) __builtin_strlen(s)
#define STBTT_memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define STBTT_memset(d, v, n) __builtin_memset((d), (v), (n))
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

static stbtt_fontinfo gInfo;

/*
 * FontTtfInit — stbtt_InitFont + arena
 *
 * 做什么：FontTtfBlob 校验、HalFpuBegin、InitFont。
 * 谁调用：FontInitialize（FontTtfLoad 成功后）。
 * 前后文：后 — FontTtfRasterCp / FontTtfCacheGet。
 * 返回：0 成功；-1 skip/fail。
 */
int FontTtfInit(void) {
    const UINT8 *Blob;
    UINT32 Size;
    int Ok;

    gInit = 0;
    Blob = FontTtfBlob(&Size);
    if (!Blob || Size < 12u || !HalFpuOk()) {
        HalSerialWriteChannel(SLOG_GUI, "Font: ttf init skip\n");
        return -1;
    }
    if (!HalFpuBegin()) {
        HalSerialWriteChannel(SLOG_GUI, "Font: ttf init skip\n");
        return -1;
    }
    gArenaUsed = 0;
    Ok = stbtt_InitFont(&gInfo, Blob, 0);
    HalFpuEnd();
    if (!Ok) {
        HalSerialWriteChannel(SLOG_GUI, "Font: ttf init fail\n");
        return -1;
    }
    gInit = 1;
    HalSerialWriteChannel(SLOG_GUI, "Font: ttf init ok\n");
    return 0;
}

/*
 * FontTtfRasterCp — 单码点栅格进 18×18 Pix
 *
 * 做什么：stb 缩放/居中；Cp<128 拒绝；Pix 清零后写 alpha。
 * 谁调用：FontTtfCache FillSlot。
 * 返回：0 成功；-1 未 init/FPU/无 glyph。
 */
int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix) {
    float Scale;
    int X0, Y0, X1, Y1, Gw, Gh, Ox, Oy, Y, X;
    UINT8 Tmp[TTF_PIX];
    UINT32 i;

    if (!Pix || !gInit || Cp < 128u) {
        return -1;
    }
    for (i = 0; i < TTF_PIX; i++) {
        Pix[i] = 0;
        Tmp[i] = 0;
    }
    gArenaUsed = 0;
    if (!HalFpuBegin()) {
        return -1;
    }
    /* CJK 常吃不满 em；略放大再钳进 18 格 */
    Scale = stbtt_ScaleForPixelHeight(&gInfo, (float)TTF_CELL) * 1.32f;
    stbtt_GetCodepointBitmapBox(&gInfo, (int)Cp, Scale, Scale, &X0, &Y0, &X1, &Y1);
    Gw = X1 - X0;
    Gh = Y1 - Y0;
    if (Gw <= 0 || Gh <= 0) {
        HalFpuEnd();
        return -1;
    }
    if (Gw > TTF_CELL || Gh > TTF_CELL) {
        float FitX = (float)TTF_CELL / (float)Gw;
        float FitY = (float)TTF_CELL / (float)Gh;
        float Fit = (FitX < FitY) ? FitX : FitY;
        Scale *= Fit * 0.98f;
        stbtt_GetCodepointBitmapBox(&gInfo, (int)Cp, Scale, Scale, &X0, &Y0, &X1, &Y1);
        Gw = X1 - X0;
        Gh = Y1 - Y0;
        if (Gw <= 0 || Gh <= 0 || Gw > TTF_CELL || Gh > TTF_CELL) {
            HalFpuEnd();
            return -1;
        }
    }
    Ox = (TTF_CELL - Gw) / 2;
    Oy = (TTF_CELL - Gh) / 2;
    if (Ox < 0) {
        Ox = 0;
    }
    if (Oy < 0) {
        Oy = 0;
    }
    stbtt_MakeCodepointBitmap(&gInfo, Tmp, Gw, Gh, TTF_CELL, Scale, Scale, (int)Cp);
    HalFpuEnd();
    for (Y = 0; Y < Gh; Y++) {
        for (X = 0; X < Gw; X++) {
            Pix[(Oy + Y) * TTF_CELL + (Ox + X)] = Tmp[Y * TTF_CELL + X];
        }
    }
    return 0;
}

#else

/* FontTtfInit — 非 X64 桩：TTF 不可用。 */
int FontTtfInit(void) {
    HalSerialWriteChannel(SLOG_GUI, "Font: ttf init skip\n");
    return -1;
}

/* FontTtfRasterCp — 非 X64 桩。 */
int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix) {
    (void)Cp; (void)Pix;
    return -1;
}

#endif
