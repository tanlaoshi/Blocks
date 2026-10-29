/*
 * FontTtfRaster.c — 汉字按当前行高栅格（须 HalFpuBegin；CFLAGS_FPU）
 *
 * gui-ttf-oversample：先 2× 直栅进 arena，再 2×2 箱滤缩到 FontTtfCell；
 * 失败软退 1×。上沿对齐像素边界；缓存槽按物理边长（PR-23b）。
 */
#include "Font.h"
#include "FontLayout.h"
#include "Hal.h"
#include "SerialLog.h"
#include "CString.h"
#include "Compiler.h"

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
 * PR-23E：栅格边长必须是物理像素，比例按有理数算：
 *   Cell = FontTtfPixel() * Num / Den
 * 100% → 24、150% → 36（24*3/2）、200% → 48、50% → 12。
 * FontTtfPixel 保持逻辑值（布局/行高用它），只有栅格/缓存用本函数。
 */
UINT32 FontTtfCell(void) {
    UINT64 Num = (UINT64)HalVideoGetDrawNum();
    UINT64 Den = (UINT64)HalVideoGetDrawDen();
    UINT32 Cell;

    if (Den == 0u) {
        Den = 1u;
    }
    if (Num == 0u) {
        Num = 1u;
    }
    Cell = (UINT32)(((UINT64)FontTtfPixel() * Num) / Den);
    if (Cell < 1u) {
        Cell = 1u;
    }
    return Cell;
}

/* 拉丁加档上限：1× 栅格缓冲（TTF_CELL=64）装得下的 em。 */
#define FONT_LATIN_EM_MAX 64u

/*
 * UI 拉丁「整数加档」：+1/4 格，钳 64。
 * FT 与 stb 必须用同一个值，否则切换栅格器时英文字号会跳一档。
 * 注意 2× 过采样路径由 FontTtfLayout 自己按「2×」算（见 EmPx），
 * 这里只描述 1× 逻辑格。
 */
UINT32 FontTtfLatinEm(UINT32 CellH) {
    UINT32 EmPx = CellH + (CellH / 4u);

    if (EmPx < CellH) {
        EmPx = CellH;
    }
    if (EmPx > FONT_LATIN_EM_MAX) {
        EmPx = FONT_LATIN_EM_MAX;
    }
    return EmPx;
}

#if defined(__x86_64__) || defined(__aarch64__)

#include <stddef.h>

#define TTF_CELL 64
#define TTF_PIX  (TTF_CELL * TTF_CELL)
/*
 * PR-23E / 坑 2：ASCII 提权上限必须装得下 200% 的 cell（48→EmPx 60）。
 * 位图格比 TTF_CELL 大时，MakeBitmap 会越界写 arena。
 */
BLOCKS_STATIC_ASSERT(TTF_CELL >= 60, "TTF_CELL must hold 200% ASCII em 60");
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

/*
 * 格内共用基线（物理像素，从格顶到基线）。
 *
 * 旧代码 FT 用 Face->size->metrics（FreeType 把 ascender/descender 向上
 * 取整到整像素：UMing@24 ascender 21.49→22、descender -3.63→-4），再套
 * 「ascent+descent 超格则上移」得 20；stb 用 (int)(ascent*scale) 截断得
 * 21。于是同一字号下两条路径的基线差 1px：FT 拉丁加档 30px 的墨顶是
 * 21，Oy=20-21=-1，位图顶行被 blit 丢掉 →「英文最上面像被削掉一行」。
 *
 * 这里统一成 stb 的截断口径，并把「加档拉丁的最高墨顶」并进基线：
 * 加档位图的顶行永远落在 Oy>=0，一个像素都不丢；汉字与拉丁共用同一
 * 个基线，切换栅格器不会上下跳。
 *
 * 注意：只取字母/数字（行盒的定义者）。括号/竖线比格还高，属于
 * 「加档装不下」的个别字形，由栅格侧退回 CellH ppem 重栅，不能为了
 * 它们把整行基线压到底。
 */
