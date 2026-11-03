/*
 * SmpBoard.c — psmp-2：Pi 真机按 enable-method 拉 AP（禁盲 HVC）
 *
 * spin-table：写 cpu-release-addr + SEV。
 * psci：对表 virt 的 CPU_ON（仅 method 明示时）。
 */
#include "Compiler.h"
#include "Hal.h"
#include "HalSerial.h"
#include "BoardConfig.h"
#include "Virtio/Dtb.h"
#include "CString.h"

#define PSCI_CPU_ON_64  0xC4000003ULL
#define SMP_READY_MAGIC 0x534D5052u /* 'SMPR' */

extern void HalApEntry(void);
extern volatile UINT32 gApReady[];
extern volatile UINT32 gApHelloCount;

static void BootMarkUnsigned(UINT32 Value) {
    char DigitBuffer[12];
    int DigitCount;
    int Index;

    DigitCount = 0;
    do {
        DigitBuffer[DigitCount++] = (char)('0' + (Value % 10u));
        Value /= 10u;
    } while (Value != 0u && DigitCount < (int)sizeof(DigitBuffer));
    for (Index = DigitCount; Index > 0; Index--) {
        char One[2];
        One[0] = DigitBuffer[Index - 1];
        One[1] = 0;
        HalSerialBootMark(One);
    }
}

