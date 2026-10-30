/*
 * Probe.c — `desktop icons` 只读探针（图标布局 / 物理矩形 / 四判据）
 *
 * 口径：图标/标签的逻辑矩形按桌面绘制同一套算式得出，再按
 * HalVideoGetDrawNum()/Den() 换成物理像素（起点下取整，末端取绘制覆盖到
 * 的最后一格，与 VideoWriteRect 的逐格跨度一致）。判据只看物理矩形：
 * 1 像素相交即算，避免 150% 下逻辑取整把真实相交藏起来。
 */
#include "DesktopPrivate.h"
#include "Console.h"

#define PROBE_LINE_MAX 192

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
} PROBE_RECT;

typedef struct {
    int OverlapA;
    int OverlapB;
    int LabelHit;
    int LabelNextA;
    int LabelNextB;
    int Taskbar;
} PROBE_AUDIT;

static UINT32 ProbeNum(void) {
    UINT32 Num = HalVideoGetDrawNum();

    return Num ? Num : 1u;
}

static UINT32 ProbeDen(void) {
    UINT32 Den = HalVideoGetDrawDen();

    return Den ? Den : 1u;
}

static UINT32 ProbeScale(UINT32 V) {
    return (UINT32)(((UINT64)V * (UINT64)ProbeNum()) / (UINT64)ProbeDen());
}

/* 与 VideoWriteRect 的逐逻辑格跨度一致：末端 floor，不额外上取整 */
static void ProbePhysRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                          PROBE_RECT *Out) {
    UINT32 Xp = ProbeScale(X);
    UINT32 Yp = ProbeScale(Y);

    Out->X = Xp;
    Out->Y = Yp;
    Out->W = ProbeScale(X + W) - Xp;
    Out->H = ProbeScale(Y + H) - Yp;
}

static int ProbeHit(const PROBE_RECT *A, const PROBE_RECT *B) {
    if (A->W == 0 || A->H == 0 || B->W == 0 || B->H == 0) {
        return 0;
    }
    return A->X < B->X + B->W && A->X + A->W > B->X &&
           A->Y < B->Y + B->H && A->Y + A->H > B->Y;
}

/* 图标脸与标签的逻辑矩形；标签宽为 0 表示没有标签 */
static void ProbeRects(int Idx, PROBE_RECT *Face, PROBE_RECT *Label) {
    const DESKTOP_ICON *Icon = &gIcons[Idx];
    UINT32 W = Icon->Label ? FontStringWidth(Icon->Label) : 0;
    UINT32 X = Icon->X;

    if (W > 0 && W < DESKTOP_ICON_SIZE) {
        X = Icon->X + (DESKTOP_ICON_SIZE - W) / 2u;
    }
    ProbePhysRect(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, Face);
    ProbePhysRect(X, Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD,
                  W, FontCellH(), Label);
}

static char *ProbePutStr(char *P, const char *S) {
    while (S && *S) {
        *P++ = *S++;
    }
    return P;
}

static char *ProbePutU32(char *P, UINT32 V) {
    char Tmp[10];
    UINT32 N = 0;

    if (V == 0u) {
        *P++ = '0';
        return P;
    }
    while (V != 0u && N < (UINT32)sizeof(Tmp)) {
        Tmp[N++] = (char)('0' + (int)(V % 10u));
        V /= 10u;
    }
    while (N > 0u) {
        *P++ = Tmp[--N];
    }
    return P;
}

static char *ProbePutPair(char *P, const char *Key, UINT32 V) {
    *P++ = ' ';
    P = ProbePutStr(P, Key);
    *P++ = '=';
    return ProbePutU32(P, V);
}

static char *ProbePutIcon(char *P, const char *Key, int Idx) {
    *P++ = ' ';
    P = ProbePutStr(P, Key);
    *P++ = '=';
    if (Idx < 0) {
        return ProbePutStr(P, "none");
    }
    P = ProbePutStr(P, "icon");
    return ProbePutU32(P, (UINT32)Idx);
}

static void ProbeFlush(char *Line, char *P) {
    *P++ = '\r';
    *P++ = '\n';
    *P = 0;
    gConsole.Write(Line);
}

