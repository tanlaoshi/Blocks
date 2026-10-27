/*
 * HAL/Arm64/Hal/Hal.c — PR-S3-hal-arm-1：CPU / IRQ / Timer / 用户地址布局
 *
 * 中断帧见 HalFrame.c；平台元信息见 HalPlat.c（同构 x64）。
 */
#include "Hal.h"
#include "Scheduler.h"
#include "BoardConfig.h"

static UINT64 gCntLast;
static UINT64 gCntFreq;
static UINT64 gCntPeriod;
static UINT32 gTimerMs = 10;
static int gTimerReady;
static int gTimerIrq;

extern void HalExceptionVectorsInstall(void);
extern void HalUserEnter(struct HAL_INTERRUPT_FRAME *Frame);
extern void HalGicInitialize(void);
extern UINT32 HalGicAck(void);
extern void HalGicEoi(UINT32 IntId);
extern int HalGicIsTimer(UINT32 IntId);

int HalInitialize(void) {
    /* PR-A14：BSP 逻辑 CPU=0（TPIDR_EL1） */
    __asm__ volatile("msr tpidr_el1, xzr" ::: "memory");
    return 0;
}

void HalCpuHalt(void) {
    /* PR-A13：开 IRQ + WFI，由 CNTV/GIC 唤醒（对齐 x86 sti;hlt;cli） */
    if (gTimerIrq) {
        HalIrqEnable();
        __asm__ volatile("wfi" ::: "memory");
        HalIrqDisable();
        return;
    }
    HalTimerPoll();
    HalCpuRelax();
}
void HalCpuPark(void) {
    for (;;) {
        __asm__ volatile("wfi");
    }
}

/*
 * BCM2835 PM / watchdog @ 0x3F100000（与 U-Boot mach-bcm283x/reset.c 同源）
 * RSTS 特殊分区 63（位型 0x555）：bootcode.bin 见之则 **停住不重启**，
 * 直到拔电——便于拔 SD，而不是 reboot 循环。
 */
#if !BOARD_IS_VIRT
#define PM_PASS                 0x5A000000u
#define PM_RSTC                 ((volatile UINT32 *)(UINTN)0x3F10001Cu)
#define PM_RSTS                 ((volatile UINT32 *)(UINTN)0x3F100020u)
#define PM_WDOG                 ((volatile UINT32 *)(UINTN)0x3F100024u)
#define PM_RSTC_WRCFG_MASK      0x00000030u
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020u
#define PM_RSTS_RASPBERRYPI_HALT 0x555u

static void PmWatchdogReset(int HaltFirmware) {
    UINT32 Rstc;

    if (HaltFirmware) {
        UINT32 Rsts = *PM_RSTS;
        Rsts |= PM_PASS;
        Rsts |= PM_RSTS_RASPBERRYPI_HALT;
        *PM_RSTS = Rsts;
    }
    Rstc = *PM_RSTC;
    Rstc &= ~PM_RSTC_WRCFG_MASK;
    Rstc |= PM_RSTC_WRCFG_FULL_RESET;
    *PM_WDOG = PM_PASS | 1u;
    *PM_RSTC = PM_PASS | Rstc;
    __asm__ volatile("dsb sy" ::: "memory");
}
#endif

void HalCpuReboot(void) {
#if BOARD_IS_VIRT
    {
        register UINT64 X0 __asm__("x0") = 0x84000009ULL; /* PSCI_SYSTEM_RESET */
        __asm__ volatile("hvc #0" : "+r"(X0) : : "memory");
    }
#else
    PmWatchdogReset(0);
#endif
    HalCpuPark();
}

/*
 * HalCpuShutdown — Pi：firmware halt（不自动再起）；virt：停车。
 * 之后可拔电源再拔卡；不要用 reboot 当关机。
 */
void HalCpuShutdown(void) {
#if BOARD_IS_VIRT
    HalCpuPark();
#else
    PmWatchdogReset(1);
    HalCpuPark();
#endif
}

