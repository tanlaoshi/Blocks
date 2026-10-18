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

/* 代码、当前文案、内建回落。en 在前，和 LOC_LANG_EN / LOC_LANG_ZH 的序号一致。 */
typedef struct {
    const char *Code;
    char (*Live)[LOCALE_STR_MAX];
    const char *const *Builtin;
} LOCALE_ENTRY;

static LOCALE_ENTRY gLocale[] = {
    { "en", gEn, gEnFallback },
    { "zh", gZh, gZhFallback },
};
#define LOCALE_COUNT ((int)(sizeof(gLocale) / sizeof(gLocale[0])))

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

    for (i = 0; i < LOCALE_COUNT; i++) {
        if (CodeEqual(gLocale[i].Code, Code)) {
            return i;
        }
    }
    return -1;
}

/* Worker 补预热（开机已同步扫过 gZh；此处用于 reload / 晚到 Want） */
static UINT32 gTtfPreheatIdx;
static int gTtfPreheatWant;

void LocaleTtfPreheatUi(void) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        FontTtfPreheatUtf8(gLocale[LOC_LANG_ZH].Live[i]);
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
    FontTtfPreheatUtf8(gLocale[LOC_LANG_ZH].Live[gTtfPreheatIdx]);
    gTtfPreheatIdx++;
    if (gTtfPreheatIdx >= (UINT32)MSG_COUNT) {
        gTtfPreheatWant = 0;
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
    LocaleLoadCatalogs();
    LocaleTtfPreheatUi();
    LocaleApplyUi();
}

LOC_LANG LocaleGet(void) {
    return gLang;
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

    if ((int)Lang < 0 || (int)Lang >= LOCALE_COUNT) {
        return -1;
    }
    gLang = Lang;
    /* 写盘丢 Worker：Settings 点击路径 DbSave 会卡死鼠标 */
    DbBeginBatch();
    Rc = DbSet("lang", gLocale[Lang].Code);
    gLangDbFlushWant = 1;
    if (CodeEqual(gLocale[Lang].Code, "zh")) {
        LocaleTtfPreheatRequest(); /* Worker 再扫一遍；点击路径不 sync 栅格 */
    }
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
