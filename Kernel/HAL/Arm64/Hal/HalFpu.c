/*
 * HalFpu.c — Arm64 FP/SIMD 岛（rpi-12 TTF 栅格）
 *
 * CPACR_EL1.FPEN=0b11；若在 EL2 再清 CPTR_EL2.TFP。
 * Begin/End 保存 32×Q + FPSR/FPCR；禁抢占至 End（对齐 X64）。
 */
#include "Compiler.h"
#include "Hal.h"
#include "HalDriverSched.h"

#define CPACR_FPEN_MASK (3ULL << 20)
#define CPACR_FPEN_FULL (3ULL << 20)
#define CPTR_TFP        (1ULL << 10)

typedef struct {
    UINT64 Q[64];
    UINT32 Fpsr;
    UINT32 Fpcr;
} FPSIMD_FRAME;

static int gCapable;
static int gOk;
static UINT8 gDepth[HAL_MAX_CPUS];
static FPSIMD_FRAME gFx[HAL_MAX_CPUS] BLOCKS_ALIGNED(16);

int HalFpuFpProbe(void);

static UINT32 CpuIndex(void) {
    UINT32 Id = HalCpuGetId();
    if (Id >= HAL_MAX_CPUS) {
        return 0;
    }
    return Id;
}

static void FpsimdSave(FPSIMD_FRAME *F) {
    UINT64 *Q = F->Q;
    UINT32 Fpsr;
    UINT32 Fpcr;

    __asm__ volatile(
        "stp q0,  q1,  [%2, #0]\n"
        "stp q2,  q3,  [%2, #32]\n"
        "stp q4,  q5,  [%2, #64]\n"
        "stp q6,  q7,  [%2, #96]\n"
        "stp q8,  q9,  [%2, #128]\n"
        "stp q10, q11, [%2, #160]\n"
        "stp q12, q13, [%2, #192]\n"
        "stp q14, q15, [%2, #224]\n"
        "stp q16, q17, [%2, #256]\n"
        "stp q18, q19, [%2, #288]\n"
        "stp q20, q21, [%2, #320]\n"
        "stp q22, q23, [%2, #352]\n"
        "stp q24, q25, [%2, #384]\n"
        "stp q26, q27, [%2, #416]\n"
        "stp q28, q29, [%2, #448]\n"
        "stp q30, q31, [%2, #480]\n"
        "mrs %0, fpsr\n"
        "mrs %1, fpcr\n"
        : "=r"(Fpsr), "=r"(Fpcr)
        : "r"(Q)
        : "memory");
    F->Fpsr = Fpsr;
    F->Fpcr = Fpcr;
}

static void FpsimdRestore(const FPSIMD_FRAME *F) {
    const UINT64 *Q = F->Q;
    UINT32 Fpsr = F->Fpsr;
    UINT32 Fpcr = F->Fpcr;

    __asm__ volatile(
        "ldp q0,  q1,  [%0, #0]\n"
        "ldp q2,  q3,  [%0, #32]\n"
        "ldp q4,  q5,  [%0, #64]\n"
        "ldp q6,  q7,  [%0, #96]\n"
        "ldp q8,  q9,  [%0, #128]\n"
        "ldp q10, q11, [%0, #160]\n"
        "ldp q12, q13, [%0, #192]\n"
        "ldp q14, q15, [%0, #224]\n"
        "ldp q16, q17, [%0, #256]\n"
        "ldp q18, q19, [%0, #288]\n"
        "ldp q20, q21, [%0, #320]\n"
        "ldp q22, q23, [%0, #352]\n"
        "ldp q24, q25, [%0, #384]\n"
        "ldp q26, q27, [%0, #416]\n"
        "ldp q28, q29, [%0, #448]\n"
        "ldp q30, q31, [%0, #480]\n"
        "msr fpsr, %1\n"
        "msr fpcr, %2\n"
        :
        : "r"(Q), "r"(Fpsr), "r"(Fpcr)
        : "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
          "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
          "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
          "v30", "v31");
}

void HalFpuEnableThisCpu(void) {
    UINT64 Cpacr;
    UINT64 Cptr;
    UINT32 El;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(El));
    El = (El >> 2) & 3u;
    if (El == 2u) {
        __asm__ volatile("mrs %0, cptr_el2" : "=r"(Cptr));
        Cptr &= ~CPTR_TFP;
        __asm__ volatile("msr cptr_el2, %0" ::"r"(Cptr) : "memory");
    }

    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(Cpacr));
    Cpacr = (Cpacr & ~CPACR_FPEN_MASK) | CPACR_FPEN_FULL;
    __asm__ volatile("msr cpacr_el1, %0" ::"r"(Cpacr) : "memory");
    __asm__ volatile("isb" ::: "memory");
    gCapable = 1;
}

int HalFpuBegin(void) {
    UINT32 Cpu;
    UINT64 Flags;

    if (!gCapable) {
        return 0;
    }
    Cpu = CpuIndex();
    HalDriverPreemptDisable();
    Flags = HalIrqSave();
    if (gDepth[Cpu] == 0) {
        FpsimdSave(&gFx[Cpu]);
    }
    if (gDepth[Cpu] < 255u) {
        gDepth[Cpu]++;
    }
    HalIrqRestore(Flags);
    return 1;
}

void HalFpuEnd(void) {
    UINT32 Cpu;
    UINT64 Flags;

    if (!gCapable) {
        return;
    }
    Cpu = CpuIndex();
    Flags = HalIrqSave();
    if (gDepth[Cpu] > 0) {
        gDepth[Cpu]--;
        if (gDepth[Cpu] == 0) {
            FpsimdRestore(&gFx[Cpu]);
        }
    }
    HalIrqRestore(Flags);
    HalDriverPreemptEnable();
}

int HalFpuOk(void) {
    return gOk;
}

int HalFpuSelfTest(void) {
    int Got;

    if (!gCapable) {
        HalFpuEnableThisCpu();
    }
    if (!gCapable) {
        gOk = 0;
        return -1;
    }
    if (!HalFpuBegin()) {
        gOk = 0;
        return -1;
    }
    Got = HalFpuFpProbe();
    HalFpuEnd();
    if (Got != 30) {
        gOk = 0;
        return -1;
    }
    gOk = 1;
    return 0;
}
