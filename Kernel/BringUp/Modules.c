/*
 * Modules.c — 模块表 + ModulesRun（按表顺序调 Init）
 *
 * 人话：串口子集 / virt 桌面 / x86 全量 各走自己的顺序表。
 * 从哪读：下方三张 gModules*，再看 KernelModulesRun*；ModulesRun 在文件后部。
 * 别改：各表条目顺序（尤其 Full 里 USB 在 FileSystem 前）。
 * Initialize*：Hal 侧在 HAL/Initialize/；Console/Gui/Net/FS 在 Services；PhysicalMemory/Driver/VMM/Scheduler 在 Core。
 * 选路见 BringUp/Kernel.c。
 */
#include "KernelModules.h"
#include "KernelModulesPrivate.h"
#include "Module.h"
#include "Hal.h"
#include "HalDevices.h"
#include "ToySerialLog.h"
#include "Debug.h"
#include "Console.h"

static int gVirtDesktop;

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

/* x86 全量：usb 在 FS 前；Video 紧跟 VMM；Driver 在 Video 后（Block 可绑 MMIO） */
static const MODULE gModulesFull[] = {
    { "Serial",          InitializeSerial },
    { "PhysicalMemory",  InitializePhysicalMemory },
    { "VirtualMemory",   InitializeVirtualMemory },
    { "Video",           InitializeVideo },
    { "Driver",          InitializeDriver },
    { "Cpu",             InitializeCpu },
    { "Smp",             InitializeSmp },
    { "USB",             InitializeUsb },
    { "FileSystem",      InitializeFileSystem },
    { "Gui",             InitializeGui },
    { "Network",         InitializeNetwork },
    { "Scheduler",       InitializeScheduler },
    { "Console",         InitializeConsole },
};

/* 串口子集。仍挂 FileSystem：virt --serial 有 virtio-blk，供 exec HELLO.ELF */
static const MODULE gModulesVirt[] = {
    { "Serial",          InitializeSerial },
    { "PhysicalMemory",  InitializePhysicalMemory },
    { "VirtualMemory",   InitializeVirtualMemory },
    { "Driver",          InitializeDriver },
    { "Cpu",             InitializeCpu },
    { "Smp",             InitializeSmp },
    { "FileSystem",      InitializeFileSystem },
    { "Scheduler",       InitializeScheduler },
    { "Console",         InitializeConsole },
};

/* virt 桌面（输入在后续路径；挂 net） */
static const MODULE gModulesVirtDesktop[] = {
    { "Serial",          InitializeSerial },
    { "PhysicalMemory",  InitializePhysicalMemory },
    { "VirtualMemory",   InitializeVirtualMemory },
    { "Video",           InitializeVideo },
    { "Driver",          InitializeDriver },
    { "Cpu",             InitializeCpu },
    { "Smp",             InitializeSmp },
    { "FileSystem",      InitializeFileSystem },
    { "Gui",             InitializeGui },
    { "Network",         InitializeNetwork },
    { "Scheduler",       InitializeScheduler },
    { "Console",         InitializeConsole },
};

int KernelModulesVirtDesktop(void) {
    return gVirtDesktop;
}

int KernelModulesRunVirt(void) {
    gVirtDesktop = 0;
    return ModulesRun(gModulesVirt, MODULE_COUNT(gModulesVirt));
}

int KernelModulesRunVirtDesktop(void) {
    gVirtDesktop = 1;
    return ModulesRun(gModulesVirtDesktop, MODULE_COUNT(gModulesVirtDesktop));
}

int KernelModulesRunFull(void) {
    gVirtDesktop = HalHasFrameBuffer() ? 1 : 0;
    return ModulesRun(gModulesFull, MODULE_COUNT(gModulesFull));
}

/* ---- ModulesRun ---- */

static void ModLog(const char *Name, const char *Suffix) {
    ToyLogBoot("[Mod] ");
    ToyLogBoot(Name);
    ToyLogBoot(Suffix);
}

/* 真机 bring-up：进度走统一 boot log，不再画左上角色块 */
static void ModProgressMark(int Step) {
    (void)Step;
}

/* 按顺序初始化所有模块；失败时打印模块名并返回 -1 */
int ModulesRun(const MODULE *List, int Count) {
    int i;

    /* 桌面在控制台模块之前就会登记命令、写提示。表必须先有函数。 */
    ConsolePublishAll();

    for (i = 0; i < Count; i++) {
        /* 真机无 COM1 时也要走 HalSerial→GOP，才能看见卡在哪个模块 */
        ModLog(List[i].Name, "\n");
        if (List[i].Init == 0 || List[i].Init() != 0) {
            ModLog(List[i].Name, " Failed\n");
            ModProgressMark(7); /* 白条 = 失败停 */
            return -1;
        }
        /*
         * 真机 poll-USB：usb 之后 gui/console 初始化期间若无人 Drain，
         * 中断 IN 完成会塞满事件环 → 桌面后键鼠假死。
         */
        if (!HalCpuIsHypervisor()) {
            HalInputPoll();
        }
    }
    return 0;
}
