/*
 * IconBitmapLoad.c — 按 ThemeUiScale 选 bmp32/48/72/96，读 BI_RGB 图标
 *
 * 缩放热切只 DesktopIconsResetDeferred；Worker 下圈 Ensure 重读，避免
 * Gui 点击路径与 Worker FAT 重入。
 */
#include "DesktopPrivate.h"

/* 与最近一次 LoadDesktopIcons 成功选用的边长；0=尚未加载 */
static UINT32 sLoadedIconAssetPixelSize;

UINT32 DesktopIconAssetPixelSize(void) {
    UINT32 Scale = ThemeUiScale();

    if (Scale <= 50u) {
        return 32u;
    }
    if (Scale <= 100u) {
        return 48u;
    }
    if (Scale <= 150u) {
        return 72u;
    }
    return 96u;
}

int DesktopIconsAssetTierMatches(void) {
    if (sLoadedIconAssetPixelSize == 0u) {
        return 1;
    }
    return (sLoadedIconAssetPixelSize == DesktopIconAssetPixelSize()) ? 1 : 0;
}

int PathHasVolPrefix(const char *Path) {
    int i;

    if (!Path) {
        return 0;
    }
    for (i = 0; Path[i] && i < 16; i++) {
        if (Path[i] == ':') {
            return 1;
        }
        if (Path[i] == '/' || Path[i] == '\\') {
            return 0;
        }
    }
    return 0;
}

static const char *BitmapLeafName(const char *Path) {
    const char *Leaf;
    const char *P;

    if (!Path || !Path[0]) {
        return 0;
    }
    Leaf = Path;
    for (P = Path; *P; P++) {
        if (*P == '/' || *P == '\\') {
            Leaf = P + 1;
        }
    }
    return Leaf[0] ? Leaf : 0;
}

/* Out = Assets/Icons/bmpNN/Leaf */
static int BitmapPathBuildTier(char *Out, int OutMax, UINT32 PixelSize,
                               const char *Leaf) {
    char Digits[4];
    int o;
    int i;
    UINT32 N;

    if (!Out || OutMax < 24 || !Leaf || !Leaf[0] || PixelSize == 0u) {
        return 0;
    }
    N = PixelSize;
    i = 0;
    do {
        Digits[i++] = (char)('0' + (N % 10u));
        N /= 10u;
    } while (N && i < 4);
    o = 0;
    {
        const char *Prefix = "Assets/Icons/bmp";
        for (; *Prefix && o < OutMax - 1; Prefix++) {
            Out[o++] = *Prefix;
        }
    }
    while (i > 0 && o < OutMax - 1) {
        Out[o++] = Digits[--i];
    }
    if (o < OutMax - 1) {
        Out[o++] = '/';
    }
    for (; *Leaf && o < OutMax - 1; Leaf++) {
        Out[o++] = *Leaf;
    }
    Out[o] = 0;
    return o > 0;
}

static void BitmapPathWithVolume(const char *Rel, char *Out, int OutMax) {
    int i;

    if (!Rel || !Out || OutMax < 9) {
        return;
    }
    Out[0] = 'B';
    Out[1] = 'L';
    Out[2] = 'O';
    Out[3] = 'C';
    Out[4] = 'K';
    Out[5] = 'S';
    Out[6] = ':';
    for (i = 0; Rel[i] && i < OutMax - 8; i++) {
        Out[7 + i] = Rel[i];
    }
    Out[7 + i] = 0;
}

/*
 * 读 BI_RGB BMP。试：原 Path → 当前档 bmpNN → bmp48 → 扁平 → BLOCKS:…
 */
