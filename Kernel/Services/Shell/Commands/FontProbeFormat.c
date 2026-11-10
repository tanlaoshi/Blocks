/*
 * FontProbeFormat.c — fonts probe 串格式 / 墨列（自 FontProbeMeasure.c 纯搬 · PR-NORM-16）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Font.h"
#include "Hal.h"

#define FONT_PROBE_PAINT_LATIN 24u
#define FONT_PROBE_PAINT_CJK 88u
#define FONT_PROBE_INK_ALPHA 128u

char *ProbePutStr(char *P, const char *S) {
    while (S && *S) {
        *P++ = *S++;
    }
    return P;
}

char *ProbePutU32(char *P, UINT32 V) {
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

char *ProbePutPair(char *P, const char *Key, UINT32 V) {
    *P++ = ' ';
    P = ProbePutStr(P, Key);
    *P++ = '=';
    return ProbePutU32(P, V);
}

int ProbeWordEq(const char *A, const char *B) {
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
UINT32 ProbeEdgeAlpha(const UINT8 *G, UINT32 W, UINT32 H, UINT32 Col) {
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
UINT32 ProbeInkColumns(const UINT8 *G, UINT32 W, UINT32 H, UINT32 Bpp,
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

UINT32 ProbeMulDivUp(UINT32 V, UINT32 Mul, UINT32 Div) {
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
UINT32 ProbeInkLogical(UINT32 W, UINT32 H, UINT32 Ink) {
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
