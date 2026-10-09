/*
 * Modules.c — 开机模块表 + ModulesRun（只拼表，Init 在各 Core/<模块>/）
 *
 * Full：Serial → Memory → VirtualMemory → Driver → Video → …
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

int ModulesRunFull(void) {
    return ModulesRun(gModulesFull, MODULE_COUNT(gModulesFull));
}
