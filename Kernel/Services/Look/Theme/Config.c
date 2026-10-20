/*
 * Config.c — 读 THEME.CFG 与 ThemeLoad
 * 核心：Theme.c
 */
#include "Db.h"
#include "Theme.h"
#include "ThemePrivate.h"

int ThemeLoadFromCfg(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[64];
    UINTN L;

    if (FileSystemReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK || Size == 0) {
        return -1;
    }
    Buf[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    return 0;
}

/*
 * 旧卷：DB 有其它键但还没有 mode= 时，从 THEME.CFG 迁一次进 DB。
 * 正常路径分辨率只认 BLOCKS.DB。
 */
int ThemeMigrateModeFromCfg(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[64];
    UINTN L;
    int Got = 0;
    const char *Val;
    THEME_SAVE_VALS Vals;

    if (FileSystemReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK || Size == 0) {
        return -1;
    }
    Buf[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                const char *P;

                Line[L] = 0;
                P = Line;
                while (*P && IsSpace(*P)) {
                    P++;
                }
                Val = ValueAfterKey(P, "mode");
                if (Val) {
                    ApplyLine(P);
                    Got = 1;
                }
                L = 0;
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    if (!Got) {
        return -1;
    }
    ThemeSaveFillVals(&Vals);
    if (ThemeHasDisplayPref() && Vals.ModeVal[0]) {
        (void)DbSet("mode", Vals.ModeVal);
        HalConsoleWriteSerial("Theme: migrated mode from THEME.CFG -> BLOCKS.DB\n");
    }
    return 0;
}

int ThemeLoad(void) {
    int FromDb;
    int ModeFromDb;

    FromDb = ApplyDbKey("desktop") + ApplyDbKey("shell") +
             ApplyDbKey("font") + ApplyDbKey("scale") +
             ApplyDbKey("scalesrc") + ApplyDbKey("fade") + ApplyDbKey("wallpaper") +
             ApplyDbKey("theme") + ApplyDbKey("deskgrad") +
             ApplyDbKey("theme.effects");
    ModeFromDb = ApplyDbKey("mode");
    if (FromDb == 0 && ModeFromDb == 0) {
        if (ThemeLoadFromCfg() != 0) {
            return -1;
        }
        /* 库空：CFG 可能带旧 mode=，立刻落到 DB，供 Boot/QEMU */
        if (ThemeHasDisplayPref()) {
            THEME_SAVE_VALS Vals;

            ThemeSaveFillVals(&Vals);
            if (Vals.ModeVal[0]) {
                (void)DbSet("mode", Vals.ModeVal);
            }
        }
        LogGui("Theme: Loaded THEME.CFG\n");
    } else {
        if (ModeFromDb == 0) {
            (void)ThemeMigrateModeFromCfg();
        }
        LogGui("Theme: Loaded ");
        LogGui(DbLoadedName());
        LogGui("\n");
    }
    /*
     * PR-K-log-cont：boot GOP 仍上滚时勿 FontSetById——Gui 加载 Sun 8x16 会中途换字。
     * gFontId 仍按 DB/CFG 算好；进桌面前再套用。
     */
    if (!HalSerialGopMirroring()) {
        (void)FontSetById(gFontId);
        if (gFontId >= FontCount() || FontCurrentId() != gFontId) {
            ThemeClampFontId();
        }
    } else if (gFontId >= FontCount()) {
        gFontId = ThemeCompactFontId();
    }
    gThemeUiScale = NormalizeUiScale(gThemeUiScale);
    /*
     * tech/modern：套色板底色。wallpaper 若 DB/CFG 已有则保留；
     * 无 wallpaper 键时 tech 关壁纸、modern 开壁纸。
     */
    if (gThemeId == THEME_PALETTE_TECH) {
        ThemeTechApplyColors();
        if (!gWallpaperPrefSet) {
            gWallpaper = 0;
        }
    } else if (gThemeId == THEME_PALETTE_MODERN) {
        ThemeModernApplyColors();
        if (!gWallpaperPrefSet) {
            gWallpaper = 1;
        }
        gDesktopGrad = 0;
    }
    /*
     * 真机当前帧缓冲为 4K：若用户未在 Settings 显式选过缩放，默认 200%。
     * 旧盘常有 ThemeSave 写下的 scale=100，不能单靠 gScalePrefSet 判断；
     * 仅 scalesrc=user（或本次 Settings 改缩放）才算用户锁定。
     */
    if (!HalCpuIsHypervisor() && !gScaleUserSet) {
        UINT32 PhysW = 0;
        UINT32 PhysH = 0;
        const BOOT_INFO *Info;

        HalVideoGetPhysicalSize(&PhysW, &PhysH);
        if (PhysW < 640u || PhysH < 480u) {
            Info = BootInfoGet();
            if (Info) {
                PhysW = Info->HorizontalResolution;
                PhysH = Info->VerticalResolution;
            }
        }
        if (PhysW >= 3840u && PhysH >= 2160u) {
            gThemeUiScale = 200;
            DebugWrite("Theme: 4K default scale=200\n");
        }
    }
    DebugWrite("Theme: desktop=");
    DebugHex32(gDesktopBg);
    DebugWrite(" shell=");
    DebugHex32(gShellClientBg);
    DebugWrite(" font=");
    DebugHex32(gFontId);
    DebugWrite(" scale=");
    DebugHex32(gThemeUiScale);
    DebugWrite(" wallpaper=");
    DebugHex32((UINT32)gWallpaper);
    DebugWrite(" theme=");
    DebugWrite(ThemeTechName(gThemeId));
    DebugWrite(" deskgrad=");
    DebugHex32((UINT32)gDesktopGrad);
    if (ThemeHasDisplayPref()) {
        DebugWrite(" mode=");
        DebugHex32(gModeW);
        DebugWrite("x");
        DebugHex32(gModeH);
    }
    DebugWrite("\n");
    return 0;
}
