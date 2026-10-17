/*
 * Kernel.c — BringUp：三架构合流后的统一入口（KernelMain）
 *
 * BringUp/<Arch>/KernelHandoff 填好 BOOT_INFO 后走进这里。
 * 选路 → ModulesRun → 拉起 shell/gui/worker → SchedulerStart。
 *
 * 从哪读：KernelMain → Virt / VirtDesktop / Full → KernelMainCommon。
 * 别改：HalConsoleOnly →（FB 且 Virt）→ Full；shell/gui/worker 失败即停。
 */
#include "BootInfo.h"
#include "ToySerialLog.h"
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

static void KernelMainVirt(void);
static void KernelMainVirtDesktop(void);
static void KernelMainFull(void);
static void KernelMainCommon(void);
static void KernelParkForever(void);

static void KernelSpawnOrPark(const char *Name, void (*Entry)(void)) {
    if (SchedulerCreate(Name, Entry) < 0) {
        ToyLogBoot("kernel: SchedulerCreate failed\n");
        KernelParkForever();
    }
}

/* SMP≥3 才起 InputTask 钉 CPU2；失败只打日志（与旧 (void)KernelSpawn 相同）。 */
static void KernelTrySpawnInput(void) {
    if (HalCpuCount() <= 2) {
        return;
    }
    if (SchedulerCreate("input", InputTask) < 0) {
        ToyLogBoot("kernel: SchedulerCreate failed\n");
    }
}

/* 单核直跑串口壳；多核进调度让 AP idle。 */
static void KernelMainVirtEnterShell(void) {
    if (HalCpuCount() > 1) {
        KernelSpawnOrPark("shell", gConsole.SerialRun);
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
    ToyLogBoot(Line);
}

/*
 * 尽早挂串口和帧缓冲：后面模块 gConsole.Write 若 Width=0 会空转。
 * 有 FB 则开 GOP 镜像（接 ToyBoot 黑底，禁止再全屏 Clear）。
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
        ToyLogBoot("Boot: KernelMain Live\n");
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

/* 串口子集。 */
static void KernelMainVirt(void) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(KernelModulesRunVirt);
    KernelAfterModules();
    KernelMainVirtEnterShell();
}

/* 桌面常驻：只顺序 Create / 演示任务 / 可选 input / Start。 */
static void KernelMainCommon(void) {
    KernelSpawnOrPark("shell", ShellTask);
    KernelSpawnOrPark("gui", GuiTask);
    KernelSpawnOrPark("worker", WorkerTask);
    KernelTaskDemoStart();
    KernelTrySpawnInput();
    SchedulerStart();
}

static void KernelMainVirtDesktop(void) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(KernelModulesRunVirtDesktop);
    KernelAfterModules();
    KernelMainCommon();
}

static void KernelMainFull(void) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(KernelModulesRunFull);
    KernelAfterModules();
    KernelMainCommon();
}

void KernelMain(void) {
    if (HalConsoleOnly()) {
        KernelMainVirt();
        return;
    }
    if (HalHasFrameBuffer() && HalPlatformIsVirtSerialConsole()) {
        KernelMainVirtDesktop();
        return;
    }
    KernelMainFull();
}
