/*
 * HAL/Arm64/Hal/Hal.c — CPU / 电源 / IRQ 门闩 / 用户地址布局
 *
 * 定时器与 IRQ 派发见 HalTimer.c；中断帧 HalFrame.c；平台元信息 HalPlat.c。
 */
#include "Hal.h"
#include "BoardConfig.h"

extern void HalExceptionVectorsInstall(void);

int HalInitialize(void) {
    /* PR-A14：BSP 逻辑 CPU=0（TPIDR_EL1） */
    __asm__ volatile("msr tpidr_el1, xzr" ::: "memory");
    return 0;
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
    UINT32 Rsts;

    if (HaltFirmware) {
        /* 对表 Linux/U-Boot：清分区位再置 0x555，同写带 PASSWORD */
        Rsts = *PM_RSTS;
        Rsts = (Rsts & ~0xfffffaaau) | PM_RSTS_RASPBERRYPI_HALT;
        *PM_RSTS = PM_PASS | Rsts;
    }
    Rstc = *PM_RSTC;
    Rstc &= ~PM_RSTC_WRCFG_MASK;
    Rstc |= PM_RSTC_WRCFG_FULL_RESET;
    *PM_WDOG = PM_PASS | 10u;
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
 * BCM2835 §6：输入+上拉，电平 0=按下。每次轮询重申输入（防 I2C ALT0 抢脚）。
 * 边沿或持续低电平都算按下（短接错过下降沿时仍能关机）。
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
    static int sPullDone;
    UINT32 Lev;
    UINT32 Sel;
    volatile UINT32 i;
    int High;

    /* 每拍强制 GPIO 输入：I2C1 默认占 GPIO2/3 ALT0，会吃掉电源键 */
    Sel = *GPFSEL0;
    Sel &= ~(7u << GPIO3_FSEL_SHIFT);
    *GPFSEL0 = Sel;

    if (!sPullDone) {
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
        sPullDone = 1;
    }

    Lev = *GPLEV0;
    High = (Lev & GPIO3_BIT) != 0 ? 1 : 0;
    if (!sReady) {
        sReady = 1;
        return 0; /* 首拍只武装，避免上电已短接立刻关机 */
    }
    /* 低电平即按下（含持续短接）；不单靠下降沿 */
    return High ? 0 : 1;
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
    (void)Vector;
    (void)Handler;
    (void)Type;
}
void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector;
    (void)Handler;
}
void HalIrqNotifyRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector;
    (void)Handler;
}
void HalIrqUnregister(UINT32 Vector) {
    (void)Vector;
}

void HalInstallUserMode(void) {
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