/*
 * Pi 3B 排针 5=GPIO3、9=GND：短接 = 电源键（对表 Linux gpio-shutdown / 课堂线）。
 * BCM2835 ARM Peripherals §6：输入 + 上拉，电平 0 为按下；下降沿触发一次（对表
 * X64 PM1 写清 STS）。virt 无此脚。
 */
int HalPowerButtonPressed(void) {
#if BOARD_IS_VIRT
    return 0;
#else
#define GPIO_BASE     0x3F200000ULL
#define GPFSEL0       ((volatile UINT32 *)(UINTN)(GPIO_BASE + 0x00u))
#define GPLEV0        ((volatile UINT32 *)(UINTN)(GPIO_BASE + 0x34u))
#define GPPUD         ((volatile UINT32 *)(UINTN)(GPIO_BASE + 0x94u))
#define GPPUDCLK0     ((volatile UINT32 *)(UINTN)(GPIO_BASE + 0x98u))
#define GPIO3_FSEL_SHIFT 9u
#define GPIO3_BIT     (1u << 3)

    static int sReady;
    static int sPrevHigh = 1;
    UINT32 Lev;
    UINT32 Sel;
    volatile UINT32 i;
    int High;
    int Edge;

    if (!sReady) {
        Sel = *GPFSEL0;
        Sel &= ~(7u << GPIO3_FSEL_SHIFT); /* input */
        *GPFSEL0 = Sel;
        /* 上拉：BCM2835 §6.1 GPPUD=2 → 等 → GPPUDCLK → 等 → 清 */
        *GPPUD = 2u;
        for (i = 0; i < 150u; i++) {
            __asm__ volatile("nop");
        }
        *GPPUDCLK0 = GPIO3_BIT;
        for (i = 0; i < 150u; i++) {
            __asm__ volatile("nop");
        }
        *GPPUD = 0;
        *GPPUDCLK0 = 0;
        __asm__ volatile("dsb sy" ::: "memory");
        sPrevHigh = ((*GPLEV0 & GPIO3_BIT) != 0) ? 1 : 0;
        sReady = 1;
        return 0; /* 首拍只武装，避免上电已短接立刻关机 */
    }

    Lev = *GPLEV0;
    High = (Lev & GPIO3_BIT) != 0 ? 1 : 0;
    Edge = (sPrevHigh && !High) ? 1 : 0;
    sPrevHigh = High;
    return Edge;
#undef GPIO_BASE
#undef GPFSEL0
#undef GPLEV0
#undef GPPUD
#undef GPPUDCLK0
#undef GPIO3_FSEL_SHIFT
#undef GPIO3_BIT
#endif
}

void HalIrqEnable(void) {
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}
void HalIrqDisable(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
}
UINT64 HalIrqSave(void) {
    UINT64 Flags;
    __asm__ volatile("mrs %0, daif" : "=r"(Flags));
    HalIrqDisable();
    return Flags;
}
void HalIrqRestore(UINT64 Flags) {
    __asm__ volatile("msr daif, %0" ::"r"(Flags) : "memory");
}
void HalCpuRelax(void) {
    __asm__ volatile("yield" ::: "memory");
}
void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type) {
    (void)Vector; (void)Handler; (void)Type;
}
void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector; (void)Handler;
}
void HalIrqNotifyRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector; (void)Handler;
}
void HalIrqUnregister(UINT32 Vector) { (void)Vector; }
void HalIrqEoi(UINT32 Vector) {
    HalGicEoi(Vector);
}

static void ArmTimerArm(void) {
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(gCntPeriod) : "memory");
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1ULL) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

void HalTimerInitialize(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gCntFreq));
    if (gCntFreq == 0) {
        gCntFreq = 62500000ULL; /* QEMU virt 常见缺省 */
    }
    gCntPeriod = gCntFreq / 100; /* ~10ms */
    if (gCntPeriod == 0) {
        gCntPeriod = 1;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(gCntLast));
    gTimerReady = 1;
}

