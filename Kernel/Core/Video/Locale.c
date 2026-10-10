/*
 * Locale.c — K28：内建 en|zh 表；缺省 en（lang zh 后见中文）
 *
 * 【初学者】
 * - Core/Video：Gui 字符串 MSG_* 多语言；Shell lang 命令改 gLang。
 * - 入口：LocaleInitialize / LocaleGet / LocaleSet / LocStr。
 * - 边界：不读磁盘 locale 文件。
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
    "Sel+New/Del; ELF run.",
    "Store",
    "Store",
    "Select + Install.",
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
    "选中后 New/Del；ELF 运行。",
    "商店",
    "商店",
    "选包后点 Install。",
    "开始",
    "lang [en|zh]",
    "lang: zh\n",
    "lang: set zh\n",
};

/*
 * LocaleInitialize — 内建 en/zh 表就绪
 *
 * 做什么：gLang=en，打 SLOG_GUI 提示；幂等。
 * 谁调用：GuiInitialize、ConsoleRefresh、LocStr/LocaleGet 懒初始化。
 * 前后文：前 — Gui 模块表；后 — LocStr 取 MSG_* 串。
 * 返回：void。
 */
void LocaleInitialize(void) {
    if (gReady) {
        return;
    }
    gLang = LOC_LANG_EN;
    gReady = 1;
    HalSerialWriteChannel(SLOG_GUI, "Locale: en (lang zh to switch)\n");
}

/*
 * LocaleGet — 当前语言枚举
 *
 * 做什么：返回 gLang；未初始化则先 LocaleInitialize。
 * 谁调用：将来 Gui/Shell 读状态；本刀以 LocStr 为主。
 * 前后文：兄弟 — LocaleSet、LocStr。
 * 返回：LOC_LANG_EN 或 LOC_LANG_ZH。
 */
LOC_LANG LocaleGet(void) {
    if (!gReady) {
        LocaleInitialize();
    }
    return gLang;
}

/*
 * LocaleSet — 切换 en/zh
 *
 * 做什么：校验 Lang 后写入 gLang；不刷新已画像素。
 * 谁调用：Shell `lang`（ShellCommand/Theme.c）。
 * 前后文：后 — Gui 重绘或 ConsoleRefresh 才见新文案。
 * 返回：0 成功；-1 非法 Lang。
 */
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

/*
 * LocStr — MSG_* 多语言字符串
 *
 * 做什么：按 gLang 返回 gEn/gZh 表项；越界返回 ""。
 * 谁调用：Gui（WindowPaint、Desktop、Settings、Files、Store、Start）、Console、Shell lang 提示。
 * 前后文：前 — LocaleInitialize；后 — FontDrawStringAt 画到帧缓冲。
 * 返回：静态只读 C 串指针。
 */
const char *LocStr(LOC_MSG Id) {
    if (!gReady) {
        LocaleInitialize();
    }
    if ((UINT32)Id >= (UINT32)MSG_COUNT) {
        return "";
    }
    return (gLang == LOC_LANG_ZH) ? gZh[Id] : gEn[Id];
}
