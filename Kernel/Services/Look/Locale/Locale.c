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

LOC_LANG gLang = LOC_LANG_ZH; /* 默认汉语；切英文便宜 */
char gEn[MSG_COUNT][LOCALE_STR_MAX];
char gZh[MSG_COUNT][LOCALE_STR_MAX];

/* 代码、当前文案、内建回落。en、zh 在前，和 LOC_LANG_EN / LOC_LANG_ZH 的序号一致。 */
#define LOCALE_EXTRA_MAX 4
#define LOCALE_CODE_MAX  8

typedef struct {
    const char *Code;
    char (*Live)[LOCALE_STR_MAX];
    const char *const *Builtin;
} LOCALE_ENTRY;

static char gExtra[LOCALE_EXTRA_MAX][MSG_COUNT][LOCALE_STR_MAX];
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
    for (i = 0; i < (int)MSG_COUNT; i++) {
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

/* Worker 补预热当前语言。开机同步扫过一遍；切语言后再排队。 */
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
    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        FontTtfPreheatUtf8(Strings[i]);
    }
}

static void LocaleTtfPreheatRequest(void) {
    gTtfPreheatIdx = 0;
    gTtfPreheatWant = 1;
}

int LocaleTtfPreheatStep(void) {
    if (!gTtfPreheatWant) {
        return 0;
    }
    if (gTtfPreheatIdx >= (UINT32)MSG_COUNT) {
        gTtfPreheatWant = 0;
        return 0;
    }
    FontTtfPreheatUtf8(CurrentStrings()[gTtfPreheatIdx]);
    gTtfPreheatIdx++;
    if (gTtfPreheatIdx >= (UINT32)MSG_COUNT) {
        gTtfPreheatWant = 0;
        /* 整批静默预热完：一次合成，换掉仍留在屏上的点阵 */
        FontTtfBumpPaint();
        return 0;
    }
    return 1;
}

void LocaleInitialize(void) {
    char Val[DB_VAL_MAX];

    LocaleLoadCatalogs();
    /* 缺省 / 表里没有的值 → 汉语 */
    gLang = LOC_LANG_ZH;
    if (DbGet("lang", Val, sizeof(Val)) == DB_OK) {
        int Found = FindCode(Val);
        if (Found >= 0) {
            gLang = (LOC_LANG)Found;
        }
    }
    DebugWrite("locale: ");
    DebugWrite(gLocale[gLang].Code);
    DebugWrite("\n");
    /* 开机同步预热中文目录（FontTtfInit 已完成）；求 UI 基本无点阵 */
    LocaleTtfPreheatUi();
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
    gLang = (Found >= 0) ? (LOC_LANG)Found : LOC_LANG_ZH;
    LocaleTtfPreheatUi();
    LocaleApplyUi();
}

LOC_LANG LocaleGet(void) {
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
    (void)LocaleSet((LOC_LANG)Found);
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

int LocaleSet(LOC_LANG Lang) {
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

const char *LocStr(MSG_ID Id) {
    const char *Text;

    if ((UINT32)Id >= (UINT32)MSG_COUNT) {
        return "";
    }
    Text = gLocale[gLang].Live[Id];
    if (Text[0] == 0) {
        Text = gLocale[LOC_LANG_EN].Builtin[Id];
    }
    return Text != 0 ? Text : "";
}

void LocaleApplyUi(void) {
    DesktopRefreshLabels();
    GuiRefreshTitles();
}
