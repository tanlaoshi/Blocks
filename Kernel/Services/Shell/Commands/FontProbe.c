/*
 * ShellCommandsFontProbe.c — `fonts probe [cold] <text>...`：步进/墨宽数字判据
 *
 * 叠字判据（只读；cold 只清字形缓存，不改配置/DB）：
 *   adv  = 布局此刻拿到的逻辑步进（FontCodepointAdvance，在画之前取，
 *          与 Chrome.c / Desktop/Paint.c / Text.c 的取法同序）
 *   ink  = 真正画出去那张位图的墨宽，换算成同一逻辑像素口径
 *   adv2 = 画完这一格后再取一次（= 下一帧起布局会拿到的值）
 * adv < ink ⇒ 下一个字母的左沿落进本字母的墨里 ⇒ 叠字。
 * 空格无墨，ink=0，只看 adv；它的宽度由 sum 行与相邻字母对比。
 *
 * cold：先 FontTtfCacheDropHits 再量，复现「换脸 / 换缩放档后第一次重画」——
 * 这两处都会把整表打成 EMPTY（FontSetById、PR-23E 的 Side 不符重栅）。
 * 测量全部做完才打印：打印本身会画字，边量边打印会提前把后面的字灌热。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Font.h"
#include "Hal.h"

#define FONT_PROBE_LINE 160u
/* 落笔门槛：GlyphCrispAlpha8 对拉丁/汉字分别丢掉低于 24/88 的灰边 */
#define FONT_PROBE_PAINT_LATIN 24u
#define FONT_PROBE_PAINT_CJK 88u
/*
 * 实墨门槛：半覆盖（128）以上才算墨。低于它的像素是 AA 灰边，会被下一个
 * 字母混掉，肉眼不成立；拿它当判据等于要求给每个字母再加 1px 字距。
 */
#define FONT_PROBE_INK_ALPHA 128u
#define FONT_PROBE_ONE 5u
#define FONT_PROBE_MAX 48u

typedef struct {
    UINT32 Adv;  /* 画之前布局拿到的步进 */
    UINT32 Adv2; /* 画之后布局会拿到的步进 */
    UINT32 Ink;  /* 实墨宽（逻辑像素）：判据用 */
    UINT32 InkP; /* 落笔范围（位图列宽）：含 AA 灰边，供对照 */
    UINT32 Edge; /* 落笔范围最右一列的最大灰度：<128 即只是灰边 */
    int Hit0;    /* 画之前缓存是否已 HIT */
    int Hit1;    /* 画之后缓存是否 HIT */
} FONT_PROBE_CHAR;

typedef struct {
    UINT32 Total;
    UINT32 Total2;
    UINT32 Overlap;
    UINT32 Worst;
} FONT_PROBE_SUM;

static char gProbeOne[FONT_PROBE_MAX][FONT_PROBE_ONE];
static UINT32 gProbeCp[FONT_PROBE_MAX];
static FONT_PROBE_CHAR gProbeChar[FONT_PROBE_MAX];

static char *ProbePutStr(char *P, const char *S) {
    while (S && *S) {
        *P++ = *S++;
    }
    return P;
}

static char *ProbePutU32(char *P, UINT32 V) {
    char Tmp[10];
    UINT32 n = 0;

    if (V == 0u) {
        *P++ = '0';
        return P;
    }
    while (V != 0u && n < (UINT32)sizeof(Tmp)) {
        Tmp[n++] = (char)('0' + (int)(V % 10u));
        V /= 10u;
    }
    while (n > 0u) {
        *P++ = Tmp[--n];
    }
    return P;
}

static char *ProbePutPair(char *P, const char *Key, UINT32 V) {
    *P++ = ' ';
    P = ProbePutStr(P, Key);
    *P++ = '=';
    return ProbePutU32(P, V);
}

static int ProbeWordEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

/* 某一列里的最大灰度 */
static UINT32 ProbeEdgeAlpha(const UINT8 *G, UINT32 W, UINT32 H, UINT32 Col) {
    UINT32 Row;
    UINT32 Max = 0;

    for (Row = 0; Row < H; Row++) {
        UINT32 V = G[Row * W + Col];

        if (V > Max) {
            Max = V;
        }
    }
    return Max;
}

