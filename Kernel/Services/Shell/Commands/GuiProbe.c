/*
 * ShellCommandsGuiProbe.c — `gui` 只读判据（窗备份 / 窗矩形）
 *
 * `gui backupprobe`：抓备份→贴回逐物理像素比对，diff=0 才不糊。
 * `gui wr`：打印每个活动窗的逻辑矩形与物理矩形、是否完整落在可用区，
 *           以及任意两窗是否重叠、最小物理间距；开窗布局是否合格一眼可判。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Gui.h"
#include "HalVideo.h"

#define GUI_PROBE_LINE 192u
#define GUI_PROBE_NO_GAP 0xFFFFFFFFu

static char *GuiPutStr(char *P, const char *S) {
    while (S && *S) {
        *P++ = *S++;
    }
    return P;
}

static char *GuiPutU32(char *P, UINT32 V) {
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

static char *GuiPutPair(char *P, const char *Key, UINT32 V) {
    *P++ = ' ';
    P = GuiPutStr(P, Key);
    *P++ = '=';
    return GuiPutU32(P, V);
}

static int GuiWordEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

/* 物理矩形相交判据：1 px 也不能算「不重叠」 */
static int GuiPhysOverlap(const GUI_RECT_INFO *A, const GUI_RECT_INFO *B) {
    return A->PhysX < B->PhysX + B->PhysWidth &&
           A->PhysX + A->PhysWidth > B->PhysX &&
           A->PhysY < B->PhysY + B->PhysHeight &&
           A->PhysY + A->PhysHeight > B->PhysY;
}

/* 两窗不相交时的切比雪夫间距（px）：斜向取两轴较大者 */
static UINT32 GuiPhysGap(const GUI_RECT_INFO *A, const GUI_RECT_INFO *B) {
    UINT32 GapX = 0;
    UINT32 GapY = 0;

    if (A->PhysX + A->PhysWidth <= B->PhysX) {
        GapX = B->PhysX - (A->PhysX + A->PhysWidth);
    } else if (B->PhysX + B->PhysWidth <= A->PhysX) {
        GapX = A->PhysX - (B->PhysX + B->PhysWidth);
    }
    if (A->PhysY + A->PhysHeight <= B->PhysY) {
        GapY = B->PhysY - (A->PhysY + A->PhysHeight);
    } else if (B->PhysY + B->PhysHeight <= A->PhysY) {
        GapY = A->PhysY - (B->PhysY + B->PhysHeight);
    }
    return (GapX > GapY) ? GapX : GapY;
}

static int GuiPhysInside(const GUI_RECT_INFO *R, const GUI_RECT_INFO *U) {
    return R->PhysX >= U->PhysX && R->PhysY >= U->PhysY &&
           R->PhysX + R->PhysWidth <= U->PhysX + U->PhysWidth &&
           R->PhysY + R->PhysHeight <= U->PhysY + U->PhysHeight;
}

static void GuiWriteWindowLine(int Index, const GUI_WINDOW_INFO *Win,
                               const GUI_RECT_INFO *Usable) {
    char Line[GUI_PROBE_LINE];
    char *P = Line;

    P = GuiPutStr(P, "win[");
    P = GuiPutU32(P, (UINT32)Index);
    P = GuiPutStr(P, "] title=\"");
    P = GuiPutStr(P, Win->Title);
    P = GuiPutStr(P, "\" x=");
    P = GuiPutU32(P, Win->Rect.X);
    P = GuiPutStr(P, " y=");
    P = GuiPutU32(P, Win->Rect.Y);
    P = GuiPutStr(P, " w=");
    P = GuiPutU32(P, Win->Rect.Width);
    P = GuiPutStr(P, " h=");
    P = GuiPutU32(P, Win->Rect.Height);
    P = GuiPutStr(P, " phys=(");
    P = GuiPutU32(P, Win->Rect.PhysX);
    *P++ = ',';
    P = GuiPutU32(P, Win->Rect.PhysY);
    *P++ = ',';
    P = GuiPutU32(P, Win->Rect.PhysWidth);
    *P++ = ',';
    P = GuiPutU32(P, Win->Rect.PhysHeight);
    *P++ = ')';
    P = GuiPutStr(P, GuiPhysInside(&Win->Rect, Usable) ? " inside=yes" : " inside=no");
    *P++ = '\r';
    *P++ = '\n';
    *P = 0;
    gConsole.Write(Line);
}