int LoadBmpPath(const char *Path, BMP_IMAGE *Out, UINT32 FileMax,
                const char *Tag) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    int Err;
    char TierPreferred[192];
    char Tier48[192];
    char Volume[192];
    const char *Leaf;
    const char *TryPath;
    int Pass;
    UINT32 Preferred;

    if (!Path || !Out || FileMax < 54) {
        return 0;
    }
    (void)Tag; /* KERNEL_DEBUG=0 时 DebugWrite 为空，仍保留 Tag 形参供日志 */
    BmpFree(Out);
    Pages = (FileMax + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        DebugWrite(Tag);
        DebugWrite(": alloc failed\n");
        return 0;
    }

    Leaf = BitmapLeafName(Path);
    Preferred = DesktopIconAssetPixelSize();
    TierPreferred[0] = 0;
    Tier48[0] = 0;
    if (Leaf) {
        (void)BitmapPathBuildTier(TierPreferred, (int)sizeof(TierPreferred),
                                  Preferred, Leaf);
        if (Preferred != 48u) {
            (void)BitmapPathBuildTier(Tier48, (int)sizeof(Tier48), 48u, Leaf);
        }
    }

    for (Pass = 0; Pass < 8; Pass++) {
        TryPath = 0;
        if (Pass == 0) {
            TryPath = Path;
        } else if (Pass == 1) {
            TryPath = TierPreferred[0] ? TierPreferred : 0;
        } else if (Pass == 2) {
            TryPath = Tier48[0] ? Tier48 : 0;
        } else if (PathHasVolPrefix(Path)) {
            break;
        } else if (Pass == 3) {
            BitmapPathWithVolume(Path, Volume, (int)sizeof(Volume));
            TryPath = Volume;
        } else if (Pass == 4 && TierPreferred[0]) {
            BitmapPathWithVolume(TierPreferred, Volume, (int)sizeof(Volume));
            TryPath = Volume;
        } else if (Pass == 5 && Tier48[0]) {
            BitmapPathWithVolume(Tier48, Volume, (int)sizeof(Volume));
            TryPath = Volume;
        } else {
            continue;
        }
        if (!TryPath || !TryPath[0]) {
            continue;
        }

        Size = 0;
        Err = FileSystemReadFile(TryPath, Buf, FileMax, &Size);
        if (Err != FAT_OK || Size < 54) {
            continue;
        }
        if (BmpImageDecode(Buf, Size, Out) != 0) {
            DebugWrite(Tag);
            DebugWrite(": decode failed ");
            DebugWrite(TryPath);
            DebugWrite("\n");
            continue;
        }
        PhysicalMemoryFreePages(Buf, Pages);
        DebugWrite(Tag);
        DebugWrite(": loaded ");
        DebugWrite(TryPath);
        DebugWrite("\n");
        return 1;
    }

    PhysicalMemoryFreePages(Buf, Pages);
    DebugWrite(Tag);
    DebugWrite(": missing ");
    DebugWrite(Path);
    DebugWrite(" (solid fallback)\n");
    return 0;
}

void LoadDesktopIcons(void) {
    int i;
    int Ok = 0;
    static const char *const SysTags[DESKTOP_SYS_ICON_COUNT] = {
        "desktop: shell", "desktop: set", "desktop: files", "desktop: store",
        "desktop: info"
    };
    static const char Hex[] = "0123456789abcdef";

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        const char *Tag;

        gIcons[i].BmpReady = 0;
        if (!gIcons[i].Present || !gIcons[i].BmpPath) {
            continue;
        }
        if (i < DESKTOP_SYS_ICON_COUNT) {
            Tag = SysTags[i];
        } else {
            Tag = "desktop: app";
        }
        gIcons[i].BmpReady = LoadBmpPath(gIcons[i].BmpPath, &gIcons[i].Bmp,
                                         ICON_FILE_MAX, Tag);
        if (gIcons[i].BmpReady) {
            Ok++;
        }
    }
    /*
     * 开始菜单资产：START/POWER/REBOOT/GAME 为既定设计（九宫格钮见
     * DrawStartMark；菜单行用下列 BMP）。勿与桌面 SHELL/SET… 新风格混改。
     */
    gStartBmpReady = LoadBmpPath("Assets/Icons/START.BMP", &gStartBmp,
                                 ICON_FILE_MAX, "desktop: start");
    gPowerBmpReady = LoadBmpPath("Assets/Icons/POWER.BMP", &gPowerBmp,
                                 ICON_FILE_MAX, "desktop: power");
    gRebootBmpReady = LoadBmpPath("Assets/Icons/REBOOT.BMP", &gRebootBmp,
                                  ICON_FILE_MAX, "desktop: reboot");
    gGameBmpReady = LoadBmpPath("Assets/Icons/GAME.BMP", &gGameBmp,
                                ICON_FILE_MAX, "desktop: game");
    DesktopLoadMenuSysIcons();
    if (gStartBmpReady) {
        Ok++;
    }
    sLoadedIconAssetPixelSize = DesktopIconAssetPixelSize();
    {
        char Msg[24];
        Msg[0] = 'G';
        Msg[1] = 'u';
        Msg[2] = 'i';
        Msg[3] = ':';
        Msg[4] = ' ';
        Msg[5] = 'I';
        Msg[6] = 'c';
        Msg[7] = 'o';
        Msg[8] = 'n';
        Msg[9] = 'B';
        Msg[10] = 'm';
        Msg[11] = 'p';
        Msg[12] = '=';
        Msg[13] = Hex[(Ok >> 4) & 15];
        Msg[14] = Hex[Ok & 15];
        Msg[15] = '\n';
        Msg[16] = 0;
        LogGui(Msg);
    }
}