int FontTtfCellBaseline(UINT32 CellH) {
    static const char Letters[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    stbtt_fontinfo *Info;
    int Asc;
    int Desc;
    int LineGap;
    int Base;
    int DescPx;
    int Top = 0;
    float Scale;
    float EmScale;
    UINT32 EmPx;
    const char *P;

    if (!gInit || CellH == 0u || FontTtfMonoActive()) {
        return -1;
    }
    Info = FontTtfCur();
    if (!Info) {
        return -1;
    }
    Scale = stbtt_ScaleForMappingEmToPixels(Info, (float)CellH);
    stbtt_GetFontVMetrics(Info, &Asc, &Desc, &LineGap);
    (void)LineGap;
    Base = (int)((float)Asc * Scale);
    DescPx = (int)(-(float)Desc * Scale);
    if (DescPx < 2) {
        DescPx = 2;
    }
    if (Base + DescPx > (int)CellH) {
        Base = (int)CellH - DescPx;
    }
    if (Base < 1) {
        Base = (int)CellH * 3 / 4;
    }
    if (Base >= (int)CellH) {
        Base = (int)CellH - 1;
    }
    EmPx = FontTtfLatinEm(CellH);
    EmScale = stbtt_ScaleForMappingEmToPixels(Info, (float)EmPx);
    for (P = Letters; *P; P++) {
        int Glyph = stbtt_FindGlyphIndex(Info, (int)(UINT8)*P);
        int X0;
        int Y0;
        int X1;
        int Y1;
        int T;

        if (Glyph == 0 || !stbtt_GetGlyphBox(Info, Glyph, &X0, &Y0, &X1, &Y1)) {
            continue;
        }
        (void)X0;
        (void)Y0;
        (void)X1;
        T = FontTtfIceil((float)Y1 * EmScale);
        if (T > Top) {
            Top = T;
        }
    }
    if (Top > Base) {
        Base = Top;
    }
    if (Base < 1) {
        Base = 1;
    }
    if (Base >= (int)CellH) {
        Base = (int)CellH - 1;
    }
    return Base;
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
 * UI stb 布局：MappingEm + TopShift + 共用基线（过采样前后均用）。
 * Baseline 由调用方按 1× 逻辑格算好（FontTtfCellBaseline），2× 路径传
 * 2× 值——两条栅格路径因此共用同一条基线，切换时不会上下跳。
 * Boost≠0 时 ASCII 才加档；加档装不下（括号/竖线等）由调用方以
 * Boost=0 重试一次。
 */
static int FontTtfLayout(stbtt_fontinfo *Info, UINT32 Cp, int Glyph, UINT32 CellW,
                         UINT32 CellH, int Baseline, int Boost, float *OutScale,
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

    if (CellW == 0 || CellH == 0) {
        return -1;
    }
    /*
     * 拉丁整数加档：1× 时 em=CellH+CellH/4（100%→30）；2× 过采样时
     * CellH 已经是 2×，em 也自然是 2×（60），栅格完箱滤回 1×。
     *
     * 旧版把上限写成 FontTtfCell()*4/3（100%→32），2× 路径的 CellH=48
     * 一进来就被夹回 48，等于「2× 路径没有加档」——FT 挂了退 stb 时
     * 英文会突然小一号。这里上限按 2× 放大，两条路径的字号才一致。
     */
    {
        UINT32 EmPx = CellH;

        if (Cp < 128u && Boost) {
            EmPx = CellH + (CellH / 4u);
            if (EmPx < CellH) {
                EmPx = CellH;
            }
            if (EmPx > 2u * FONT_LATIN_EM_MAX) {
                EmPx = 2u * FONT_LATIN_EM_MAX;
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
     * 落格：共用基线 + 顶对齐兜底。
     *
     * 「位图所有行都要留下」在这里写死：
     * - Gh <= 格高：只要顶/底有一行越界，就整字平移进格（顶对齐），
     *   绝不让 blit 从顶上裁掉一行。正常拉丁的 Oy 已经是 0~正值
     *   （基线已按加档墨顶抬高），只有 '_' 这类贴底字会触发底越界。
     * - Gh > 格高：根本装不下。拉丁返回 -1，由调用方退 CellH ppem
     *   重栅一次；汉字沿用「保顶裁底」。
     */
    stbtt_GetCodepointBitmapBoxSubpixel(Info, (int)Cp, Scale, Scale, 0.0f, Shift,
                                        &X0, &Y0, &X1, &Y1);
    Gw = X1 - X0;
    Gh = Y1 - Y0;
    if (Gw <= 0 || Gh <= 0) {
        return -1;
    }
    Ox = (Cp < 128u) ? 0 : (((int)CellW - Gw) / 2);
    Oy = Baseline + Y0;
    if (Gh <= (int)CellH) {
        if (Oy < 0 || Oy + Gh > (int)CellH) {
            Oy = (int)CellH - Gh;
        }
        if (Oy < 0) {
            Oy = 0;
        }
    } else {
        if (Cp < 128u) {
            return -1;
        }
        if (Oy < 0) {
            Oy = 0;
        }
    }
    if (Ox < 0) {
        Ox = 0;
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
    int Base1;
    int Rc;

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
    /*
     * 两条栅格路径共用同一条基线（1× 逻辑格口径）：FT 侧直接调
     * FontTtfCellBaseline()，这里 2× 路径乘 2、1× 路径原样用。
     */
    Base1 = Mono ? (int)CellH * 3 / 4 : FontTtfCellBaseline(CellH);
    if (Base1 < 0) {
        Base1 = (int)CellH * 3 / 4;
    }
    if (Base1 < 1) {
        Base1 = 1;
    }
    if (Base1 >= (int)CellH) {
        Base1 = (int)CellH - 1;
    }
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
        if (Hi != 0) {
            /*
             * 先按加档 em 布局；拉丁若装不下（括号/竖线比格高）返回 -1，
             * 退回不加档的 CellH em 再算一次——保证位图每一行都在格内。
             */
            Rc = FontTtfLayout(Info, Cp, Glyph, OsW, OsH, Base1 * 2, 1, &Scale,
                               &Shift, &Gw, &Gh, &Ox, &Oy);
            if (Rc != 0 && Cp < 128u) {
                Rc = FontTtfLayout(Info, Cp, Glyph, OsW, OsH, Base1 * 2, 0, &Scale,
                                   &Shift, &Gw, &Gh, &Ox, &Oy);
            }
            if (Rc == 0) {
                STBTT_memset(Hi, 0, HiBytes);
                stbtt_MakeCodepointBitmapSubpixel(Info, Hi + Oy * (int)OsW + Ox, Gw,
                                                  Gh, (int)OsW, Scale, Scale, 0.0f,
                                                  Shift, (int)Cp);
                FontTtfDownsample2(Hi, OsW, OsH, Pix18, CellW, CellH);
                HalFpuEnd();
                return 0;
            }
        }

        gArenaUsed = 0;
        Rc = FontTtfLayout(Info, Cp, Glyph, CellW, CellH, Base1, 1, &Scale, &Shift,
                           &Gw, &Gh, &Ox, &Oy);
        if (Rc != 0 && Cp < 128u) {
            Rc = FontTtfLayout(Info, Cp, Glyph, CellW, CellH, Base1, 0, &Scale,
                               &Shift, &Gw, &Gh, &Ox, &Oy);
        }
        if (Rc != 0) {
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