static void GuiWriteWindowSummary(const GUI_WINDOW_INFO *Wins, int Count,
                                  const GUI_RECT_INFO *Usable, int AllInside) {
    char Line[GUI_PROBE_LINE];
    char *P = Line;
    UINT32 MinGap = GUI_PROBE_NO_GAP;
    int Overlap = 0;
    int i;
    int j;

    for (i = 0; i < Count; i++) {
        for (j = i + 1; j < Count; j++) {
            if (GuiPhysOverlap(&Wins[i].Rect, &Wins[j].Rect)) {
                Overlap = 1;
            } else {
                UINT32 Gap = GuiPhysGap(&Wins[i].Rect, &Wins[j].Rect);
                if (Gap < MinGap) {
                    MinGap = Gap;
                }
            }
        }
    }
    P = GuiPutStr(P, "overlap=");
    P = GuiPutStr(P, Overlap ? "yes" : "none");
    if (!Overlap && MinGap != GUI_PROBE_NO_GAP) {
        P = GuiPutPair(P, "gap", MinGap);
        P = GuiPutStr(P, "px");
    }
    P = GuiPutStr(P, AllInside ? " inside=yes" : " inside=no");
    P = GuiPutStr(P, " ui=");
    P = GuiPutU32(P, HalVideoGetUiScale());
    P = GuiPutStr(P, "% usable=");
    P = GuiPutStr(P, "(");
    P = GuiPutU32(P, Usable->X);
    *P++ = ',';
    P = GuiPutU32(P, Usable->Y);
    *P++ = ',';
    P = GuiPutU32(P, Usable->Width);
    *P++ = ',';
    P = GuiPutU32(P, Usable->Height);
    P = GuiPutStr(P, ") usable-phys=(");
    P = GuiPutU32(P, Usable->PhysX);
    *P++ = ',';
    P = GuiPutU32(P, Usable->PhysY);
    *P++ = ',';
    P = GuiPutU32(P, Usable->PhysWidth);
    *P++ = ',';
    P = GuiPutU32(P, Usable->PhysHeight);
    P = GuiPutStr(P, ")");
    *P++ = '\r';
    *P++ = '\n';
    *P = 0;
    gConsole.Write(Line);
}

static void CommandGuiWindowRects(void) {
    GUI_WINDOW_INFO Wins[GUI_MAX_WINS];
    GUI_RECT_INFO Usable;
    int Count = 0;
    int AllInside = 1;
    int i;

    GuiDesktopUsableGet(&Usable);
    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowRectGet(i, &Wins[Count])) {
            Count++;
        }
    }
    if (Count == 0) {
        gConsole.Write("gui wr: no active window\r\n");
        return;
    }
    for (i = 0; i < Count; i++) {
        if (!GuiPhysInside(&Wins[i].Rect, &Usable)) {
            AllInside = 0;
        }
        GuiWriteWindowLine(i, &Wins[i], &Usable);
    }
    GuiWriteWindowSummary(Wins, Count, &Usable, AllInside);
}

/* 布局自测：开 Files+Settings 再打印矩形；人工验收 `gui wr` 只需开窗后敲命令 */
static void CommandGuiWindowRectsOpen(void) {
    (void)GuiOpenFiles();
    (void)GuiOpenSettings();
    CommandGuiWindowRects();
}

static void CommandGuiBackupProbe(void) {
    char Line[GUI_PROBE_LINE];
    char *P;
    UINT32 Wp = 0;
    UINT32 Hp = 0;
    UINT32 Diff = 0;
    UINT32 Rows = 0;

    if (GuiBackupRoundTripDiff(&Wp, &Hp, &Diff, &Rows) != 0) {
        gConsole.Write("gui backupprobe: no focused window / no backup\r\n");
        return;
    }
    P = GuiPutStr(Line, "gui backupprobe");
    P = GuiPutPair(P, "phys-w", Wp);
    P = GuiPutPair(P, "phys-h", Hp);
    P = GuiPutPair(P, "diff", Diff);
    P = GuiPutPair(P, "rows", Rows);
    P = GuiPutStr(P, Diff ? " judge=BLUR" : " judge=OK");
    *P++ = '\r';
    *P++ = '\n';
    *P = 0;
    gConsole.Write(Line);
}

static void CommandGui(int Argc, char **Argv) {
    if (Argc < 2) {
        gConsole.Write("usage: gui wr | gui wropen | gui backupprobe\n");
        return;
    }
    if (GuiWordEq(Argv[1], "wr")) {
        CommandGuiWindowRects();
        return;
    }
    if (GuiWordEq(Argv[1], "wropen")) {
        CommandGuiWindowRectsOpen();
        return;
    }
    if (GuiWordEq(Argv[1], "backupprobe")) {
        CommandGuiBackupProbe();
        return;
    }
    gConsole.Write("usage: gui wr | gui wropen | gui backupprobe\n");
}

void ShellCommandsGuiProbeRegister(void) {
    gConsole.Register("gui", "gui wr | gui wropen | gui backupprobe",
                     CommandGui);
}
