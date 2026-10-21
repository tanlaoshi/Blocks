/*
 * FontTtfRaster.c — 汉字按当前行高栅格（须 HalFpuBegin；CFLAGS_FPU）
 *
 * gui-ttf-oversample：先 2× 直栅进 arena，再 2×2 箱滤缩到 FontTtfPixel；
 * 失败软退 1×。上沿对齐像素边界；缓存槽仍按显示边长。
 */
#include "Font.h"
#include "Hal.h"
#include "SerialLog.h"

UINT32 FontTtfPixel(void) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 H = 24u;

    /* 界面字高至少 24。换更矮的英文字体时，汉字不再跟着变小。 */
    if (F != 0 && F->Height != 0u) {
        UINT32 S = F->Scale ? F->Scale : 1u;
        UINT32 Nat = F->Height * S;

        if (Nat > H) {
            H = Nat;
        }
    }
    if (H > 32u) {
        H = 32u;
    }
    return H;
}

#if defined(__x86_64__)

#include <stddef.h>

#define TTF_CELL 32
#define TTF_PIX  (TTF_CELL * TTF_CELL)
#define TTF_OS   2u
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

/* 字形上沿是负坐标。(int) 朝零截断会把顶上将近一行丢掉。 */
static int FontTtfIfloor(float X) {
    int I = (int)X;

    if ((float)I > X) {
        I--;
    }
    return I;
}

static int FontTtfIceil(float X) {
    int I = (int)X;

    if ((float)I < X) {
        I++;
    }
    return I;
}

#define STBTT_ifloor(x) FontTtfIfloor(x)
#define STBTT_iceil(x)  FontTtfIceil(x)
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
        LogBoot("Boot: ttf init skip\n");
        return -1;
    }
    if (!HalFpuBegin()) {
        LogBoot("Boot: ttf init skip\n");
        return -1;
    }
    gArenaUsed = 0;
    Ok = stbtt_InitFont(&gInfo, Blob, 0);
    HalFpuEnd();
    if (!Ok) {
        LogBoot("Boot: ttf init fail\n");
        return -1;
    }
    gInit = 1;
    LogBoot("Boot: ttf init ok\n");
    return 0;
}

int FontTtfReady(void) {
    return gInit ? 1 : 0;
}

/* 把字形上沿挪到像素边界。半像素上的第一行灰度很浅，看起来像少了一笔。 */
static float FontTtfTopShift(int Glyph, float Scale) {
    int X0;
    int Y0;
    int X1;
    int Y1;
    float Top;
    int TopI;
    float Shift;

    if (!stbtt_GetGlyphBox(&gInfo, Glyph, &X0, &Y0, &X1, &Y1)) {
        return 0.0f;
    }
    (void)X0;
    (void)X1;
    (void)Y0;
    Top = -(float)Y1 * Scale;
    TopI = FontTtfIfloor(Top);
    Shift = -(Top - (float)TopI);
    if (Shift > -0.001f) {
        Shift = 0.0f;
    }
    return Shift;
}

/* 在 Cell 边长上算 Scale/框/居中；成功填 *Out*，失败 -1（已在 FPU 岛内）。 */
static int FontTtfLayout(UINT32 Cp, int Glyph, UINT32 Cell, float *OutScale,
                         float *OutShift, int *OutGw, int *OutGh, int *OutOx,
                         int *OutOy) {
    float Scale;
    float Shift;
    int X0;
    int Y0;
    int X1;
    int Y1;
    int Gw;
    int Gh;
    int Ox;
    int Oy;

    Scale = stbtt_ScaleForMappingEmToPixels(&gInfo, (float)Cell);
    Shift = FontTtfTopShift(Glyph, Scale);
    stbtt_GetCodepointBitmapBoxSubpixel(&gInfo, (int)Cp, Scale, Scale, 0.0f, Shift,
                                        &X0, &Y0, &X1, &Y1);
    Gw = X1 - X0;
    Gh = Y1 - Y0;
    if (Gw <= 0 || Gh <= 0) {
        return -1;
    }
    if (Gw > (int)Cell || Gh > (int)Cell) {
        float FitX = (float)Cell / (float)Gw;
        float FitY = (float)Cell / (float)Gh;
        float Fit = (FitX < FitY) ? FitX : FitY;

        Scale *= Fit;
        Shift = FontTtfTopShift(Glyph, Scale);
        stbtt_GetCodepointBitmapBoxSubpixel(&gInfo, (int)Cp, Scale, Scale, 0.0f,
                                            Shift, &X0, &Y0, &X1, &Y1);
        Gw = X1 - X0;
        Gh = Y1 - Y0;
        if (Gw <= 0 || Gh <= 0 || Gw > (int)Cell || Gh > (int)Cell) {
            return -1;
        }
    }
    Ox = ((int)Cell - Gw) / 2;
    Oy = ((int)Cell - Gh) / 2;
    if (Ox < 0) {
        Ox = 0;
    }
    if (Oy < 0) {
        Oy = 0;
    }
    if (Ox + Gw > (int)Cell) {
        Gw = (int)Cell - Ox;
    }
    if (Oy + Gh > (int)Cell) {
        Gh = (int)Cell - Oy;
    }
    if (Gw <= 0 || Gh <= 0) {
        return -1;
    }
    *OutScale = Scale;
    *OutShift = Shift;
    *OutGw = Gw;
    *OutGh = Gh;
    *OutOx = Ox;
    *OutOy = Oy;
    return 0;
}

