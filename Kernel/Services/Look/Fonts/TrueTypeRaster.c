/*
 * FontTtfRaster.c — 汉字按当前行高栅格（须 HalFpuBegin；CFLAGS_FPU）
 *
 * gui-ttf-oversample：先 2× 直栅进 arena，再 2×2 箱滤缩到 FontTtfCell；
 * 失败软退 1×。上沿对齐像素边界；缓存槽按物理边长（PR-23b）。
 */
#include "Font.h"
#include "Hal.h"
#include "SerialLog.h"
#include "CString.h"

UINT32 FontTtfPixel(void) {
    const FONT_FACE *F;
    UINT32 H = 24u;

    /*
     * cjk-4：用 Latin 点阵 face 量高。UI TTF 项（UMing）Glyphs=0，
     * 若跟 FontGetCurrent 会在切 UMing 时改 Side、整表 miss → 中文 defer 全空。
     */
    F = FontGetCurrent();
    if (F == 0 || F->Glyphs == 0 || F->BytesPerGlyph == 0) {
        F = &gFontFaceTerminus10x18;
    }
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

/*
 * PR-23b：栅格边长必须是物理像素。200% 时逻辑 24px 的汉字占 48 物理像素，
 * 若仍按 24 栅格再由绘制层放大，物理分辨率就白丢了。
 * FontTtfPixel 保持逻辑值（布局/行高用它），只有栅格/缓存用本函数。
 */
UINT32 FontTtfCell(void) {
    UINT32 S = HalVideoGetDrawScale();

    if (S < 1u) {
        S = 1u;
    }
    return FontTtfPixel() * S;
}

#if defined(__x86_64__) || defined(__aarch64__)

#include <stddef.h>

#define TTF_CELL 64
#define TTF_PIX  (TTF_CELL * TTF_CELL)
#define TTF_OS   2u
#define TTF_ARENA (64u * 1024u)

static unsigned char gArena[TTF_ARENA];
static UINT32 gArenaUsed;
static int gInit;

static float FontTtfSqrtf(float X) {
    float R;
#if defined(__x86_64__)
    __asm__ volatile("sqrtss %1, %0" : "=x"(R) : "x"(X));
#else
    __asm__ volatile("fsqrt %s0, %s1" : "=w"(R) : "w"(X));
#endif
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
#define STBTT_strlen(s) strlen(s)
#define STBTT_memcpy(d, s, n) memcpy((d), (s), (n))
#define STBTT_memset(d, v, n) memset((d), (v), (n))
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static stbtt_fontinfo gInfo[FONT_TTF_UI_MAX];
static UINT32 gInitMask;
static stbtt_fontinfo gMonoInfo;
static int gMonoInit;
static int gMonoNest;
static UINT32 gMonoAdv;

int FontTtfInit(void) {
    const UINT8 *Blob;
    UINT32 Size;
    UINT32 Slot;
    int Ok;
    int Any = 0;

    gInit = 0;
    gInitMask = 0;
    if (!HalFpuOk()) {
        LogDrv("Boot: ttf init skip\n");
        return -1;
    }
    if (!HalFpuBegin()) {
        LogDrv("Boot: ttf init skip\n");
        return -1;
    }
    for (Slot = 0; Slot < FONT_TTF_UI_MAX; Slot++) {
        Size = 0;
        Blob = FontTtfUiBlob(Slot, &Size);
        if (!Blob || Size < 12u) {
            continue;
        }
        gArenaUsed = 0;
        Ok = stbtt_InitFont(&gInfo[Slot], Blob, 0);
        if (Ok) {
            gInitMask |= (1u << Slot);
            Any = 1;
        }
    }
    HalFpuEnd();
    if (!Any) {
        LogDrv("Boot: ttf init fail\n");
        return -1;
    }
    gInit = 1;
    /* SESSION 早预热可能灌满 MISS；清掉才能在就绪后再栅 */
    FontTtfCacheDropMisses();
    LogDrv("Boot: ttf init ok slots=");
    LogDrvHex32(gInitMask);
    LogDrv("\n");
    return 0;
}

int FontTtfMonoInit(void) {
    const UINT8 *Blob;
    UINT32 Size;
    int Ok;
    float Scale;
    int Adv;
    int Lsb;

    gMonoInit = 0;
    gMonoAdv = 0;
    Blob = FontTtfMonoBlob(&Size);
    if (!Blob || Size < 12u || !HalFpuOk() || !gInit) {
        LogDrv("Boot: ttf-mono init skip\n");
        return -1;
    }
    if (!HalFpuBegin()) {
        LogDrv("Boot: ttf-mono init skip\n");
        return -1;
    }
    gArenaUsed = 0;
    Ok = stbtt_InitFont(&gMonoInfo, Blob, 0);
    if (Ok) {
        Scale = stbtt_ScaleForMappingEmToPixels(&gMonoInfo, (float)FontTtfPixel());
        stbtt_GetCodepointHMetrics(&gMonoInfo, (int)'M', &Adv, &Lsb);
        (void)Lsb;
        if (Adv > 0) {
            gMonoAdv = (UINT32)(Adv * Scale + 0.5f);
        }
        if (gMonoAdv < 1u) {
            gMonoAdv = FontTtfPixel() / 2u;
        }
        if (gMonoAdv < 1u) {
            gMonoAdv = 1u;
        }
    }
    HalFpuEnd();
    if (!Ok) {
        LogDrv("Boot: ttf-mono init fail\n");
        return -1;
    }
    gMonoInit = 1;
    LogDrv("Boot: ttf-mono init ok\n");
    return 0;
}

int FontTtfReady(void) {
    return gInit ? 1 : 0;
}

int FontTtfMonoReady(void) {
    return gMonoInit ? 1 : 0;
}

void FontTtfMonoBegin(void) {
    if (gMonoInit) {
        gMonoNest++;
    }
}

void FontTtfMonoEnd(void) {
    if (gMonoNest > 0) {
        gMonoNest--;
    }
}

int FontTtfMonoActive(void) {
    return (gMonoInit && gMonoNest > 0) ? 1 : 0;
}

UINT32 FontTtfMonoAdvance(void) {
    return gMonoAdv;
}

static stbtt_fontinfo *FontTtfCur(void) {
    UINT32 Slot;

    if (FontTtfMonoActive()) {
        return &gMonoInfo;
    }
    Slot = FontTtfUiActiveSlot();
    if (Slot >= FONT_TTF_UI_MAX || (gInitMask & (1u << Slot)) == 0u) {
        Slot = 0;
    }
    if ((gInitMask & (1u << Slot)) == 0u) {
        /* 兜底：任一已 Init 槽 */
        for (Slot = 0; Slot < FONT_TTF_UI_MAX; Slot++) {
            if (gInitMask & (1u << Slot)) {
                break;
            }
        }
    }
    return &gInfo[Slot < FONT_TTF_UI_MAX ? Slot : 0];
}

/* 把字形上沿挪到像素边界。半像素上的第一行灰度很浅，看起来像少了一笔。 */
static float FontTtfTopShift(stbtt_fontinfo *Info, int Glyph, float Scale) {
    int X0;
    int Y0;
    int X1;
    int Y1;
    float Top;
    int TopI;
    float Shift;

    if (!stbtt_GetGlyphBox(Info, Glyph, &X0, &Y0, &X1, &Y1)) {
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

/*
 * UI stb 布局：MappingEm + TopShift + 格内居中（过采样前后均用）。
 * 曾改 VMetrics 基线导致大量栅格失败→MISS→汉字点阵、拉丁等 TTF 空白/框。
 */
static int FontTtfLayout(stbtt_fontinfo *Info, UINT32 Cp, int Glyph, UINT32 CellW,
                         UINT32 CellH, float *OutScale, float *OutShift, int *OutGw,
                         int *OutGh, int *OutOx, int *OutOy) {
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

    if (CellW == 0 || CellH == 0) {
        return -1;
    }
    /*
     * 与 FT 拉丁光学一致：ASCII 用整数加档 em→px（+1/4 钳 32）再装入格；
     * 汉字仍 MappingEm=CellH。禁止非整数位图拉伸。
     */
    {
        UINT32 EmPx = CellH;
        /*
         * PR-23b：上限随绘制倍数走（100%→32，200%→64）。
         * 过采样 2× 路径传进来的 CellH 也是物理值；若恒用 64，100% 下
         * ASCII 会从 48 突增到 60，整屏拉丁字变胖，基线不再逐像素相同。
         */
        UINT32 EmCap = 32u * HalVideoGetDrawScale();

        if (Cp < 128u) {
            EmPx = CellH + (CellH / 4u);
            if (EmPx > EmCap) {
                EmPx = EmCap;
            }
            if (EmPx < CellH) {
                EmPx = CellH;
            }
        }
        Scale = stbtt_ScaleForMappingEmToPixels(Info, (float)EmPx);
    }
    Shift = FontTtfTopShift(Info, Glyph, Scale);
    stbtt_GetCodepointBitmapBoxSubpixel(Info, (int)Cp, Scale, Scale, 0.0f, Shift,
                                        &X0, &Y0, &X1, &Y1);
    Gw = X1 - X0;
    Gh = Y1 - Y0;
    if (Gw <= 0 || Gh <= 0) {
        return -1;
    }
    /*
     * 汉字超格：按比例 Fit 进格（整数格内重算 Scale）。
     * 拉丁加档后可能略超格：勿 Fit 缩回（会抵消光学加档），平移裁切装入。
     */
    if (Cp >= 128u && (Gw > (int)CellW || Gh > (int)CellH)) {
        float FitX = (float)CellW / (float)Gw;
        float FitY = (float)CellH / (float)Gh;
        float Fit = (FitX < FitY) ? FitX : FitY;

        Scale *= Fit;
        Shift = FontTtfTopShift(Info, Glyph, Scale);
        stbtt_GetCodepointBitmapBoxSubpixel(Info, (int)Cp, Scale, Scale, 0.0f,
                                            Shift, &X0, &Y0, &X1, &Y1);
        Gw = X1 - X0;
        Gh = Y1 - Y0;
        if (Gw <= 0 || Gh <= 0 || Gw > (int)CellW || Gh > (int)CellH) {
            return -1;
        }
    }
    /*
     * 与 FT 一致：CellH 收进基线（ascent+descent 超格则上移留底），
     * 加档拉丁不贴边乱移；此处夹进格以免 MakeBitmap 越界。
     */
    {
        int Ascent;
        int Descent;
        int LineGap;
        int Baseline;
        int DescPx;
        float ScaleCell;

        ScaleCell = stbtt_ScaleForMappingEmToPixels(Info, (float)CellH);
        stbtt_GetFontVMetrics(Info, &Ascent, &Descent, &LineGap);
        (void)LineGap;
        Baseline = (int)((float)Ascent * ScaleCell);
        DescPx = (int)(-(float)Descent * ScaleCell);
        if (DescPx < 2) {
            DescPx = 2;
        }
        if (Baseline + DescPx > (int)CellH) {
            Baseline = (int)CellH - DescPx;
        }
        if (Baseline < 1) {
            Baseline = (int)CellH * 3 / 4;
        }
        if (Baseline >= (int)CellH) {
            Baseline = (int)CellH - 1;
        }
        stbtt_GetCodepointBitmapBoxSubpixel(Info, (int)Cp, Scale, Scale, 0.0f,
                                            Shift, &X0, &Y0, &X1, &Y1);
        Gw = X1 - X0;
        Gh = Y1 - Y0;
        if (Gw <= 0 || Gh <= 0) {
            return -1;
        }
        Ox = (Cp < 128u) ? 0 : (((int)CellW - Gw) / 2);
        Oy = Baseline + Y0;
        if (Cp >= 128u && Gh <= (int)CellH && Oy + Gh > (int)CellH) {
            Oy = (int)CellH - Gh;
        }
    }
    if (Ox < 0) {
        Ox = 0;
    }
    if (Oy < 0) {
        Oy = 0;
    }
    if (Ox + Gw > (int)CellW) {
        Gw = (int)CellW - Ox;
    }
    if (Oy + Gh > (int)CellH) {
        Gh = (int)CellH - Oy;
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

static void FontTtfDownsample2(const UINT8 *Hi, UINT32 OsW, UINT32 OsH, UINT8 *Lo,
                               UINT32 W, UINT32 H) {
    UINT32 Y;
    UINT32 X;

    (void)OsH;
    for (Y = 0; Y < H; Y++) {
        for (X = 0; X < W; X++) {
            UINT32 S = 0;
            UINT32 R0 = (Y * TTF_OS) * OsW + (X * TTF_OS);

            S += Hi[R0];
            S += Hi[R0 + 1u];
            S += Hi[R0 + OsW];
            S += Hi[R0 + OsW + 1u];
            Lo[Y * W + X] = (UINT8)((S + 2u) / 4u);
        }
    }
}

/*
 * mono 专用：共用字体基线（VMetrics），禁止 TopShift / 按字形高垂直居中。
 * 否则 T 与 y 会对齐到顶（截图像 Type）。
 */
static int FontTtfLayoutMono(stbtt_fontinfo *Info, UINT32 Cp, UINT32 CellW,
                             UINT32 CellH, float *OutScale, int *OutGw, int *OutGh,
                             int *OutOx, int *OutOy) {
    float Scale;
    int Ascent;
    int Descent;
    int LineGap;
    int Baseline;
    int X0;
    int Y0;
    int X1;
    int Y1;
    int Gw;
    int Gh;
    int Ox;
    int Oy;

    if (CellW == 0 || CellH == 0) {
        return -1;
    }
    Scale = stbtt_ScaleForPixelHeight(Info, (float)CellH);
    stbtt_GetFontVMetrics(Info, &Ascent, &Descent, &LineGap);
    (void)Descent;
    (void)LineGap;
    Baseline = (int)((float)Ascent * Scale);
    if (Baseline < 1) {
        Baseline = (int)CellH * 3 / 4;
    }
    if (Baseline >= (int)CellH) {
        Baseline = (int)CellH - 1;
    }
    stbtt_GetCodepointBitmapBoxSubpixel(Info, (int)Cp, Scale, Scale, 0.0f, 0.0f,
                                        &X0, &Y0, &X1, &Y1);
    Gw = X1 - X0;
    Gh = Y1 - Y0;
    if (Gw <= 0 || Gh <= 0) {
        return -1;
    }
    if (Gw > (int)CellW || Gh > (int)CellH) {
        float FitX = (float)CellW / (float)Gw;
        float FitY = (float)CellH / (float)Gh;
        float Fit = (FitX < FitY) ? FitX : FitY;

        Scale *= Fit;
        Baseline = (int)((float)Ascent * Scale);
        if (Baseline < 1) {
            Baseline = (int)CellH * 3 / 4;
        }
        if (Baseline >= (int)CellH) {
            Baseline = (int)CellH - 1;
        }
        stbtt_GetCodepointBitmapBoxSubpixel(Info, (int)Cp, Scale, Scale, 0.0f,
                                            0.0f, &X0, &Y0, &X1, &Y1);
        Gw = X1 - X0;
        Gh = Y1 - Y0;
        if (Gw <= 0 || Gh <= 0 || Gw > (int)CellW || Gh > (int)CellH) {
            return -1;
        }
    }
    Ox = ((int)CellW - Gw) / 2;
    Oy = Baseline + Y0;
    if (Ox < 0) {
        Ox = 0;
    }
    if (Oy < 0) {
        Oy = 0;
    }
    if (Ox + Gw > (int)CellW) {
        Gw = (int)CellW - Ox;
    }
    if (Oy + Gh > (int)CellH) {
        Gh = (int)CellH - Oy;
    }
    if (Gw <= 0 || Gh <= 0) {
        return -1;
    }
    *OutScale = Scale;
    *OutGw = Gw;
    *OutGh = Gh;
    *OutOx = Ox;
    *OutOy = Oy;
    return 0;
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    float Scale;
    float Shift;
    int Glyph;
    int Gw;
    int Gh;
    int Ox;
    int Oy;
    UINT32 CellW;
    UINT32 CellH;
    UINT32 OsW;
    UINT32 OsH;
    UINT32 i;
    UINT8 *Hi;
    UINT32 HiBytes;
    int Mono;

    /* 可打印 ASCII/汉字；mono 激活时用 MONO.TTF */
    if (!Pix18 || Cp < 32u || Cp == 127u) {
        return -1;
    }
    Mono = FontTtfMonoActive();
    if (Mono) {
        if (!gMonoInit || Cp >= 128u) {
            return -1;
        }
    } else if (!gInit) {
        return -1;
    }
    CellH = FontTtfCell();
    if (Mono) {
        CellW = gMonoAdv;
        if (CellW < 1u) {
            CellW = CellH / 2u;
        }
        if (CellW > TTF_CELL) {
            CellW = TTF_CELL;
        }
        if (CellW * CellH > TTF_PIX) {
            CellW = TTF_PIX / CellH;
        }
    } else {
        CellW = CellH;
    }
    for (i = 0; i < TTF_PIX; i++) {
        Pix18[i] = 0;
    }
    /*
     * font-freetype-hook / cjk-5：汉字、mono、UI 拉丁均优先 FT。
     * 拉丁绘制走 FontGlyphCrispLatin 软 AA（勿再汉字硬 crisp 滤空心）。
     */
    FontFtClearLastAdvance();
    if (FontFtReady() &&
        FontFtRasterCp(Cp, Pix18, CellW, CellH) == 0) {
        return 0;
    }
    FontFtClearLastAdvance();
    gArenaUsed = 0;
    if (!HalFpuBegin()) {
        return -1;
    }
    {
        stbtt_fontinfo *Info = FontTtfCur();

        if (Mono) {
            /* 1× + 共用基线；过采样会搅乱 Oy，mono 先保证整齐 */
            if (FontTtfLayoutMono(Info, Cp, CellW, CellH, &Scale, &Gw, &Gh, &Ox,
                                  &Oy) != 0) {
                HalFpuEnd();
                return -1;
            }
            stbtt_MakeCodepointBitmapSubpixel(Info, Pix18 + Oy * (int)CellW + Ox,
                                              Gw, Gh, (int)CellW, Scale, Scale,
                                              0.0f, 0.0f, (int)Cp);
            HalFpuEnd();
            return 0;
        }

        Glyph = stbtt_FindGlyphIndex(Info, (int)Cp);
        /*
         * cjk-3：glyph 0 = 字体无此码（常映 .notdef）。勿当 HIT 灌空白装过关；
         * 交 Cache MISS → 绘制层明示缺字（U+FFFD / 斜线 tofu）。
         */
        if (Glyph == 0) {
            HalFpuEnd();
            return -1;
        }

        /* UI 汉字：2× 过采样 → 箱滤；失败 → 1× */
        OsW = CellW * TTF_OS;
        OsH = CellH * TTF_OS;
        HiBytes = OsW * OsH;
        Hi = (UINT8 *)FontTtfMalloc(HiBytes);
        if (Hi != 0 &&
            FontTtfLayout(Info, Cp, Glyph, OsW, OsH, &Scale, &Shift, &Gw, &Gh,
                          &Ox, &Oy) == 0) {
            STBTT_memset(Hi, 0, HiBytes);
            stbtt_MakeCodepointBitmapSubpixel(Info, Hi + Oy * (int)OsW + Ox, Gw, Gh,
                                              (int)OsW, Scale, Scale, 0.0f, Shift,
                                              (int)Cp);
            FontTtfDownsample2(Hi, OsW, OsH, Pix18, CellW, CellH);
            HalFpuEnd();
            return 0;
        }

        gArenaUsed = 0;
        if (FontTtfLayout(Info, Cp, Glyph, CellW, CellH, &Scale, &Shift, &Gw, &Gh,
                          &Ox, &Oy) != 0) {
            HalFpuEnd();
            return -1;
        }
        stbtt_MakeCodepointBitmapSubpixel(Info, Pix18 + Oy * (int)CellW + Ox, Gw,
                                          Gh, (int)CellW, Scale, Scale, 0.0f,
                                          Shift, (int)Cp);
    }
    HalFpuEnd();
    return 0;
}

#else

int FontTtfInit(void) {
    LogDrv("Boot: ttf init skip\n");
    return -1;
}

int FontTtfMonoInit(void) {
    return -1;
}

int FontTtfReady(void) {
    return 0;
}

int FontTtfMonoReady(void) {
    return 0;
}

void FontTtfMonoBegin(void) {
}

void FontTtfMonoEnd(void) {
}

int FontTtfMonoActive(void) {
    return 0;
}

UINT32 FontTtfMonoAdvance(void) {
    return 0;
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    (void)Cp;
    (void)Pix18;
    return -1;
}

#endif
