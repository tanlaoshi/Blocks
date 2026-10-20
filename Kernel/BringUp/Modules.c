/*
 * Modules.c — 模块表 + ModulesRun（按表顺序调 Init）
 *
 * 三张表对应 KernelMain 三条路（选路见 BringUp/Kernel.c 文件头）：
 *   Virt          ← HalConsoleOnly
 *   VirtDesktop   ← FB 且 HalPlatformIsVirtSerialConsole
 *   Full          ← 否则（真机；USB 在 FileSystem 前）
 *
 * 从哪读：下方 gModules*，再看 KernelModulesRun*；ModulesRun 在文件后部。
 * 别改：各表条目顺序（尤其 Full 里 USB 在 FileSystem 前）。
 * 串口、处理器、USB、画面的开机函数在 HAL/Initialize/，名词在前。
 * 多核直接调 HalSmpStartApplicationProcessors。
 * 调度、控制台、文件系统、窗口管理器、虚拟内存、驱动的开机函数在各自主文件里。
 */
#include "KernelModules.h"
#include "KernelModulesPrivate.h"
#include "Module.h"
#include "Hal.h"
#include "HalDevices.h"
#include "ToySerialLog.h"
#include "Debug.h"
#include "Console.h"
#include "Desktop.h"

static int gVirtDesktop;

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

/* x86 全量：usb 在 FS 前；Video 紧跟 VMM；Driver 在 Video 后（Block 可绑 MMIO） */
static const MODULE gModulesFull[] = {
    { "Serial",          SerialModuleInitialize },
    { "PhysicalMemory",  InitializePhysicalMemory },
    { "VirtualMemory",   InitializeVirtualMemory },
    { "Video",           VideoInitialize },
    { "Driver",          InitializeDriver },
    { "Cpu",             CpuInitialize },
    { "Smp",             HalSmpStartApplicationProcessors },
    { "USB",             UsbInitialize },
    { "FileSystem",      InitializeFileSystem },
    { "Gui",             InitializeGui },
    { "Network",         InitializeNetwork },
    { "Scheduler",       InitializeScheduler },
    { "Console",         InitializeConsole },
};

/* 串口子集。仍挂 FileSystem：virt --serial 有 virtio-blk，供 exec HELLO.ELF */
static const MODULE gModulesVirt[] = {
    { "Serial",          SerialModuleInitialize },
    { "PhysicalMemory",  InitializePhysicalMemory },
    { "VirtualMemory",   InitializeVirtualMemory },
    { "Driver",          InitializeDriver },
    { "Cpu",             CpuInitialize },
    { "Smp",             HalSmpStartApplicationProcessors },
    { "FileSystem",      InitializeFileSystem },
    { "Scheduler",       InitializeScheduler },
    { "Console",         InitializeConsole },
};

/* virt 桌面（输入在后续路径；挂 net） */
static const MODULE gModulesVirtDesktop[] = {
    { "Serial",          SerialModuleInitialize },
    { "PhysicalMemory",  InitializePhysicalMemory },
    { "VirtualMemory",   InitializeVirtualMemory },
    { "Video",           VideoInitialize },
    { "Driver",          InitializeDriver },
    { "Cpu",             CpuInitialize },
    { "Smp",             HalSmpStartApplicationProcessors },
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
    ToyLogBoot("Boot: Mod ");
    ToyLogBoot(Name);
    ToyLogBoot(Suffix);
}

/* 引导读盘时 T 落到 40。这里按做完的模块继续下落。 */
static UINT32 ModLogoPct(const char *Name) {
    if (Name[0] == 'D' && Name[1] == 'r') {
        return 55;
    }
    if (Name[0] == 'C' && Name[1] == 'p') {
        return 62;
    }
    if (Name[0] == 'S' && Name[1] == 'm') {
        return 70;
    }
    if (Name[0] == 'U') {
        return 84;
    }
    if (Name[0] == 'F') {
        return 94;
    }
    return 0;
}

static void ModProgressMark(const char *Name) {
    UINT32 Pct = ModLogoPct(Name);

    if (Pct != 0) {
        DesktopBootLogoSet(Pct);
    }
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
            ModProgressMark(List[i].Name);
            return -1;
        }
        /*
         * 真机 poll-USB：usb 之后 gui/console 初始化期间若无人 Drain，
         * 中断 IN 完成会塞满事件环 → 桌面后键鼠假死。
         */
        if (!HalCpuIsHypervisor()) {
            HalInputPoll();
        }
        ModProgressMark(List[i].Name);
    }
    return 0;
}
