/*
 * Locale.c — K28：内建 en|zh 表；缺省 en（lang zh 后见中文）
 */
#include "Locale.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static LOC_LANG gLang = LOC_LANG_EN;
static int gReady;

static const char *const gEn[MSG_COUNT] = {
    "Blocks",
    "System ready",
    "Shell",
    "About",
    "Click to raise.",
    "Shell",
    "Settings",
    "Set",
    "Tap a color.",
    "Files",
    "Files",
    "Tap ELF to run.",
    "Start",
    "lang [en|zh]",
    "lang: en\n",
    "lang: set en\n",
};

static const char *const gZh[MSG_COUNT] = {
    "积木",
    "系统已就绪",
    "命令窗",
    "说明",
    "点击抬升。",
    "命令",
    "设置",
    "设置",
    "点色块改主题。",
    "文件",
    "文件",
    "点 ELF 运行。",
    "开始",
    "lang [en|zh]",
    "lang: zh\n",
    "lang: set zh\n",
};

void LocaleInitialize(void) {
    if (gReady) {
        return;
    }
    gLang = LOC_LANG_EN;
    gReady = 1;
    HalSerialWriteChannel(SLOG_GUI, "Locale: en (lang zh to switch)\n");
}

LOC_LANG LocaleGet(void) {
    if (!gReady) {
        LocaleInitialize();
    }
    return gLang;
}

int LocaleSet(LOC_LANG Lang) {
    if (Lang != LOC_LANG_EN && Lang != LOC_LANG_ZH) {
        return -1;
    }
    if (!gReady) {
        LocaleInitialize();
    }
    gLang = Lang;
    return 0;
}

const char *LocStr(LOC_MSG Id) {
    if (!gReady) {
        LocaleInitialize();
    }
    if ((UINT32)Id >= (UINT32)MSG_COUNT) {
        return "";
    }
    return (gLang == LOC_LANG_ZH) ? gZh[Id] : gEn[Id];
}
