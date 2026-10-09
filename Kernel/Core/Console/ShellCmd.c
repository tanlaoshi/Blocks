/*
 * ShellCmd.c — 命令表（K22–K25；系统类见 ShellSys）
 */
#include "ShellCmd.h"
#include "ShellSys.h"
#include "FatFile.h"
#include "HalSerial.h"
#include "Process.h"
#include "Locale.h"
#include "Gui.h"
#include "Console.h"
#include "Theme.h"
#include "Network.h"

#define ARG_MAX   8
#define NAME_MAX  16
#define HELP_MAX  40
#define CMD_MAX   20

typedef struct {
    char Name[NAME_MAX];
    char Help[HELP_MAX];
    SHELL_CMD_FN Fn;
} SHELL_CMD;

static SHELL_CMD gCmds[CMD_MAX];
static int gCmdN;
static char gArgBuf[120];
static char *gArgv[ARG_MAX];

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static int StrEq(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void StrCopyCap(char *Dst, const char *Src, int Cap) {
    int i;
    if (Dst == 0 || Cap <= 0) {
        return;
    }
    if (Src == 0) {
        Dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Cap && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

int ShellCmdRegister(const char *Name, const char *Help, SHELL_CMD_FN Fn) {
    if (gCmdN >= CMD_MAX || Name == 0 || Fn == 0) {
        return -1;
    }
    StrCopyCap(gCmds[gCmdN].Name, Name, NAME_MAX);
    StrCopyCap(gCmds[gCmdN].Help, Help ? Help : "", HELP_MAX);
    gCmds[gCmdN].Fn = Fn;
    gCmdN++;
    return 0;
}

/* 就地分词：Line 拷进 gArgBuf */
static int SplitArgs(const char *Line) {
    int N = 0;
    int i = 0;
    int In = 0;

    if (Line == 0) {
        return 0;
    }
    while (Line[i] && i + 1 < (int)sizeof(gArgBuf)) {
        gArgBuf[i] = Line[i];
        i++;
    }
    gArgBuf[i] = 0;

    i = 0;
    while (gArgBuf[i]) {
        while (gArgBuf[i] == ' ' || gArgBuf[i] == '\t') {
            gArgBuf[i++] = 0;
        }
        if (gArgBuf[i] == 0) {
            break;
        }
        if (N >= ARG_MAX) {
            break;
        }
        gArgv[N++] = &gArgBuf[i];
        In = 1;
        while (gArgBuf[i] && gArgBuf[i] != ' ' && gArgBuf[i] != '\t') {
            i++;
        }
        if (gArgBuf[i] == 0) {
            break;
        }
        gArgBuf[i++] = 0;
        (void)In;
    }
    return N;
}

static void CmdHelp(int Argc, char **Argv) {
    int i;
    (void)Argc;
    (void)Argv;
    Put("commands:\n");
    for (i = 0; i < gCmdN; i++) {
        Put("  ");
        Put(gCmds[i].Name);
        Put("  ");
        Put(gCmds[i].Help);
        Put("\n");
    }
}

static void CmdClear(int Argc, char **Argv) {
    int i;
    (void)Argc;
    (void)Argv;
    for (i = 0; i < 24; i++) {
        Put("\n");
    }
}

static void CmdEcho(int Argc, char **Argv) {
    int i;
    for (i = 1; i < Argc; i++) {
        if (i > 1) {
            Put(" ");
        }
        Put(Argv[i]);
    }
    Put("\n");
}


static void PutHex32(UINT32 V) {
    char Hex[12];
    HalSerialFormatHex(Hex, V, 8);
    Put(Hex);
}

static void CmdLs(int Argc, char **Argv) {
    FAT_DIR_ENT Ents[64];
    UINT32 N = 0;
    UINT32 i;
    (void)Argc;
    (void)Argv;
    if (FatDirListRoot(Ents, 64, &N) != 0) {
        Put("ls: fail\n");
        return;
    }
    for (i = 0; i < N; i++) {
        Put(Ents[i].IsDir ? "d " : "- ");
        PutHex32(Ents[i].Size);
        Put("  ");
        Put(Ents[i].Name);
        Put("\n");
    }
}

static void CmdCat(int Argc, char **Argv) {
    UINT8 Buf[2048];
    UINT32 Size = 0;
    int N;
    UINT32 i;
    if (Argc < 2) {
        Put("cat: need file\n");
        return;
    }
    N = FatFileReadPath(Argv[1], Buf, sizeof(Buf) - 1u, &Size);
    if (N < 0) {
        Put("cat: fail\n");
        return;
    }
    for (i = 0; i < Size; i++) {
        char One[2];
        UINT8 C = Buf[i];
        if (C == 0) {
            break;
        }
        if (C == '\n' || C == '\r' || (C >= 0x20u && C <= 0x7eu)) {
            One[0] = (char)C;
            One[1] = 0;
            Put(One);
        } else {
            Put(".");
        }
    }
    if (Size == 0 || Buf[Size - 1] != '\n') {
        Put("\n");
    }
}


static void CmdWrite(int Argc, char **Argv) {
    char Body[512];
    int o = 0;
    int i, j;
    if (Argc < 2) {
        Put("write: need name [text]\n");
        return;
    }
    for (i = 2; i < Argc; i++) {
        if (i > 2 && o + 1 < (int)sizeof(Body)) {
            Body[o++] = ' ';
        }
        for (j = 0; Argv[i][j] && o + 1 < (int)sizeof(Body); j++) {
            Body[o++] = Argv[i][j];
        }
    }
    Body[o] = 0;
    if (FatFileWritePath(Argv[1], Body, (UINT32)o) != 0) {
        Put("write: fail\n");
        return;
    }
    Put("write: ok\n");
}

static void CmdMkdir(int Argc, char **Argv) {
    if (Argc < 2) {
        Put("mkdir: need name\n");
        return;
    }
    if (FatMkdirPath(Argv[1]) != 0) {
        Put("mkdir: fail\n");
        return;
    }
    Put("mkdir: ok\n");
}

static void CmdRm(int Argc, char **Argv) {
    if (Argc < 2) {
        Put("rm: need name\n");
        return;
    }
    if (FatRmPath(Argv[1]) != 0) {
        Put("rm: fail\n");
        return;
    }
    Put("rm: ok\n");
}

static void PutU32(UINT32 V) {
    char T[12];
    int N = 0;
    int i;

    if (V == 0) {
        Put("0");
        return;
    }
    while (V != 0 && N < 11) {
        T[N++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    for (i = N - 1; i >= 0; i--) {
        char One[2];
        One[0] = T[i];
        One[1] = 0;
        Put(One);
    }
}

/* mode — 查/写分辨率偏好（下次 Boot 生效；见 Boot VideoTheme） */
static void CmdMode(int Argc, char **Argv) {
    UINT32 W = 0;
    UINT32 H = 0;
    const char *S;
    UINT32 Ww = 0;
    UINT32 Hh = 0;

    ThemeGetMode(&W, &H);
    if (Argc < 2) {
        Put("mode: ");
        if (W == 0 || H == 0) {
            Put("auto");
        } else {
            PutU32(W);
            Put("x");
            PutU32(H);
        }
        Put(" (next boot; ThemeSave keeps it)\n");
        return;
    }
    S = Argv[1];
    if (StrEq(S, "auto")) {
        ThemeSetMode(0, 0);
        if (ThemeSaveCfg() != 0) {
            Put("mode: save fail\n");
            return;
        }
        Put("mode: auto (saved)\n");
        return;
    }
    while (*S >= '0' && *S <= '9') {
        Ww = Ww * 10u + (UINT32)(*S - '0');
        S++;
        if (Ww > 10000u) {
            Put("mode: bad (use 1024x768|auto)\n");
            return;
        }
    }
    if (*S != 'x' && *S != 'X') {
        Put("mode: bad (use 1024x768|auto)\n");
        return;
    }
    S++;
    while (*S >= '0' && *S <= '9') {
        Hh = Hh * 10u + (UINT32)(*S - '0');
        S++;
        if (Hh > 10000u) {
            Put("mode: bad (use 1024x768|auto)\n");
            return;
        }
    }
    if (*S != 0 || Ww < 640u || Hh < 480u) {
        Put("mode: bad (min 640x480)\n");
        return;
    }
    ThemeSetMode(Ww, Hh);
    if (ThemeSaveCfg() != 0) {
        Put("mode: save fail\n");
        return;
    }
    Put("mode: saved ");
    PutU32(Ww);
    Put("x");
    PutU32(Hh);
    Put(" (reboot to apply)\n");
}

static void CmdLang(int Argc, char **Argv) {
    if (Argc < 2) {
        Put(LocStr(MSG_LANG_USAGE));
        Put("\n");
        Put(LocStr(MSG_LANG_NOW));
        return;
    }
    if (StrEq(Argv[1], "en")) {
        (void)LocaleSet(LOC_LANG_EN);
        GuiRefreshLabels();
        ConsoleRefreshBanner();
        Put(LocStr(MSG_LANG_SET));
        return;
    }
    if (StrEq(Argv[1], "zh")) {
        (void)LocaleSet(LOC_LANG_ZH);
        GuiRefreshLabels();
        ConsoleRefreshBanner();
        Put(LocStr(MSG_LANG_SET));
        return;
    }
    Put(LocStr(MSG_LANG_USAGE));
    Put("\n");
}

static void CmdHello(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    (void)ProcessRunHello();
}

/* ping — ICMP echo；默认 10.0.2.2（QEMU 网关） */
static void CmdPing(int Argc, char **Argv) {
    const char *Host = "10.0.2.2";
    int Rc;

    if (!NetworkNicReady()) {
        Put("ping: no nic\n");
        return;
    }
    if (Argc >= 2) {
        Host = Argv[1];
    }
    Put("ping ");
    Put(Host);
    Put(" ...\n");
    Rc = NetworkPing(Host, 3000);
    if (Rc == 0) {
        Put("ping: ok\n");
        return;
    }
    Put("ping: fail ");
    if (Rc == -1) {
        Put("(net down)\n");
    } else if (Rc == -2) {
        Put("(bad ip)\n");
    } else if (Rc == -3) {
        Put("(arp)\n");
    } else if (Rc == -4) {
        Put("(tx)\n");
    } else if (Rc == -5) {
        Put("(timeout)\n");
    } else {
        PutU32((UINT32)(-Rc));
        Put("\n");
    }
}

void ShellCmdInitialize(void) {
    gCmdN = 0;
    ShellCmdRegister("help", "list commands", CmdHelp);
    ShellCmdRegister("clear", "clear screen", CmdClear);
    ShellCmdRegister("echo", "print arguments", CmdEcho);
    ShellCmdRegister("hello", "run HELLO.ELF", CmdHello);
    ShellCmdRegister("ls", "list root dir", CmdLs);
    ShellCmdRegister("cat", "print text file", CmdCat);
    ShellCmdRegister("write", "write text file", CmdWrite);
    ShellCmdRegister("mkdir", "make directory", CmdMkdir);
    ShellCmdRegister("rm", "remove file/dir", CmdRm);
    ShellCmdRegister("lang", "UI language en|zh", CmdLang);
    ShellCmdRegister("mode", "display pref WxH|auto", CmdMode);
    ShellCmdRegister("ping", "ICMP echo (default 10.0.2.2)", CmdPing);
    ShellSysRegister();
    Put("Shell: cmds ok\n");
}

void ShellCmdRunLine(const char *Line) {
    int Argc;
    int i;

    Argc = SplitArgs(Line);
    if (Argc <= 0) {
        return;
    }
    for (i = 0; i < gCmdN; i++) {
        if (StrEq(gCmds[i].Name, gArgv[0])) {
            gCmds[i].Fn(Argc, gArgv);
            return;
        }
    }
    Put("unknown: ");
    Put(gArgv[0]);
    Put(" (try help)\n");
}
