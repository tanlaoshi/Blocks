/*
 * ShellCommandsGuiProbe.c — `gui backupprobe`：窗备份物理化自测（PR-23d）
 *
 * 数字判据：diff=0 表示「抓备份 → 从备份贴回」逐物理像素一致，即 150% 下
 * 失焦/拖动重贴不会把文字抹成块；diff>0 的行数就是糊掉的证据。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Gui.h"

#define GUI_PROBE_LINE 128u

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

static void CommandGui(int Argc, char **Argv) {
    char Line[GUI_PROBE_LINE];
    char *P;
    UINT32 Wp = 0;
    UINT32 Hp = 0;
    UINT32 Diff = 0;
    UINT32 Rows = 0;
    int Rc;

    if (Argc < 2 || !GuiWordEq(Argv[1], "backupprobe")) {
        gConsole.Write("usage: gui backupprobe (focused window backup round-trip)\n");
        return;
    }
    Rc = GuiBackupRoundTripDiff(&Wp, &Hp, &Diff, &Rows);
    if (Rc != 0) {
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

void ShellCommandsGuiProbeRegister(void) {
    gConsole.Register("gui", "gui backupprobe (backup round-trip, read-only)",
                     CommandGui);
}
