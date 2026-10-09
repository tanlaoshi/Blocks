/*
 * Locale.h — K28：UI 字符串表 + lang en|zh（对标现网 I18N2 最小）
 *
 * 不做：落盘 TOYOS.DB、Assets 外置文案、翻译平台。
 */
#ifndef LOCALE_H
#define LOCALE_H

#include "BootTypes.h"

typedef enum {
    LOC_LANG_EN = 0,
    LOC_LANG_ZH = 1
} LOC_LANG;

typedef enum {
    MSG_DESKTOP_TITLE = 0, /* 顶栏：Blocks / 积木 */
    MSG_READY,             /* 客户区：System ready / 系统已就绪 */
    MSG_WIN_SHELL,         /* 窗标题：Shell / 命令窗 */
    MSG_WIN_ABOUT,         /* 窗标题：About / 说明 */
    MSG_ABOUT_BODY,        /* About 客户区一行说明 */
    MSG_ICON_SHELL,        /* 桌面图标：Shell / 命令 */
    MSG_WIN_SETTINGS,      /* 窗标题：Settings / 设置 */
    MSG_ICON_SETTINGS,     /* 桌面图标：Settings / 设置 */
    MSG_SETTINGS_HINT,     /* Settings 提示：点色块改主题 */
    MSG_LANG_USAGE,
    MSG_LANG_NOW,
    MSG_LANG_SET,
    MSG_COUNT
} LOC_MSG;

void LocaleInitialize(void);
LOC_LANG LocaleGet(void);
/* 0=ok；切换后由调用方重绘 Gui/Console 标签 */
int LocaleSet(LOC_LANG Lang);
const char *LocStr(LOC_MSG Id);

#endif
