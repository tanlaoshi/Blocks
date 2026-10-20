/*
 * Kernel.c — BringUp：三架构合流后的统一入口（KernelMain）
 *
 * BringUp/<Arch>/KernelHandoff 填好 BOOT_INFO 后走进这里。
 *
 * 三条路（勿改判定顺序）：
 *   1. HalConsoleOnly()
 *        → 模块表 Virt（串口子集）→ 单核直跑串口壳 / 多核 Shell+SchedulerStart
 *   2. HalHasFrameBuffer() && HalPlatformIsVirtSerialConsole()
 *        → 模块表 VirtDesktop（有 Video/Gui/Net，无 USB）→ Common
 *   3. 否则（真机 x86 等）
 *        → 模块表 Full（USB 在 FileSystem 前）→ Common
 *
 * 公共节拍：AttachEarlyVideo → ModulesRun → AfterModules → Finish。
 *
 * 任务拉起（不在模块表；Initialize 已在表内 Scheduler 项做完）：
 *   Common：Create Shell/Gui/Worker（失败 Park）→ KernelTaskDemoStart（Tasks 层）
 *            → 可选 Input → SchedulerStart
 *   Virt 多核：Create Shell → SchedulerStart；单核直跑 SerialRun（不 Start）
 * SchedulerStart 步骤见 Core/Scheduler/Boot.c。
 */
#include "BootInfo.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalSerial.h"
#include "Scheduler.h"
#include "KernelModules.h"
#include "Tasks.h"
#include "KernelTask.h"
#include "Console.h"
#include "Font.h"
#include "Theme.h"
#include "Elf.h"
#include "LibWrite.h"

static void KernelMainCommon(void);
static void KernelMainVirtEnterShell(void);
static void KernelParkForever(void);

static void KernelSpawnOrPark(const char *Name, void (*Entry)(void)) {
    if (SchedulerCreate(Name, Entry) < 0) {
        LogBoot("Boot: SchedulerCreate Failed\n");
        KernelParkForever();
    }
}

/*
 * 逻辑 CPU≥3 才 Create Input（要 0/1/2 三槽，非「板子必须三核」）。
 * Start 再钉 CPU2。drain 分域：Tasks.c YieldForPollInput 头；钉核 sched-pin-doc。
 * 失败只打日志。
 */
static void KernelTrySpawnInput(void) {
    if (HalCpuCount() <= 2) {
        return;
    }
    if (SchedulerCreate("Input", InputTask) < 0) {
        LogBoot("Boot: SchedulerCreate Failed\n");
    }
}

/* 单核直跑串口壳；多核进调度让 AP idle。 */
static void KernelMainVirtEnterShell(void) {
    if (HalCpuCount() > 1) {
        KernelSpawnOrPark("Shell", gConsole.SerialRun);
        SchedulerStart();
        return;
    }
    HalTimerStart();
    gConsole.SerialRun();
}

static void KernelParkForever(void) {
    for (;;) {
        HalCpuPark();
    }
}

static void KernelAppendDecimal4(char *Line, int *N, UINT32 Value) {
    Line[(*N)++] = (char)('0' + ((Value / 1000) % 10));
    Line[(*N)++] = (char)('0' + ((Value / 100) % 10));
    Line[(*N)++] = (char)('0' + ((Value / 10) % 10));
    Line[(*N)++] = (char)('0' + (Value % 10));
}

static void KernelLogFrameBufferSize(const BOOT_INFO *Info) {
    char Line[48];
    int N = 0;
    const char *Prefix = "Boot: FB ";

    while (*Prefix && N < 16) {
        Line[N++] = *Prefix++;
    }
    KernelAppendDecimal4(Line, &N, Info->HorizontalResolution);
    Line[N++] = 'x';
    KernelAppendDecimal4(Line, &N, Info->VerticalResolution);
    Line[N++] = '\n';
    Line[N] = 0;
    LogBoot(Line);
}

/*
 * 尽早挂串口和帧缓冲：后面模块 gConsole.Write 若 Width=0 会空转。
 * 有 FB 则开 GOP 镜像（接 Boot 黑底，禁止再全屏 Clear）。
 */
static ELF_RELOC_KIND KernelElfRelocKind(UINT32 Type) {
    return (ELF_RELOC_KIND)HalElfRelocKind(Type);
}

static void KernelAttachEarlyVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);

    HalSerialInitialize();
    LibWriteRegister(HalSerialWrite);
    ElfArchRegister(HalElfMachine(), KernelElfRelocKind, HalSyncICache, HalUserSoBase());
    HalVideoSet(&Video);
    if (Info && Info->FrameBufferSize != 0) {
        FontInitialize();
        ThemeInitialize();
        HalSerialGopEnable();
        LogBoot("Boot: KernelMain Live\n");
        KernelLogFrameBufferSize(Info);
    }
}

static void KernelAfterModules(void) {
    (void)FontSetById(ThemeFontId());
    HalSerialGopMirror(0);
}

static void KernelRunOrPark(int (*RunModules)(void)) {
    if (RunModules() != 0) {
        KernelParkForever();
    }
}

/* Attach → 模块表 → After → Finish（Virt 进串口壳，桌面进 Common） */
static void KernelRunPath(int (*RunModules)(void), void (*Finish)(void)) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(RunModules);
    KernelAfterModules();
    Finish();
}

/* 桌面常驻：只顺序 Create / 演示任务 / 可选 input / Start。 */
static void KernelMainCommon(void) {
    KernelSpawnOrPark("Shell", ShellTask);
    KernelSpawnOrPark("Gui", GuiTask);
    KernelSpawnOrPark("Worker", WorkerTask);
    KernelTaskDemoStart();
    KernelTrySpawnInput();
    SchedulerStart();
}

void KernelMain(void) {
    if (HalConsoleOnly()) {
        KernelRunPath(KernelModulesRunVirt, KernelMainVirtEnterShell);
        return;
    }
    if (HalHasFrameBuffer() && HalPlatformIsVirtSerialConsole()) {
        KernelRunPath(KernelModulesRunVirtDesktop, KernelMainCommon);
        return;
    }
    KernelRunPath(KernelModulesRunFull, KernelMainCommon);
}
