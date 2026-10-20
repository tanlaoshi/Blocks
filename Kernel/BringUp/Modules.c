/*
 * Modules.c — 模块表 + ModulesRun（按表顺序调 Init）
 *
 * 三张表（选路见 Kernel.c）：Virt / VirtDesktop / Full。
 * 从哪读：MODULE_PHASE_* → gModules* → ModulesRun。
 *
 * 硬脊（违反会起不来或挂盘/输入错序）：
 *   PMM → VMM → DISPLAY(Video) → … → INPUT(USB) → FileSystem
 *   Create 任务在 ModulesRun 返回之后（SESSION 只登记调度/命令，不开四环）。
 *
 * 现序（已收软点）：
 *   · PLATFORM：Cpu → Driver → Smp（先中断/syscall，再绑 MMIO/块，再拉 AP）
 *   · SESSION 在 FS/Gui/Net 前（HalDriverSched 早挂；Gui 长画可用 IoBreath）
 *
 * 落点：Initialize 胶水 → BringUp/Initialize/；机制 → Core/；会话 → Services/；
 *   Smp → HalSmp*；驱动回调槽 → HAL/Slots/（不进本表）。
 */
#include "KernelModules.h"
#include "KernelModulesPrivate.h"
#include "Module.h"
#include "Hal.h"
#include "HalDevices.h"
#include "SerialLog.h"
#include "Debug.h"
#include "Console.h"
#include "Desktop.h"

static int gVirtDesktop;

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

/*
 * 阶段宏 —— 只拼表；展开后条目相对顺序勿改。
 *
 * EARLY     Serial → PMM → VMM     早日志；PMM 必须紧挨在 VMM 前
 * DISPLAY   Video                  背缓冲/Logo；必须紧跟 VMM（无 FB 的 Virt 表无本阶段）
 * PLATFORM  Cpu → Driver → Smp     先 CPU/中断/syscall；再绑 MMIO/块；再拉 AP
 * INPUT     USB                    键鼠；仅 Full；必须在 FileSystem 前
 * SESSION   Scheduler → Console    调度槽与命令表；须在 FS/Gui/Net 前（IoBreath）
 * （字面量）FileSystem / Gui / Network   Services；Virt 可省略 Gui/Net
 */
#define MODULE_PHASE_EARLY                                                         \
    { "Serial",          SerialModuleInitialize },                                 \
    { "PhysicalMemory",  PhysicalMemoryInitialize },                               \
    { "VirtualMemory",   VirtualMemoryInitialize }

#define MODULE_PHASE_DISPLAY                                                       \
    { "Video",           VideoInitialize }

#define MODULE_PHASE_PLATFORM                                                      \
    { "Cpu",             CpuInitialize },                                          \
    { "Driver",          DriverInitialize },                                       \
    { "Smp",             HalSmpStartApplicationProcessors }

#define MODULE_PHASE_INPUT                                                         \
    { "USB",             UsbInitialize }

#define MODULE_PHASE_SESSION                                                       \
    { "Scheduler",       SchedulerInitialize },                                    \
    { "Console",         ConsoleInitialize }

/* 真机 / QEMU 桌面：EARLY→DISPLAY→PLATFORM→INPUT→SESSION→FS/Gui/Net */
static const MODULE gModulesFull[] = {
    MODULE_PHASE_EARLY,
    MODULE_PHASE_DISPLAY,
    MODULE_PHASE_PLATFORM,
    MODULE_PHASE_INPUT,
    MODULE_PHASE_SESSION,
    { "FileSystem",      FileSystemInitialize },
    { "Gui",             GuiInitialize },
    { "Network",         NetworkInitialize },
};

/* 串口-only：无 DISPLAY/INPUT/Gui/Net；SESSION 在 FS 前；FS 仍要（virtio-blk） */
static const MODULE gModulesVirt[] = {
    MODULE_PHASE_EARLY,
    MODULE_PHASE_PLATFORM,
    MODULE_PHASE_SESSION,
    { "FileSystem",      FileSystemInitialize },
};

/* virt 桌面：有 DISPLAY/Gui/Net，无 INPUT；SESSION 在 FS/Gui/Net 前 */
static const MODULE gModulesVirtDesktop[] = {
    MODULE_PHASE_EARLY,
    MODULE_PHASE_DISPLAY,
    MODULE_PHASE_PLATFORM,
    MODULE_PHASE_SESSION,
    { "FileSystem",      FileSystemInitialize },
    { "Gui",             GuiInitialize },
    { "Network",         NetworkInitialize },
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

/* ---- ModulesRun：按表顺序调 Init；失败打模块名。真机每步后 HalInputPoll 防 USB 环堵 ---- */

static void ModLog(const char *Name, const char *Suffix) {
    LogBoot("Boot: Mod ");
    LogBoot(Name);
    LogBoot(Suffix);
}

/*
 * Logo 从 0 起；模块目标点间距收紧（大跳由 Set 拆成 1%）；
 * USB→FS 之间 78..92 留给 MSC claim 按口 HalBootLogoBump。
 */
static UINT32 ModLogoPct(const char *Name) {
    if (Name[0] == 'C' && Name[1] == 'p') {
        return 20;
    }
    if (Name[0] == 'D' && Name[1] == 'r') {
        return 35;
    }
    if (Name[0] == 'S' && Name[1] == 'm') {
        return 50;
    }
    if (Name[0] == 'U') {
        return 70;
    }
    if (Name[0] == 'F') {
        return 92;
    }
    return 0;
}

static void ModProgressMark(const char *Name) {
    UINT32 Pct = ModLogoPct(Name);

    if (Pct != 0) {
        DesktopBootLogoSet(Pct);
    }
}

int ModulesRun(const MODULE *List, int Count) {
    /* Gui 等会早于 Console 模块登记命令；函数指针须先就位 */
    ConsolePublishAll();

    for (int i = 0; i < Count; i++) {
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
