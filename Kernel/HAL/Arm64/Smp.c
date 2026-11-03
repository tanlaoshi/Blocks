/*
 * Smp.c — PR-A14：QEMU virt aarch64 PSCI CPU_ON + 每核 idle
 */
#include "Compiler.h"
#include "Hal.h"
#include "HalSerial.h"
#include "BoardConfig.h"
#include "Virtio/Dtb.h"
#include "Scheduler.h"
#include "SerialLog.h"
#include "CString.h"

#define SmpLog(Text)   LogSmp(Text)
#define SmpLogHex32(V) LogSmpHex32(V)
#define SmpLogHex64(V) LogSmpHex64(V)

#define PSCI_CPU_ON_64     0xC4000003ULL
#define SMP_READY_MAGIC    0x534D5052u /* 'SMPR' */
#define ARM64_VIRT_DTB_ADDR 0x4a000000ULL

extern void HalApEntry(void);
extern void HalTimerStartAp(void);
extern void HalPagingEnableAp(void);
extern void HalExceptionVectorsInstall(void);

UINT64 gApStackTop[HAL_MAX_CPUS];

static UINT8 gApStacks[HAL_MAX_CPUS][8192] BLOCKS_ALIGNED(16);
static int gCpuCount = 1;
static volatile UINT32 gApReady[HAL_MAX_CPUS];
static volatile UINT32 gApHelloCount;
static volatile UINT64 gCpuTicks[HAL_MAX_CPUS];
static UINT64 gSmpDtbPhys;

void HalSmpNoteDtb(UINT64 DtbPhys) {
    if (DtbPhys != 0) {
        gSmpDtbPhys = DtbPhys;
    }
}

static void SetCpuId(UINT32 Id) {
    __asm__ volatile("msr tpidr_el1, %0" ::"r"((UINT64)Id) : "memory");
}

static INT64 PsciCpuOn(UINT64 TargetMpidr, UINT64 Entry, UINT64 Context) {
    register UINT64 X0 __asm__("x0") = PSCI_CPU_ON_64;
    register UINT64 X1 __asm__("x1") = TargetMpidr;
    register UINT64 X2 __asm__("x2") = Entry;
    register UINT64 X3 __asm__("x3") = Context;
    __asm__ volatile("hvc #0"
                     : "+r"(X0)
                     : "r"(X1), "r"(X2), "r"(X3)
                     : "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
                       "x11", "x12", "x13", "x14", "x15", "x16", "x17");
    return (INT64)X0;
}

static void DelayLoops(volatile UINT32 N) {
    while (N--) {
        __asm__ volatile("yield");
    }
}

void HalApMain(UINT32 Logical) {
    if (Logical >= HAL_MAX_CPUS) {
        Logical = HAL_MAX_CPUS - 1;
    }
    SetCpuId(Logical);
    HalPagingEnableAp();
    HalExceptionVectorsInstall();
    HalTimerStartAp();

    SmpLog("Smp: Hello CPU=");
    SmpLogHex32(Logical);
    SmpLog("\n");
    gApHelloCount++;
    __asm__ volatile("" ::: "memory");
    gApReady[Logical] = SMP_READY_MAGIC;

    while (!SchedulerIsOnline()) {
        HalCpuHalt();
    }
    SchedulerApStart();
    for (;;) {
        HalCpuPark();
    }
}

int HalCpuCount(void) {
    return gCpuCount > 0 ? gCpuCount : 1;
}

UINT8 HalCpuApicId(UINT32 LogicalCpu) {
    (void)LogicalCpu;
    return 0;
}

UINT32 HalCpuGetId(void) {
    UINT64 V;
    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(V));
    return (UINT32)V;
}

void HalCpuSetLogicalId(UINT32 Id) {
    __asm__ volatile("msr tpidr_el1, %0" ::"r"((UINT64)Id) : "memory");
}

int HalCpuIsBootstrapProcessor(void) {
    return HalCpuGetId() == 0;
}

void HalCpuIncrementTicks(void) {
    UINT32 Id = HalCpuGetId();
    if (Id < HAL_MAX_CPUS) {
        gCpuTicks[Id]++;
    }
#if BOARD_IS_VIRT
    /* PR-V-input-diag：仅 QEMU virt 定时采样；Pi 勿链进刷屏 */
    {
        void VirtioInputDiagOnTimer(void);
        VirtioInputDiagOnTimer();
    }
#endif
}

