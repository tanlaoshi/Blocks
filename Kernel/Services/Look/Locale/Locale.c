/*
 * Locale.c — PR-S3-locale-1：语言状态与对外 API
 *
 * 表见 LocaleTable.c；catalog 解析见 LocaleParse.c。
 */
#include "LocalePrivate.h"
#include "Db.h"
#include "Desktop.h"
#include "Font.h"
#include "Gui.h"
#include "Debug.h"

LOCALE_LANGUAGE gLang = LOCALE_LANGUAGE_ZH; /* 默认汉语；切英文便宜 */
char gEn[MESSAGE_COUNT][LOCALE_STR_MAX];
char gZh[MESSAGE_COUNT][LOCALE_STR_MAX];

/* 代码、当前文案、内建回落。en、zh 在前，和 LOCALE_LANGUAGE_EN / LOCALE_LANGUAGE_ZH 的序号一致。 */
#define LOCALE_EXTRA_MAX 4
#define LOCALE_CODE_MAX  8

typedef struct {
    const char *Code;
    char (*Live)[LOCALE_STR_MAX];
    const char *const *Builtin;
} LOCALE_ENTRY;

static char gExtra[LOCALE_EXTRA_MAX][MESSAGE_COUNT][LOCALE_STR_MAX];
static char gExtraCode[LOCALE_EXTRA_MAX][LOCALE_CODE_MAX];
static LOCALE_ENTRY gLocale[2 + LOCALE_EXTRA_MAX];
static int gLocaleCount;

void LocaleRegistryReset(void) {
    gLocale[0].Code = "en";
    gLocale[0].Live = gEn;
    gLocale[0].Builtin = gEnFallback;
    gLocale[1].Code = "zh";
    gLocale[1].Live = gZh;
    gLocale[1].Builtin = gZhFallback;
    gLocaleCount = 2;
}

static int CodeEqual(const char *Code, const char *Value) {
    UINTN i;

    if (Code == 0 || Value == 0) {
        return 0;
    }
    for (i = 0; Code[i] != 0 && Value[i] != 0; i++) {
        if (Code[i] != Value[i]) {
            return 0;
        }
    }
    return Code[i] == 0 && Value[i] == 0;
}

static int FindCode(const char *Code) {
    int i;

    for (i = 0; i < gLocaleCount; i++) {
        if (CodeEqual(gLocale[i].Code, Code)) {
            return i;
        }
    }
    return -1;
}

int LocaleOffer(const char *Code) {
    int Found;
    int Extra;
    int n;
    int i;
    char c;

    Found = FindCode(Code);
    if (Found >= 0) {
        return Found;
    }
    if (Code == 0 || gLocaleCount >= 2 + LOCALE_EXTRA_MAX) {
        return -1;
    }
    n = 0;
    while (Code[n] != 0) {
        c = Code[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return -1;
        }
        if (n + 1 >= LOCALE_CODE_MAX) {
            return -1;
        }
        n++;
    }
    if (n == 0) {
        return -1;
    }
    Extra = gLocaleCount - 2;
    for (i = 0; i <= n; i++) {
        gExtraCode[Extra][i] = Code[i];
    }
    for (i = 0; i < (int)MESSAGE_COUNT; i++) {
        gExtra[Extra][i][0] = 0;
    }
    gLocale[gLocaleCount].Code = gExtraCode[Extra];
    gLocale[gLocaleCount].Live = gExtra[Extra];
    gLocale[gLocaleCount].Builtin = 0;
    gLocaleCount++;
    return gLocaleCount - 1;
}

void LocaleDropLast(int Index) {
    if (Index >= 2 && Index == gLocaleCount - 1) {
        gLocaleCount--;
    }
}

char (*LocaleLive(int Index))[LOCALE_STR_MAX] {
    if (Index < 0 || Index >= gLocaleCount) {
        return 0;
    }
    return gLocale[Index].Live;
}