static void FontTtfDownsample2(const UINT8 *Hi, UINT32 OsSide, UINT8 *Lo,
                               UINT32 Side) {
    UINT32 Y;
    UINT32 X;

    for (Y = 0; Y < Side; Y++) {
        for (X = 0; X < Side; X++) {
            UINT32 S = 0;
            UINT32 R0 = (Y * TTF_OS) * OsSide + (X * TTF_OS);

            S += Hi[R0];
            S += Hi[R0 + 1u];
            S += Hi[R0 + OsSide];
            S += Hi[R0 + OsSide + 1u];
            Lo[Y * Side + X] = (UINT8)((S + 2u) / 4u);
        }
    }
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    float Scale;
    float Shift;
    int Glyph;
    int Gw;
    int Gh;
    int Ox;
    int Oy;
    UINT32 Side;
    UINT32 OsSide;
    UINT32 i;
    UINT8 *Hi;
    UINT32 HiBytes;

    /* font-unified-ttf：可打印 ASCII 与汉字同源栅格；控制符仍拒 */
    if (!Pix18 || !gInit || Cp < 32u || Cp == 127u) {
        return -1;
    }
    Side = FontTtfPixel();
    for (i = 0; i < TTF_PIX; i++) {
        Pix18[i] = 0;
    }
    gArenaUsed = 0;
    if (!HalFpuBegin()) {
        return -1;
    }
    Glyph = stbtt_FindGlyphIndex(&gInfo, (int)Cp);

    /* 2× 过采样：大格直栅 → 箱滤；暂存不够或布局失败 → 1× */
    OsSide = Side * TTF_OS;
    HiBytes = OsSide * OsSide;
    Hi = (UINT8 *)FontTtfMalloc(HiBytes);
    if (Hi != 0 &&
        FontTtfLayout(Cp, Glyph, OsSide, &Scale, &Shift, &Gw, &Gh, &Ox, &Oy) ==
            0) {
        STBTT_memset(Hi, 0, HiBytes);
        stbtt_MakeCodepointBitmapSubpixel(&gInfo, Hi + Oy * (int)OsSide + Ox, Gw,
                                          Gh, (int)OsSide, Scale, Scale, 0.0f,
                                          Shift, (int)Cp);
        FontTtfDownsample2(Hi, OsSide, Pix18, Side);
        HalFpuEnd();
        return 0;
    }

    gArenaUsed = 0;
    if (FontTtfLayout(Cp, Glyph, Side, &Scale, &Shift, &Gw, &Gh, &Ox, &Oy) !=
        0) {
        HalFpuEnd();
        return -1;
    }
    stbtt_MakeCodepointBitmapSubpixel(&gInfo, Pix18 + Oy * (int)Side + Ox, Gw, Gh,
                                      (int)Side, Scale, Scale, 0.0f, Shift,
                                      (int)Cp);
    HalFpuEnd();
    return 0;
}

#else

int FontTtfInit(void) {
    LogBoot("Boot: ttf init skip\n");
    return -1;
}

int FontTtfReady(void) {
    return 0;
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    (void)Cp;
    (void)Pix18;
    return -1;
}

#endif