void HalTimerSetInterval(UINT32 Milliseconds) {
    if (Milliseconds == 0) {
        Milliseconds = 10;
    }
    gTimerMs = Milliseconds;
    if (gCntFreq == 0) {
        return;
    }
    gCntPeriod = (gCntFreq * (UINT64)Milliseconds) / 1000ULL;
    if (gCntPeriod == 0) {
        gCntPeriod = 1;
    }
}

void HalTimerAck(void) {
    ArmTimerArm();
}

void HalTimerStart(void) {
    if (!gTimerReady) {
        HalTimerInitialize();
    }
    HalExceptionVectorsInstall();
#if BOARD_IS_VIRT
    HalGicInitialize();
    HalTimerSetInterval(gTimerMs);
    ArmTimerArm();
    gTimerIrq = 1;
    HalSerialWrite("timer: Arm64 CNTV+GIC irq\n");
#else
    /* 真机尚无板级 GIC 基址；轮询 CNTV（HalTimerPoll），勿写 virt@0x08000000 */
    HalTimerSetInterval(gTimerMs);
    gTimerIrq = 0;
    /* 勿 HalSerialWrite：常落在 blocks> 前后刷屏；需要时看 BootMark/调试 */
#endif
}

UINT32 HalTicksPerSec(void) {
    return 1000; /* CNTV 已按 ms 武装 */
}

/* PR-A14：AP 在 BSP HalTimerStart 之前也可本地开 CNTV（gCntPeriod 已在 InitCpu） */
void HalTimerStartAp(void) {
    extern void HalGicInitializeCpu(void);

    if (!gTimerReady) {
        HalTimerInitialize();
    }
    HalExceptionVectorsInstall();
    HalGicInitializeCpu();
    ArmTimerArm();
    gTimerIrq = 1;
}

void HalTimerPoll(void) {
    UINT64 Now;
    UINT64 Delta;

    if (!gTimerReady || gTimerIrq) {
        return;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Now));
    Delta = Now - gCntLast;
    while (Delta >= gCntPeriod) {
        HalCpuIncrementTicks();
        gCntLast += gCntPeriod;
        Delta -= gCntPeriod;
    }
}

int HalTimerUsesIrq(void) {
    return gTimerIrq ? 1 : 0;
}

/* PR-A13 / PR-V-input-fix：IRQ 帧上 tick + SchedulerOnTimer（对齐 x86） */
UINT64 HalExceptionIrqDispatch(HAL_INTERRUPT_FRAME *Frame) {
    UINT32 Id = HalGicAck();
    UINT64 Ret = 0;

    if (HalGicIsTimer(Id)) {
        HalCpuIncrementTicks();
        HalTimerAck();
        HalGicEoi(Id);
        if (SchedulerIsOnline() && Frame) {
            Ret = SchedulerOnTimer(Frame);
        }
        return Ret;
    }
    HalGicEoi(Id);
    return 0;
}

void HalInstallUserMode(void) {
    /* EL0 入口前确保向量表；SP_EL1 由当前内核栈承担 */
    HalExceptionVectorsInstall();
}

void HalSyscallInitialize(void) {
    HalExceptionVectorsInstall();
    HalSerialWrite("syscall: Arm64 SVC (EL0) ready\n");
#if BOARD_IS_VIRT
    HalUserSelfTest();
#endif
}

void HalKernelSetStack(UINT64 StackTop) {
    (void)StackTop;
}

/* 躲开内核 @0x40000000 恒等映射：用户区从 4GiB 起 */
UINT64 HalUserCodeVirt(void) {
    return 0x100000000ULL;
}
UINT64 HalUserStackVirt(void) {
    return 0x100100000ULL;
}
UINT64 HalUserStackSize(void) {
    return 0x4000ULL;
}
UINT64 HalUserBrkMax(void) {
    return 0x100080000ULL;
}
UINT64 HalUserSoBase(void) {
    return 0x100080000ULL;
}
UINT64 HalUserMmapBase(void) {
    return 0x100200000ULL;
}
UINT64 HalUserMmapEnd(void) {
    return 0x100400000ULL;
}
UINT64 HalUserVirtEnd(void) {
    return HalUserMmapEnd();
}