/* Worker 分片预热当前语言。开机只同步桌面关键文案，其余 Request。 */
static UINT32 gTtfPreheatIdx;
static int gTtfPreheatWant;

static char (*CurrentStrings(void))[LOCALE_STR_MAX] {
    if ((int)gLang < 0 || (int)gLang >= gLocaleCount) {
        return gZh;
    }
    return gLocale[gLang].Live;
}

void LocaleTtfPreheatUi(void) {
    UINT32 i;
    UINT32 BlobSize = 0;
    char (*Strings)[LOCALE_STR_MAX] = CurrentStrings();

    /* Console 早于 Gui/TTF：无 blob 勿预热（避免缓存灌 MISS） */
    if (!FontTtfBlob(&BlobSize) || BlobSize < 12u) {
        return;
    }
    for (i = 0; i < (UINT32)MESSAGE_COUNT; i++) {
        FontTtfPreheatUtf8(Strings[i]);
    }
}

/* gui-ttf-preheat-defer：首屏 Start/图标/设置侧栏同步栅，避免进桌面 defer 空白 */
static void LocaleTtfPreheatCritical(void) {
    static const MESSAGE_ID Crit[] = {
        MESSAGE_START,
        MESSAGE_ICON_SHELL,
        MESSAGE_ICON_SETTINGS,
        MESSAGE_ICON_FILES,
        MESSAGE_ICON_STORE,
        MESSAGE_ICON_DEVICES,
        MESSAGE_ICON_APPS,
        MESSAGE_ICON_GAME,
        MESSAGE_ICON_SNAKE,
        MESSAGE_ICON_SHUTDOWN,
        MESSAGE_ICON_REBOOT,
        MESSAGE_MENU_EMPTY,
        MESSAGE_SET_TITLE,
        MESSAGE_SET_DESKTOP_BG,
        MESSAGE_SET_SHELL_BG,
        MESSAGE_SET_FONT,
        MESSAGE_SET_DISPLAY,
        MESSAGE_SET_LANGUAGE,
        MESSAGE_SET_SCALE,
        MESSAGE_SET_THEME,
        MESSAGE_SET_EFFECTS,
        MESSAGE_SET_HOTSEND,
        /* 失根大窗：盘没了禁再 Preheat；须开机灌格，否则只剩英文 ASCII */
        MESSAGE_BOOT_GONE_TITLE,
        MESSAGE_BOOT_GONE_BODY,
        MESSAGE_KSEND_UPDATING_TITLE,
        MESSAGE_KSEND_UPDATING_BODY,
    };
    UINT32 i;
    UINT32 BlobSize = 0;
    char (*Strings)[LOCALE_STR_MAX] = CurrentStrings();

    if (!FontTtfBlob(&BlobSize) || BlobSize < 12u) {
        return;
    }
    for (i = 0; i < sizeof(Crit) / sizeof(Crit[0]); i++) {
        UINT32 Id = (UINT32)Crit[i];

        if (Id < (UINT32)MESSAGE_COUNT) {
            FontTtfPreheatUtf8(Strings[Id]);
        }
    }
}

void LocaleTtfPreheatRequest(void) {
    gTtfPreheatIdx = 0;
    gTtfPreheatWant = 1;
}

int LocaleTtfPreheatStep(void) {
    if (!gTtfPreheatWant) {
        return 0;
    }
    if (gTtfPreheatIdx >= (UINT32)MESSAGE_COUNT) {
        gTtfPreheatWant = 0;
        return 0;
    }
    FontTtfPreheatUtf8(CurrentStrings()[gTtfPreheatIdx]);
    gTtfPreheatIdx++;
    if (gTtfPreheatIdx >= (UINT32)MESSAGE_COUNT) {
        gTtfPreheatWant = 0;
        /* 整批静默预热完：一次合成，补上仍 defer 的中文 */
        FontTtfBumpPaint();
        return 0;
    }
    return 1;
}

