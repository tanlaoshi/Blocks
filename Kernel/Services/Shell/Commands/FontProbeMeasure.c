/*
 * FontProbeMeasure.c — fonts probe 实现（自 FontProbe.c 纯搬 · PR-NORM-16）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Font.h"
#include "Hal.h"

#define FONT_PROBE_LINE 160u
#define FONT_PROBE_PAINT_LATIN 24u
#define FONT_PROBE_PAINT_CJK 88u
#define FONT_PROBE_INK_ALPHA 128u
#define FONT_PROBE_ONE 5u
#define FONT_PROBE_MAX 48u

typedef struct {
    UINT32 Adv;
    UINT32 Adv2;
    UINT32 Ink;
    UINT32 InkP;
    UINT32 Edge;
    int Hit0;
    int Hit1;
} FONT_PROBE_CHAR;

typedef struct {
    UINT32 Total;
    UINT32 Total2;
    UINT32 Overlap;
    UINT32 Worst;
} FONT_PROBE_SUM;

/* FontProbeFormat.c · PR-NORM-16 */
char *ProbePutStr(char *P, const char *S);
char *ProbePutU32(char *P, UINT32 V);
char *ProbePutPair(char *P, const char *Key, UINT32 V);
int ProbeWordEq(const char *A, const char *B);
UINT32 ProbeEdgeAlpha(const UINT8 *G, UINT32 W, UINT32 H, UINT32 Col);
UINT32 ProbeInkColumns(const UINT8 *G, UINT32 W, UINT32 H, UINT32 Bpp,
                       UINT32 Min);
UINT32 ProbeMulDivUp(UINT32 V, UINT32 Mul, UINT32 Div);
UINT32 ProbeInkLogical(UINT32 W, UINT32 H, UINT32 Ink);

static char gProbeOne[FONT_PROBE_MAX][FONT_PROBE_ONE];
static UINT32 gProbeCp[FONT_PROBE_MAX];
static FONT_PROBE_CHAR gProbeChar[FONT_PROBE_MAX];

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

void ShellCommandFonts(int Argc, char **Argv) {
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
