/*
 * SaveConfig.c — 拼 THEME.CFG 缓冲并写盘（PR-F-theme-1）
 * 核心：Save.c
 */
#include "ThemePrivate.h"
#include "SerialLog.h"

#define THEME_CFG_CAP 320u

static char sLastConfig[THEME_CFG_CAP];
static UINTN sLastCfgN;

static void BufferPut(char *Buf, UINTN *N, UINTN Cap, char C) {
    if (*N + 1 < Cap) {
        Buf[(*N)++] = C;
    }
}

static void BufferPutString(char *Buf, UINTN *N, UINTN Cap, const char *S) {
    int i;

    if (!S) {
        return;
    }
    for (i = 0; S[i]; i++) {
        BufferPut(Buf, N, Cap, S[i]);
    }
}

static void BufferPutKeyString(char *Buf, UINTN *N, UINTN Cap, const char *Key,
                         const char *Val) {
    BufferPutString(Buf, N, Cap, Key);
    BufferPut(Buf, N, Cap, '=');
    BufferPutString(Buf, N, Cap, Val);
    BufferPut(Buf, N, Cap, '\n');
}

static void BufferPutKeyHex(char *Buf, UINTN *N, UINTN Cap, const char *Key,
                         UINT32 Color) {
    char Hex[7];

    PutHex6(Hex, Color);
    Hex[6] = 0;
    BufferPutKeyString(Buf, N, Cap, Key, Hex);
}

UINTN ThemeSaveBuildConfig(char *Buf, UINTN Cap, const THEME_SAVE_VALS *V) {
    UINTN N = 0;

    if (!Buf || Cap < 4 || !V) {
        return 0;
    }
    BufferPutKeyHex(Buf, &N, Cap, "desktop", gDesktopBg);
    BufferPutKeyHex(Buf, &N, Cap, "shell", gShellClientBg);
    BufferPutKeyString(Buf, &N, Cap, "font", V->FontVal);
    /* 分辨率只写 BLOCKS.DB 的 mode=；CFG 不再带 mode，避免双份 */
    BufferPutKeyString(Buf, &N, Cap, "scale", V->ScaleVal);
    if (gScaleUserSet) {
        BufferPutString(Buf, &N, Cap, "scalesrc=user\n");
    }
    BufferPutKeyString(Buf, &N, Cap, "fade", V->FadeVal);
    BufferPutKeyString(Buf, &N, Cap, "wallpaper", V->WallVal);
    BufferPutKeyString(Buf, &N, Cap, "theme", ThemeTechName(gThemeId));
    BufferPutKeyString(Buf, &N, Cap, "deskgrad", V->GradVal);
    BufferPutKeyString(Buf, &N, Cap, "theme.effects",
                 ThemeEffectLevelName(gEffectLevel));
    Buf[N] = 0;
    return N;
}

/*
 * 写 THEME.CFG。返回：0 已写；1 内容未变跳过；-1 失败。
 * 勿 Delete 再 Write（vvfat unlink+create 易丢文件）。
 */
int ThemeSaveWriteConfig(const char *Buf, UINTN N) {
    UINTN i;
    int Same;

    if (!Buf || N == 0 || N >= THEME_CFG_CAP) {
        return -1;
    }
    if (N == sLastCfgN && sLastCfgN != 0) {
        Same = 1;
        for (i = 0; i < N; i++) {
            if (sLastConfig[i] != Buf[i]) {
                Same = 0;
                break;
            }
        }
        if (Same) {
            LogDrv("Theme: already saved (skip)\n");
            return 1;
        }
    }
    if (FileSystemWriteFile(THEME_CFG_PATH, Buf, N) != FAT_OK) {
        /* 勿进串口壳；Pi µSD 现只读时属预期失败 */
        LogDrv("Theme: save THEME.CFG failed\n");
        return -1;
    }
    for (i = 0; i < N && i < sizeof(sLastConfig); i++) {
        sLastConfig[i] = Buf[i];
    }
    sLastCfgN = N;
    return 0;
}