void LocaleInitialize(void) {
    char Val[DB_VAL_MAX];

    LocaleLoadCatalogs();
    /* 缺省 / 表里没有的值 → 汉语 */
    gLang = LOCALE_LANGUAGE_ZH;
    if (DbGet("lang", Val, sizeof(Val)) == DB_OK) {
        int Found = FindCode(Val);
        if (Found >= 0) {
            gLang = (LOCALE_LANGUAGE)Found;
        }
    }
    DebugWrite("locale: ");
    DebugWrite(gLocale[gLang].Code);
    DebugWrite("\n");
    /*
     * gui-ttf-preheat-defer：FT 已在 GuiInitialize。首屏只栅关键文案，
     * 其余 Worker PreheatStep（Reveal 后），勿开机全量 MESSAGE_COUNT。
     */
    LocaleTtfPreheatCritical();
    LocaleTtfPreheatRequest();
}

void LocaleReload(void) {
    char Prev[LOCALE_CODE_MAX];
    const char *Code = LocaleCode();
    int i;
    int Found;

    for (i = 0; i + 1 < LOCALE_CODE_MAX && Code[i] != 0; i++) {
        Prev[i] = Code[i];
    }
    Prev[i] = 0;
    LocaleLoadCatalogs();
    Found = FindCode(Prev);
    gLang = (Found >= 0) ? (LOCALE_LANGUAGE)Found : LOCALE_LANGUAGE_ZH;
    LocaleTtfPreheatUi();
    LocaleApplyUi();
}

LOCALE_LANGUAGE LocaleGet(void) {
    return gLang;
}

const char *LocaleCode(void) {
    if ((int)gLang < 0 || (int)gLang >= gLocaleCount) {
        return "zh";
    }
    return gLocale[gLang].Code;
}

int LocaleCount(void) {
    return gLocaleCount;
}

const char *LocaleCodeAt(int Index) {
    if (Index < 0 || Index >= gLocaleCount || gLocale[Index].Code == 0) {
        return "";
    }
    return gLocale[Index].Code;
}

int LocaleIndex(void) {
    if ((int)gLang < 0 || (int)gLang >= gLocaleCount) {
        return 0;
    }
    return (int)gLang;
}

int LocaleSetCode(const char *Code) {
    int Found = FindCode(Code);

    if (Found < 0) {
        return -1;
    }
    (void)LocaleSet((LOCALE_LANGUAGE)Found);
    return 0;
}

static int gLangDbFlushWant;

int LocaleDbFlushStep(void) {
    if (!gLangDbFlushWant) {
        return 0;
    }
    gLangDbFlushWant = 0;
    (void)DbEndBatch();
    return 0;
}

int LocaleSet(LOCALE_LANGUAGE Lang) {
    int Rc;

    if ((int)Lang < 0 || (int)Lang >= gLocaleCount) {
        return -1;
    }
    gLang = Lang;
    /* 写盘丢 Worker：Settings 点击路径 DbSave 会卡死鼠标 */
    DbBeginBatch();
    Rc = DbSet("lang", gLocale[Lang].Code);
    gLangDbFlushWant = 1;
    /* 切到哪种语言，就排队那种语言里的非 ASCII。点击路径不在这里同步栅格。 */
    LocaleTtfPreheatRequest();
    LocaleApplyUi();
    return Rc == DB_OK ? 0 : -1;
}

const char *LocaleString(MESSAGE_ID Id) {
    const char *Text;

    if ((UINT32)Id >= (UINT32)MESSAGE_COUNT) {
        return "";
    }
    Text = gLocale[gLang].Live[Id];
    if (Text[0] == 0) {
        Text = gLocale[LOCALE_LANGUAGE_EN].Builtin[Id];
    }
    return Text != 0 ? Text : "";
}

void LocaleApplyUi(void) {
    DesktopRefreshLabels();
    GuiRefreshTitles();
}