UINT64 HalCpuTicks(UINT32 Cpu) {
    if (Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    return gCpuTicks[Cpu];
}

int HalSmpStartApplicationProcessors(void) {
    UINT64 Dtb;
    int Want;
    int i;
    int Started = 0;

    SetCpuId(0);
    gCpuCount = 1;
    gApHelloCount = 0;
    for (i = 0; i < HAL_MAX_CPUS; i++) {
        gCpuTicks[i] = 0;
        gApReady[i] = 0;
        gApStackTop[i] =
            (UINT64)(UINTN)(gApStacks[i] + sizeof(gApStacks[i]));
    }

#if !BOARD_IS_VIRT
    /*
     * psmp-1：真机只探针 DTB 核数 / enable-method，禁止盲发 HVC，gCpuCount 仍为 1。
     * 串口：Boot: Pi SMP cpus=N method=…
     */
    {
        char EnableMethod[48];
        char Line[96];
        char DigitBuffer[12];
        int MethodOk;
        UINT32 CpuReported;
        UINT32 Value;
        int DigitCount;
        int Index;
        UINTN Offset;

        Dtb = gSmpDtbPhys;
        Want = Dtb != 0 ? DtbCpuCount(Dtb) : 0;
        CpuReported = (UINT32)(Want > 0 ? Want : 0);
        MethodOk = (Dtb != 0 &&
                    DtbCpuEnableMethod(Dtb, EnableMethod,
                                       (int)sizeof(EnableMethod)) == 0)
                       ? 1
                       : 0;
        /* 走 BootMark：与 WIFI 同行可见；LogSmp 环可能未刷进壳串口 */
        StringCopy(Line, sizeof(Line), "Boot: Pi SMP cpus=");
        Offset = 0;
        while (Offset + 1 < sizeof(Line) && Line[Offset] != 0) {
            Offset++;
        }
        Value = CpuReported;
        DigitCount = 0;
        do {
            DigitBuffer[DigitCount++] = (char)('0' + (Value % 10u));
            Value /= 10u;
        } while (Value != 0u && DigitCount < (int)sizeof(DigitBuffer));
        for (Index = DigitCount; Index > 0 && Offset + 1 < sizeof(Line);
             Index--) {
            Line[Offset++] = DigitBuffer[Index - 1];
        }
        StringCopy(Line + Offset, sizeof(Line) - Offset, " method=");
        Offset = 0;
        while (Offset + 1 < sizeof(Line) && Line[Offset] != 0) {
            Offset++;
        }
        if (MethodOk) {
            StringCopy(Line + Offset, sizeof(Line) - Offset, EnableMethod);
        } else {
            StringCopy(Line + Offset, sizeof(Line) - Offset, "(none)");
        }
        Offset = 0;
        while (Offset + 1 < sizeof(Line) && Line[Offset] != 0) {
            Offset++;
        }
        if (Offset + 1 < sizeof(Line)) {
            Line[Offset] = '\n';
            Line[Offset + 1] = 0;
        }
        HalSerialBootMark(Line);
        HalSerialBootMark(
            "Smp: Single CPU (board probe only, no bring-up)\n");
        return 0;
    }
#endif

    Dtb = gSmpDtbPhys ? gSmpDtbPhys : ARM64_VIRT_DTB_ADDR;
    Want = DtbCpuCount(Dtb);
    if (Want > HAL_MAX_CPUS) {
        Want = HAL_MAX_CPUS;
    }
    if (Want < 1) {
        Want = 1;
    }

    SmpLog("Smp: DTB CPUs=");
    SmpLogHex32((UINT32)Want);
    SmpLog("\n");

    if (Want <= 1) {
        SmpLog("Smp: Single CPU\n");
        return 0;
    }

    for (i = 1; i < Want; i++) {
        INT64 Rc;
        int Tries;

        gApReady[i] = 0;
        Rc = PsciCpuOn((UINT64)i, (UINT64)(UINTN)HalApEntry, (UINT64)i);
        if (Rc != 0) {
            SmpLog("Smp: PSCI CPU_ON Fail CPU=");
            SmpLogHex32((UINT32)i);
            SmpLog(" rc=");
            SmpLogHex64((UINT64)Rc);
            SmpLog("\n");
            continue;
        }
        Tries = 2000000;
        while (gApReady[i] != SMP_READY_MAGIC && Tries-- > 0) {
            HalCpuRelax();
        }
        if (gApReady[i] != SMP_READY_MAGIC) {
            SmpLog("Smp: AP Timeout CPU=");
            SmpLogHex32((UINT32)i);
            SmpLog("\n");
            continue;
        }
        Started++;
    }

    gCpuCount = 1 + Started;
    SmpLog("Smp: APs Started=");
    SmpLogHex32((UINT32)Started);
    SmpLog(" hellos=");
    SmpLogHex32(gApHelloCount);
    SmpLog("\n");

    if (Started > 0) {
        int Wait;
        for (Wait = 0; Wait < 40; Wait++) {
            if (gCpuTicks[1] != 0) {
                break;
            }
            DelayLoops(20000);
        }
    }
    SmpLog("Smp: Ticks");
    for (i = 0; i < gCpuCount && i < HAL_MAX_CPUS; i++) {
        SmpLog(" cpu");
        SmpLogHex32((UINT32)i);
        SmpLog("=");
        SmpLogHex64(gCpuTicks[i]);
    }
    SmpLog("\n");
    if (Started == 0 && Want > 1) {
        SmpLog("Smp: Continue Single-CPU (AP Failed)\n");
    }
    return 0;
}