static void ProbeOneIcon(int Idx, const PROBE_RECT *Face,
                         const PROBE_RECT *Tag) {
    char Line[PROBE_LINE_MAX];
    char *P;

    P = ProbePutStr(Line, "icon[");
    P = ProbePutU32(P, (UINT32)Idx);
    P = ProbePutStr(P, "] \"");
    P = ProbePutStr(P, gIcons[Idx].Label ? gIcons[Idx].Label : "");
    P = ProbePutStr(P, "\"");
    P = ProbePutPair(P, "x", gIcons[Idx].X);
    P = ProbePutPair(P, "y", gIcons[Idx].Y);
    P = ProbePutPair(P, "w", DESKTOP_ICON_SIZE);
    P = ProbePutPair(P, "h", DESKTOP_ICON_SIZE);
    P = ProbePutStr(P, " phys=(");
    P = ProbePutU32(P, Face->X);
    *P++ = ',';
    P = ProbePutU32(P, Face->Y);
    *P++ = ',';
    P = ProbePutU32(P, Face->W);
    *P++ = ',';
    P = ProbePutU32(P, Face->H);
    P = ProbePutStr(P, ") label=(");
    P = ProbePutU32(P, Tag->X);
    *P++ = ',';
    P = ProbePutU32(P, Tag->Y);
    *P++ = ',';
    P = ProbePutU32(P, Tag->W);
    *P++ = ',';
    P = ProbePutU32(P, Tag->H);
    *P++ = ')';
    ProbeFlush(Line, P);
}

static void ProbeAudit(const PROBE_RECT *Face, const PROBE_RECT *Tag,
                       const int *Present, UINT32 BarYp, PROBE_AUDIT *Out) {
    int i;
    int j;

    Out->OverlapA = -1;
    Out->OverlapB = -1;
    Out->LabelHit = -1;
    Out->LabelNextA = -1;
    Out->LabelNextB = -1;
    Out->Taskbar = -1;
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        if (!Present[i]) {
            continue;
        }
        if (Out->OverlapA < 0) {
            for (j = i + 1; j < DESKTOP_ICON_COUNT; j++) {
                if (Present[j] && ProbeHit(&Face[i], &Face[j])) {
                    Out->OverlapA = i;
                    Out->OverlapB = j;
                    break;
                }
            }
        }
        if (Out->LabelHit < 0 && ProbeHit(&Face[i], &Tag[i])) {
            Out->LabelHit = i;
        }
        if (Out->LabelNextA < 0) {
            for (j = 0; j < DESKTOP_ICON_COUNT; j++) {
                if (j != i && Present[j] && ProbeHit(&Tag[i], &Face[j])) {
                    Out->LabelNextA = i;
                    Out->LabelNextB = j;
                    break;
                }
            }
        }
        if (Out->Taskbar < 0 &&
            (Face[i].Y + Face[i].H > BarYp || Tag[i].Y + Tag[i].H > BarYp)) {
            Out->Taskbar = i;
        }
    }
}

static void ProbeSummary(const PROBE_AUDIT *Audit) {
    char Line[PROBE_LINE_MAX];
    char *P;

    P = ProbePutIcon(Line, "overlap", Audit->OverlapA);
    if (Audit->OverlapA >= 0) {
        *P++ = '/';
        P = ProbePutStr(P, "icon");
        P = ProbePutU32(P, (UINT32)Audit->OverlapB);
    }
    P = ProbePutIcon(P, "labelHit", Audit->LabelHit);
    P = ProbePutIcon(P, "labelNext", Audit->LabelNextA);
    if (Audit->LabelNextA >= 0) {
        *P++ = '/';
        P = ProbePutStr(P, "icon");
        P = ProbePutU32(P, (UINT32)Audit->LabelNextB);
    }
    P = ProbePutIcon(P, "taskbar", Audit->Taskbar);
    P = ProbePutPair(P, "ui", HalVideoGetUiScale());
    *P++ = '%';
    if (DesktopIconLayoutLimited()) {
        P = ProbePutStr(P, " layout=min-overlap");
    }
    ProbeFlush(Line, P);
}

static void DesktopIconsCommand(int Argc, char **Argv) {
    PROBE_RECT Face[DESKTOP_ICON_COUNT];
    PROBE_RECT Tag[DESKTOP_ICON_COUNT];
    int Present[DESKTOP_ICON_COUNT];
    PROBE_AUDIT Audit;
    UINT32 BarY;
    UINT32 Sw;
    UINT32 Sh;
    int i;

    (void)Argc;
    (void)Argv;
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        Present[i] = gIcons[i].Present ? 1 : 0;
        if (Present[i]) {
            ProbeRects(i, &Face[i], &Tag[i]);
            ProbeOneIcon(i, &Face[i], &Tag[i]);
        }
    }
    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)Sw;
    (void)Sh;
    ProbeAudit(Face, Tag, Present, ProbeScale(BarY), &Audit);
    ProbeSummary(&Audit);
}

void DesktopIconsCommandRegister(void) {
    gConsole.Register2("desktop", "icons",
                       "desktop icons (layout + overlap audit, read-only)",
                       DesktopIconsCommand);
}