static void BootMarkHex32(UINT32 Value) {
    static const char Digits[] = "0123456789abcdef";
    char Text[9];
    int Index;

    for (Index = 0; Index < 8; Index++) {
        Text[Index] = Digits[(Value >> (28 - Index * 4)) & 0xfu];
    }
    Text[8] = 0;
    HalSerialBootMark(Text);
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

static void BootMarkCpuLine(UINT32 CpuReported, const char *Method) {
    char Line[96];
    char DigitBuffer[12];
    UINT32 Value;
    int DigitCount;
    int Index;
    UINTN Offset;

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
    for (Index = DigitCount; Index > 0 && Offset + 1 < sizeof(Line); Index--) {
        Line[Offset++] = DigitBuffer[Index - 1];
    }
    StringCopy(Line + Offset, sizeof(Line) - Offset, " method=");
    Offset = 0;
    while (Offset + 1 < sizeof(Line) && Line[Offset] != 0) {
        Offset++;
    }
    StringCopy(Line + Offset, sizeof(Line) - Offset, Method ? Method : "(none)");
    Offset = 0;
    while (Offset + 1 < sizeof(Line) && Line[Offset] != 0) {
        Offset++;
    }
    if (Offset + 1 < sizeof(Line)) {
        Line[Offset] = '\n';
        Line[Offset + 1] = 0;
    }
    HalSerialBootMark(Line);
}

static int MethodIsPsci(const char *Method) {
    return Method != 0 && Method[0] == 'p' && Method[1] == 's' &&
           Method[2] == 'c' && Method[3] == 'i';
}

static int MethodIsSpinTable(const char *Method) {
    return Method != 0 && StringEqual(Method, "spin-table");
}

static int WaitApReady(UINT32 Logical, int Tries) {
    while (gApReady[Logical] != SMP_READY_MAGIC && Tries-- > 0) {
        HalCpuRelax();
    }
    return gApReady[Logical] == SMP_READY_MAGIC ? 1 : 0;
}

/*
 * Pi armstub / 多数 spin-table 槽只盯 **32-bit** 非零再跳（对表 bcm 低址 0xd8/e0…）。
 * 先前写 UINT64 宽槽，次核对低字/对齐敏感 → 全 timeout。本刀改写宽=32。
 */
static int BringUpSpinTable(UINT64 DtbPhys, int Want) {
    int Logical;
    int Started = 0;
    UINT32 EntryPhys = (UINT32)(UINTN)HalApEntry;

    for (Logical = 1; Logical < Want; Logical++) {
        UINT64 ReleasePhys;
        volatile UINT32 *Release;

        if (DtbCpuReleaseAddress(DtbPhys, (UINT32)Logical, &ReleasePhys) != 0) {
            HalSerialBootMark("Boot: Pi SMP spin-table no release-addr cpu=");
            BootMarkUnsigned((UINT32)Logical);
            HalSerialBootMark("\n");
            continue;
        }
        HalSerialBootMark("Boot: Pi SMP release cpu=");
        BootMarkUnsigned((UINT32)Logical);
        HalSerialBootMark(" addr=");
        BootMarkHex32((UINT32)ReleasePhys);
        HalSerialBootMark(" entry=");
        BootMarkHex32(EntryPhys);
        HalSerialBootMark(" width=32\n");

        Release = (volatile UINT32 *)(UINTN)ReleasePhys;
        *Release = EntryPhys;
        HalDcacheCleanInvalidateRange((void *)(UINTN)ReleasePhys, sizeof(UINT32));
        __asm__ volatile("dsb sy\n sev" ::: "memory");
        if (!WaitApReady((UINT32)Logical, 2000000)) {
            HalSerialBootMark("Boot: Pi SMP spin-table timeout cpu=");
            BootMarkUnsigned((UINT32)Logical);
            HalSerialBootMark("\n");
            continue;
        }
        Started++;
    }
    return Started;
}

static int BringUpPsci(int Want) {
    int Logical;
    int Started = 0;

    for (Logical = 1; Logical < Want; Logical++) {
        INT64 Result;

        gApReady[Logical] = 0;
        Result = PsciCpuOn((UINT64)Logical, (UINT64)(UINTN)HalApEntry,
                           (UINT64)Logical);
        if (Result != 0) {
            HalSerialBootMark("Boot: Pi SMP PSCI CPU_ON fail cpu=");
            BootMarkUnsigned((UINT32)Logical);
            HalSerialBootMark("\n");
            continue;
        }
        if (!WaitApReady((UINT32)Logical, 2000000)) {
            HalSerialBootMark("Boot: Pi SMP PSCI timeout cpu=");
            BootMarkUnsigned((UINT32)Logical);
            HalSerialBootMark("\n");
            continue;
        }
        Started++;
    }
    return Started;
}

/*
 * 返回已起 AP 数；*OutCpuCount = 1+Started。
 * DtbPhys 可为 0：会试 BOARD_DTB_LOAD（memory 模板坏时仍有 cpu 节点）。
 */
int SmpBoardStartApplicationProcessors(UINT64 DtbPhys, int *OutCpuCount) {
    char EnableMethod[48];
    int Want;
    int MethodOk;
    int Started = 0;
    UINT32 CpuReported;

    if (OutCpuCount == 0) {
        return 0;
    }
    *OutCpuCount = 1;

    if (DtbPhys == 0 || !DtbLooksValid(DtbPhys)) {
        if (DtbLooksValid(BOARD_DTB_LOAD)) {
            DtbPhys = BOARD_DTB_LOAD;
            HalSerialBootMark("Boot: Pi SMP dtb fallback BOARD_DTB_LOAD\n");
        }
    }

    Want = DtbPhys != 0 ? DtbCpuCount(DtbPhys) : 0;
    if (Want > HAL_MAX_CPUS) {
        Want = HAL_MAX_CPUS;
    }
    CpuReported = (UINT32)(Want > 0 ? Want : 0);
    MethodOk = (DtbPhys != 0 &&
                DtbCpuEnableMethod(DtbPhys, EnableMethod,
                                   (int)sizeof(EnableMethod)) == 0)
                   ? 1
                   : 0;
    BootMarkCpuLine(CpuReported, MethodOk ? EnableMethod : "(none)");

    if (DtbPhys == 0 || Want <= 1) {
        HalSerialBootMark(
            "Smp: Single CPU (board probe only, no bring-up)\n");
        return 0;
    }

    if (MethodIsSpinTable(EnableMethod)) {
        Started = BringUpSpinTable(DtbPhys, Want);
    } else if (MethodIsPsci(EnableMethod)) {
        Started = BringUpPsci(Want);
    } else {
        HalSerialBootMark("Boot: Pi SMP unsupported method — stay 1 CPU\n");
        return 0;
    }

    *OutCpuCount = 1 + Started;
    HalSerialBootMark("Boot: Pi SMP APs started=");
    BootMarkUnsigned((UINT32)Started);
    HalSerialBootMark(" hellos=");
    BootMarkUnsigned(gApHelloCount);
    HalSerialBootMark("\n");
    if (Started == 0) {
        HalSerialBootMark("Smp: Continue Single-CPU (AP Failed)\n");
        *OutCpuCount = 1;
    }
    return Started;
}
