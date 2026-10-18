/*
 * FontTtfRaster.c — 汉字按两倍行高画，再收成实心点（须 HalFpuBegin；CFLAGS_FPU）
 */
#include "Font.h"
#include "Hal.h"
#include "ToySerialLog.h"

#if defined(__x86_64__)

#include <stddef.h>

#define TTF_CELL 32
#define TTF_HI   (TTF_CELL * 2)
#define TTF_PIX  (TTF_CELL * TTF_CELL)
#define TTF_HI_PIX (TTF_HI * TTF_HI)
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
    (void)X;
    (void)Y;
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
#include "stb_truetype.h"

static stbtt_fontinfo gInfo;

int FontTtfInit(void) {
    const UINT8 *Blob;
    UINT32 Size;
    int Ok;

    gInit = 0;
    Blob = FontTtfBlob(&Size);
    if (!Blob || Size < 12u || !HalFpuOk()) {
        ToyLogBoot("Boot: ttf init skip\n");
        return -1;
    }
    if (!HalFpuBegin()) {
        ToyLogBoot("Boot: ttf init skip\n");
        return -1;
    }
    gArenaUsed = 0;
    Ok = stbtt_InitFont(&gInfo, Blob, 0);
    HalFpuEnd();
    if (!Ok) {
        ToyLogBoot("Boot: ttf init fail\n");
        return -1;
    }
    gInit = 1;
    ToyLogBoot("Boot: ttf init ok\n");
    return 0;
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    float Scale;
    int InkX0;
    int InkY0;
    int InkX1;
    int InkY1;
    int InkW;
    int InkH;
    int X0;
    int Y0;
    int X1;
    int Y1;
    int Gw;
    int Gh;
    int Ox;
    int Oy;
    int Y;
    UINT8 Hi[TTF_HI_PIX];
    UINT32 i;

    if (!Pix18 || !gInit || Cp < 128u) {
        return -1;
    }
    for (i = 0; i < TTF_PIX; i++) {
        Pix18[i] = 0;
    }
    for (i = 0; i < TTF_HI_PIX; i++) {
        Hi[i] = 0;
    }
    gArenaUsed = 0;
    if (!HalFpuBegin()) {
        return -1;
    }
    /*
     * 按字身缩放到两倍行高，不按整格字框。屏幕上每个点看背后四格，够深就涂实。
     */
    if (!stbtt_GetCodepointBox(&gInfo, (int)Cp, &InkX0, &InkY0, &InkX1, &InkY1)) {
        HalFpuEnd();
        return -1;
    }
    InkW = InkX1 - InkX0;
    InkH = InkY1 - InkY0;
    if (InkW <= 0 || InkH <= 0) {
        HalFpuEnd();
        return -1;
    }
    Scale = (float)TTF_HI / (float)((InkW > InkH) ? InkW : InkH);
    stbtt_GetCodepointBitmapBox(&gInfo, (int)Cp, Scale, Scale, &X0, &Y0, &X1, &Y1);
    Gw = X1 - X0;
    Gh = Y1 - Y0;
    if (Gw <= 0 || Gh <= 0) {
        HalFpuEnd();
        return -1;
    }
    if (Gw > TTF_HI || Gh > TTF_HI) {
        float FitX = (float)TTF_HI / (float)Gw;
        float FitY = (float)TTF_HI / (float)Gh;
        float Fit = (FitX < FitY) ? FitX : FitY;

        Scale *= Fit;
        stbtt_GetCodepointBitmapBox(&gInfo, (int)Cp, Scale, Scale, &X0, &Y0, &X1,
                                    &Y1);
        Gw = X1 - X0;
        Gh = Y1 - Y0;
        if (Gw <= 0 || Gh <= 0 || Gw > TTF_HI || Gh > TTF_HI) {
            HalFpuEnd();
            return -1;
        }
    }
    Ox = (TTF_HI - Gw) / 2;
    Oy = (TTF_HI - Gh) / 2;
    if (Ox < 0) {
        Ox = 0;
    }
    if (Oy < 0) {
        Oy = 0;
    }
    stbtt_MakeCodepointBitmap(&gInfo, Hi + Oy * TTF_HI + Ox, Gw, Gh, TTF_HI,
                              Scale, Scale, (int)Cp);
    HalFpuEnd();
    for (Y = 0; Y < TTF_CELL; Y++) {
        int X;

        for (X = 0; X < TTF_CELL; X++) {
            int Hy = Y * 2;
            int Hx = X * 2;
            UINT32 Sum;

            Sum = Hi[Hy * TTF_HI + Hx];
            Sum += Hi[Hy * TTF_HI + Hx + 1];
            Sum += Hi[(Hy + 1) * TTF_HI + Hx];
            Sum += Hi[(Hy + 1) * TTF_HI + Hx + 1];
            Pix18[Y * TTF_CELL + X] = (Sum >= 256u) ? 255u : 0;
        }
    }
    return 0;
}

#else

int FontTtfInit(void) {
    ToyLogBoot("Boot: ttf init skip\n");
    return -1;
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    (void)Cp;
    (void)Pix18;
    return -1;
}

#endif
