/*
 * KernelModules.c — 开机模块表（积木拼表）
 *
 * 【初学者】
 * 每一行 = 一块要启动的积木：名字 + 初始化函数。
 * 顺序就是依赖顺序（例如 Memory 必须在 VirtualMemory 前面）。
 *
 * 当前 Full 表只有 Serial。以后往表里加 Memory、Video 等即可。
 * InitializeSerial 很薄：真正干活的是各 Arch 的 HalSerialInitialize()。
 */
#include "KernelModules.h"
#include "Module.h"
#include "HalSerial.h"

static int InitializeSerial(void) {
    HalSerialInitialize();
    return 0;
}

static const MODULE gModulesFull[] = {
    { "Serial", InitializeSerial },
};

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

int KernelModulesRunFull(void) {
    return ModulesRun(gModulesFull, MODULE_COUNT(gModulesFull));
}