/* 位图里最右一列达到 Min 的列宽；0 = 全空 */
static UINT32 ProbeInkColumns(const UINT8 *G, UINT32 W, UINT32 H, UINT32 Bpp,
                              UINT32 Min) {
    UINT32 Best = 0;
    UINT32 Col;
    UINT32 Row;

    if (Bpp != 8u) {
        Min = 1u;
    }
    for (Col = 0; Col < W; Col++) {
        for (Row = 0; Row < H; Row++) {
            UINT32 V;

            if (Bpp == 8u) {
                V = G[Row * W + Col];
            } else {
                V = (G[Row * ((W + 7u) / 8u) + Col / 8u] >> (7u - (Col & 7u))) &
                    1u;
            }
            if (V >= Min) {
                Best = Col + 1u;
                break;
            }
        }
    }
    return Best;
}

static UINT32 ProbeMulDivUp(UINT32 V, UINT32 Mul, UINT32 Div) {
    UINT64 T;

    if (Div == 0u) {
        return V;
    }
    T = ((UINT64)V * (UINT64)Mul + (UINT64)Div - 1u) / (UINT64)Div;
    return (UINT32)T;
}

/*
 * 位图墨宽 → 逻辑像素。三条绘制路径（见 Glyph.c VideoDrawBitmapAt）：
 *   1) 8bpp 且边长==FontTtfCell()≠FontTtfPixel()：物理 1:1 落笔（PR-23b）
 *   2) 8bpp 其余（100% 或逻辑位图）：整块按 FontGlyphStretch 放大
 *   3) 点阵：FontGlyphBox 装不下时 PaintGlyphFitted 再缩
 */
static UINT32 ProbeInkLogical(UINT32 W, UINT32 H, UINT32 Ink) {
    UINT32 Bpp = FontGlyphCpBpp();
    UINT32 Step = FontGlyphStretch(H);
    UINT32 BoxW;
    UINT32 BoxH;

    if (Ink == 0u || W == 0u) {
        return 0u;
    }
    if (Step < 1u) {
        Step = 1u;
    }
    if (Bpp == 8u) {
        if (FontTtfCell() != FontTtfPixel() && W == FontTtfCell() &&
            H == FontTtfCell()) {
            return ProbeMulDivUp(Ink, HalVideoGetDrawDen(),
                                 HalVideoGetDrawNum());
        }
        return Ink * Step;
    }
    FontGlyphBox(W, H, &BoxW, &BoxH);
    if (BoxW <= W * Step) {
        return Ink * Step;
    }
    return ProbeMulDivUp(Ink, BoxW, W);
}

/* 按绘制同序取三个数：画前 adv / 画后 adv2 / 位图墨宽 */
static void ProbeOneChar(UINT32 Cp, FONT_PROBE_CHAR *Out) {
    const UINT8 *G;
    UINT32 W = 0;
    UINT32 H = 0;

    Out->Hit0 = FontTtfCacheAdvance(Cp) != 0u;
    Out->Adv = FontCodepointAdvance(Cp);
    G = FontGlyphCp(Cp, &W, &H);
    Out->Hit1 = FontTtfCacheAdvance(Cp) != 0u;
    Out->Adv2 = FontCodepointAdvance(Cp);
    Out->Ink = 0u;
    Out->InkP = 0u;
    Out->Edge = 0u;
    if (G) {
        UINT32 Bpp = FontGlyphCpBpp();
        UINT32 Paint = (Cp < 128u) ? FONT_PROBE_PAINT_LATIN
                                   : FONT_PROBE_PAINT_CJK;
        UINT32 Core;

        Out->InkP = ProbeInkColumns(G, W, H, Bpp, Paint);
        Core = ProbeInkColumns(G, W, H, Bpp, FONT_PROBE_INK_ALPHA);
        Out->Ink = ProbeInkLogical(W, H, Core);
        if (Bpp == 8u && Out->InkP > 0u) {
            Out->Edge = ProbeEdgeAlpha(G, W, H, Out->InkP - 1u);
        }
    }
}

static void ProbePrintChar(const char *One, UINT32 Cp,
                           const FONT_PROBE_CHAR *C) {
    char Line[FONT_PROBE_LINE];
    char *P;

    P = ProbePutStr(Line, "  '");
    P = ProbePutStr(P, One);
    P = ProbePutStr(P, "'");
    P = ProbePutPair(P, "cp", Cp);
    P = ProbePutPair(P, "adv", C->Adv);
    P = ProbePutPair(P, "ink", C->Ink);
    P = ProbePutPair(P, "inkp", C->InkP);
    P = ProbePutPair(P, "edge", C->Edge);
    P = ProbePutPair(P, "adv2", C->Adv2);
    P = ProbePutStr(P, C->Hit0 ? " hit=yes" : " hit=no");
    P = ProbePutStr(P, C->Hit1 ? "->yes" : "->no");
    if (C->Adv < C->Ink) {
        P = ProbePutStr(P, " OVERLAP");
    }
    *P++ = '\r';
    *P++ = '\n';
    *P = 0;
    gConsole.Write(Line);
}

