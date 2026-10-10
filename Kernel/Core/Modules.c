/*
 * Modules.c — 开机模块表 + ModulesRun（只拼表，Init 在各 Core/<模块>/）
 *
 * 【初学者】
 * - Core 编排：KernelMain 调用 ModulesRunFull，按序跑 gModulesFull。
 * - 入口：ModulesRun / ModulesRunFull；各模块 *Initialize 在子目录。
 * - 边界：本文件不写模块逻辑；失败打 [Mod] 日志并返回 -1。
 */
#include "Modules.h"
#include "Module.h"
#include "HalSerial.h"
#include "Serial.h"
#include "Memory.h"
#include "VirtualMemory.h"
#include "Driver.h"
#include "Video.h"
#include "Cpu.h"
#include "Usb.h"
#include "FileSystem.h"
#include "Network.h"
#include "Gui.h"
#include "Scheduler.h"
#include "Console.h"

static void ModLog(const char *Name, const char *Suffix) {
    HalSerialWrite("[Mod] ");
    HalSerialWrite(Name);
    HalSerialWrite(Suffix);
}

/*
 * ModulesRun — 按表顺序调用各模块 Init
 *
 * 做什么：打 [Mod] 日志；任一 Init 非 0 则失败返回。
 * 谁调用：ModulesRunFull；测试可传自定义表。
 * 返回：0 成功；-1 表无效或某模块失败。
 */
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

static const MODULE gModulesFull[] = {
    { "Serial", SerialInitialize },
    { "Memory", MemoryInitialize },
    { "VirtualMemory", VirtualMemoryInitialize },
    { "Driver", DriverInitialize },
    { "Video", VideoInitialize },
    { "Cpu", CpuInitialize },
    { "USB", UsbInitialize },
    { "FileSystem", FileSystemInitialize },
    { "Network", NetworkInitialize },
    { "Gui", GuiInitialize },
    { "Scheduler", SchedulerInitialize },
    { "Console", ConsoleInitialize },
};

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

/*
 * ModulesRunFull — 完整开机模块链
 *
 * 做什么：跑 gModulesFull（Serial…Console）。
 * 谁调用：KernelMain。
 */
int ModulesRunFull(void) {
    return ModulesRun(gModulesFull, MODULE_COUNT(gModulesFull));
}
