/*
 * Module.c — 按表顺序启动各个子系统
 *
 * 【初学者】
 *   for each module in table:
 *       print "[Mod] Name"
 *       if Init() != 0: print Failed; return -1
 *   return 0
 *
 * 串口上看见 `[Mod] Serial` 就说明跑到这一格了。
 *
 * 【积木】本文件是胶水；不要在这里写「Memory 用哪种算法」。
 */
#include "Module.h"
#include "HalSerial.h"

static void ModLog(const char *Name, const char *Suffix) {
    HalSerialWrite("[Mod] ");
    HalSerialWrite(Name);
    HalSerialWrite(Suffix);
}

int ModulesRun(const MODULE *List, int Count) {
    int i;

    if (List == 0 || Count <= 0) {
        return -1;
    }
    for (i = 0; i < Count; i++) {
        ModLog(List[i].Name, "\n");
        if (List[i].Init == 0 || List[i].Init() != 0) {
            ModLog(List[i].Name, " Failed\n");
            return -1;
        }
    }
    return 0;
}
