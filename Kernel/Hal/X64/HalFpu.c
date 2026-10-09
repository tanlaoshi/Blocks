/*
 * HalFpu.c — K27：本核开 OSFXSR + Begin/End 岛（X64）
 *
 * 【初学者】
 * 内核默认 -mgeneral-regs-only，不能随便用 float。
 * stb_truetype 需要 SSE：先开 CR4.OSFXSR，栅格时 fxsave 保护现场。
 * 本文件本身不用 XMM（探针在 HalFpuSse.c）。
 */
#include "HalFpu.h"

#define CR0_EM           (1ull << 2)
#define CR0_MP           (1ull << 1)
#define CR0_TS           (1ull << 3)
#define CR4_OSFXSR       (1ull << 9)
#define CR4_OSXMMEXCPT   (1ull << 10)
#define CPUID_FXSR       (1u << 24)
#define CPUID_SSE        (1u << 25)
#define FX_BYTES         512u

static int gCapable;
static int gOk;
static UINT8 gDepth;
static UINT8 gFx[FX_BYTES] __attribute__((aligned(16)));

int HalFpuSseProbe(void);

static UINT32 CpuIdEdx1(void) {
    UINT32 A, B, C, D;
    __asm__ volatile("cpuid" : "=a"(A), "=b"(B), "=c"(C), "=d"(D)
                     : "a"(1u), "c"(0u));
    (void)A; (void)B; (void)C;
    return D;
}

static UINT64 IrqSave(void) {
    UINT64 Flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(Flags) :: "memory");
    return Flags;
}

static void IrqRestore(UINT64 Flags) {
    __asm__ volatile("push %0; popfq" :: "r"(Flags) : "memory", "cc");
}

void HalFpuEnableThisCpu(void) {
    UINT64 Cr0, Cr4;
    UINT32 Edx;

    Edx = CpuIdEdx1();
    if ((Edx & CPUID_FXSR) == 0 || (Edx & CPUID_SSE) == 0) {
        gCapable = 0;
        return;
    }
    gCapable = 1;

    __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0));
    Cr0 &= ~(CR0_EM | CR0_TS);
    Cr0 |= CR0_MP;
    __asm__ volatile("mov %0, %%cr0" :: "r"(Cr0) : "memory");

    __asm__ volatile("mov %%cr4, %0" : "=r"(Cr4));
    Cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    __asm__ volatile("mov %0, %%cr4" :: "r"(Cr4) : "memory");

    __asm__ volatile("fninit" ::: "memory");
}

int HalFpuBegin(void) {
    UINT64 Flags;
    if (!gCapable) {
        return 0;
    }
    Flags = IrqSave();
    if (gDepth == 0) {
        __asm__ volatile("fxsave %0" : "=m"(gFx) :: "memory");
    }
    if (gDepth < 255u) {
        gDepth++;
    }
    IrqRestore(Flags);
    return 1;
}

void HalFpuEnd(void) {
    UINT64 Flags;
    if (!gCapable) {
        return;
    }
    Flags = IrqSave();
    if (gDepth > 0) {
        gDepth--;
        if (gDepth == 0) {
            __asm__ volatile("fxrstor %0" :: "m"(gFx) : "memory");
        }
    }
    IrqRestore(Flags);
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
    Got = HalFpuSseProbe();
    HalFpuEnd();
    if (Got != 30) {
        gOk = 0;
        return -1;
    }
    gOk = 1;
    return 0;
}