static void ProbePrintSum(const FONT_PROBE_SUM *S) {
    char Line[FONT_PROBE_LINE];
    char *P;

    P = ProbePutStr(Line, "  sum");
    P = ProbePutPair(P, "total", S->Total);
    P = ProbePutPair(P, "total2", S->Total2);
    P = ProbePutPair(P, "overlap", S->Overlap);
    P = ProbePutPair(P, "worst", S->Worst);
    P = ProbePutStr(P, S->Overlap ? " judge=OVERLAP" : " judge=OK");
    *P++ = '\r';
    *P++ = '\n';
    *P = 0;
    gConsole.Write(Line);
}

/* 先把整串量完（打印会画字，边量边打印会提前把后面的字灌热） */
static UINT32 ProbeMeasure(const char *S) {
    UINT32 n = 0;

    while (S && *S && n < FONT_PROBE_MAX) {
        UINT32 Cp;
        UINTN Len = Utf8Decode(S, &Cp);
        UINTN k;

        if (Len == 0 || Len >= FONT_PROBE_ONE) {
            S++;
            continue;
        }
        for (k = 0; k < Len; k++) {
            gProbeOne[n][k] = S[k];
        }
        gProbeOne[n][k] = 0;
        gProbeCp[n] = Cp;
        ProbeOneChar(Cp, &gProbeChar[n]);
        n++;
        S += Len;
    }
    return n;
}

static void ProbePrintString(const char *S, UINT32 N) {
    FONT_PROBE_SUM Sum = { 0u, 0u, 0u, 0u };
    UINT32 i;

    gConsole.Write(S);
    gConsole.Write("\n");
    for (i = 0; i < N; i++) {
        ProbePrintChar(gProbeOne[i], gProbeCp[i], &gProbeChar[i]);
        Sum.Total += gProbeChar[i].Adv;
        Sum.Total2 += gProbeChar[i].Adv2;
        if (gProbeChar[i].Adv < gProbeChar[i].Ink) {
            Sum.Overlap++;
            if (Sum.Worst < gProbeChar[i].Ink - gProbeChar[i].Adv) {
                Sum.Worst = gProbeChar[i].Ink - gProbeChar[i].Adv;
            }
        }
    }
    ProbePrintSum(&Sum);
}

static void ProbePrintHeader(const char *Mode) {
    char Line[FONT_PROBE_LINE];
    char Label[32];
    char *P;

    FontFaceLabel(FontCurrentId(), Label, (int)sizeof(Label));
    P = ProbePutStr(Line, "fonts probe");
    P = ProbePutStr(P, Mode);
    P = ProbePutPair(P, "ratio-num", HalVideoGetDrawNum());
    P = ProbePutPair(P, "ratio-den", HalVideoGetDrawDen());
    P = ProbePutPair(P, "px", FontTtfPixel());
    P = ProbePutPair(P, "cell", FontTtfCell());
    P = ProbePutStr(P, " font=");
    P = ProbePutStr(P, Label[0] ? Label : "?");
    *P++ = '\r';
    *P++ = '\n';
    *P = 0;
    gConsole.Write(Line);
}

static void CommandFonts(int Argc, char **Argv) {
    int First = 2;
    int Cold = 0;

    if (Argc < 3 || !ProbeWordEq(Argv[1], "probe")) {
        gConsole.Write("usage: fonts probe [cold] <text>...\n");
        return;
    }
    if (ProbeWordEq(Argv[2], "cold")) {
        Cold = 1;
        First = 3;
    }
    if (Argc <= First) {
        gConsole.Write("usage: fonts probe [cold] <text>...\n");
        return;
    }
    ProbePrintHeader(Cold ? " cold" : "");
    while (First < Argc) {
        /*
         * cold：每一串量之前再清一次。前一串的打印结果会被画到屏上，
         * 不重清就会把它里面的字母灌热，第二串就不再是「第一次重画」。
         */
        if (Cold) {
            FontTtfCacheDropHits();
        }
        ProbePrintString(Argv[First], ProbeMeasure(Argv[First]));
        First++;
    }
}

void ShellCommandsFontProbeRegister(void) {
    gConsole.Register("fonts", "fonts probe [cold] <text>... (adv/ink, read-only)",
                     CommandFonts);
}
