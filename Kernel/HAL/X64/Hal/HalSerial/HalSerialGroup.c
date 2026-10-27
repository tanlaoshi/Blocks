/*
 * HalSerialGroup.c — 行首前缀分组；组切换插一空行
 *
 * 存储/MSC（block: / FS: / Boot: MSC）同组，挂盘段不拆空行。
 */
#include "HalSerialPrivate.h"

static int gLogAtBol = 1;
static int gLogGroup;

#define LOG_GROUP_NONE 0
#define LOG_GROUP_STORAGE 1
#define LOG_GROUP_BOOT 2
#define LOG_GROUP_MODULE 3
#define LOG_GROUP_BANNER 4

static int TextStartsWith(const char *Text, const char *Prefix) {
    while (*Prefix) {
        if (*Text++ != *Prefix++) {
            return 0;
        }
    }
    return 1;
}

static int LogPrefixGroup(const char *Text) {
    if (!Text || Text[0] == 0 || Text[0] == '\n' || Text[0] == '\r') {
        return LOG_GROUP_NONE;
    }
    if (TextStartsWith(Text, "block:") ||
        TextStartsWith(Text, "block-mux:") ||
        TextStartsWith(Text, "FS:") ||
        TextStartsWith(Text, "Boot: MSC")) {
        return LOG_GROUP_STORAGE;
    }
    if (TextStartsWith(Text, "Boot:")) {
        return LOG_GROUP_BOOT;
    }
    if (TextStartsWith(Text, "***** Module")) {
        return LOG_GROUP_MODULE;
    }
    if (TextStartsWith(Text, "********************") ||
        TextStartsWith(Text, "Blocks Boot") ||
        TextStartsWith(Text, "Blocks Kernel") ||
        TextStartsWith(Text, "COM1 ")) {
        return LOG_GROUP_BANNER;
    }
    return LOG_GROUP_NONE;
}

int HalSerialGroupShouldBreak(const char *Text) {
    int Group;

    if (!gLogAtBol || !Text || !Text[0]) {
        return 0;
    }
    Group = LogPrefixGroup(Text);
    if (Group == LOG_GROUP_NONE) {
        return 0;
    }
    if (gLogGroup != LOG_GROUP_NONE && Group != gLogGroup) {
        gLogGroup = Group;
        return 1;
    }
    gLogGroup = Group;
    return 0;
}

void HalSerialGroupNoteBol(const char *Text) {
    const char *P;

    if (!Text || !Text[0]) {
        return;
    }
    P = Text;
    while (*P) {
        P++;
    }
    /* \n 或裸 \r（部分终端拷贝）都算行结束 */
    gLogAtBol = (P > Text && (P[-1] == '\n' || P[-1] == '\r')) ? 1 : 0;
}
