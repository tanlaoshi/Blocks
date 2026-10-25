/*
 * HalPlat.c — PR-S3-hal-riscv-1：架构名 / ELF / 调试 / 平台探测
 */
#include "Hal.h"
#include "BootHandoff.h"
#include "BoardConfig.h"

int HalHasFrameBuffer(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();

    return (Info != 0 && Info->FrameBufferSize != 0) ? 1 : 0;
}

/*
 * input-f3-console（riscv）：同 arm64 —
 *   ConsoleOnly=!FB → 只 Shell；Pin=0；有 FB 可按 MIN 分 Input。
 * VirtSerialConsole 另看 BOARD_IS_VIRT（模块表 VirtDesktop vs Full）。
 */
int HalConsoleOnly(void) {
    return HalHasFrameBuffer() ? 0 : 1;
}

int HalPinInteractiveToBootstrap(void) {
    return 0;
}

int HalPlatformIsVirtSerialConsole(void) {
    /* virt 平台形状（协作调度 / 桌面模块表）；真机板包 IS_VIRT=0（PR-B3） */
#if defined(BOARD_IS_VIRT) && BOARD_IS_VIRT
    return 1;
#else
    return 0;
#endif
}

int HalCpuIsHypervisor(void) {
#if defined(BOARD_IS_VIRT) && BOARD_IS_VIRT
    return 1;
#else
    return 0;
#endif
}

void HalVirtIdleLoop(void) {
    HalSerialWrite("virt: idle loop (no console)\n");
    for (;;) {
        HalCpuHalt();
    }
}

const char *HalArchName(void) { return "riscv64"; }
const char *HalCpuInfo(void) { return "RISC-V (virt A14)"; }

UINT16 HalElfMachine(void) {
    return 243; /* EM_RISCV */
}

/* PR-A12：RISC-V ELF reloc → Common Elf.c 可处理 kind */
HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 3: /* R_RISCV_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 2: /* R_RISCV_64 */
        return HAL_ELF_RELOC_ABS64;
    case 5: /* R_RISCV_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 4: /* R_RISCV_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

/*
 * C906（T-Head）：fence.i  alone 不保证 D 脏行落 DRAM / I 作废。
 * 写用户映像后须 dcache.cva + icache.iall（RT-Thread/Linux THEAD CMO）。
 * virt：无 thead 扩展，仅 fence.i。
 */
#define C906_CACHE_LINE 64ULL

void HalDcacheCleanInvalidateRange(void *Va, UINTN Len) {
#if defined(BOARD_IS_VIRT) && BOARD_IS_VIRT
    (void)Va;
    (void)Len;
#else
    UINT64 Start;
    UINT64 End;
    UINT64 A;

    if (!Va || Len == 0) {
        return;
    }
    Start = (UINT64)(UINTN)Va & ~(C906_CACHE_LINE - 1ULL);
    End = ((UINT64)(UINTN)Va + (UINT64)Len + C906_CACHE_LINE - 1ULL) &
          ~(C906_CACHE_LINE - 1ULL);
    for (A = Start; A < End; A += C906_CACHE_LINE) {
        __asm__ volatile(".insn i 0x0B, 0, x0, %0, 0x027" ::"r"(A) : "memory"); /* civa */
    }
    __asm__ volatile(".long 0x0190000b" ::: "memory"); /* sync.s */
#endif
}

void HalSyncICache(void *Addr, UINTN Size) {
#if defined(BOARD_IS_VIRT) && BOARD_IS_VIRT
    (void)Addr;
    (void)Size;
    __asm__ volatile("fence.i" ::: "memory");
#else
    UINT64 Start;
    UINT64 End;
    UINT64 A;

    /* 无 Addr 时整 D 回写（CoopDrain 入口） */
    if (!Addr || Size == 0) {
        __asm__ volatile(
            ".long 0x0010000b\n" /* dcache.call */
            ".long 0x0190000b\n" /* sync.s */
            ".long 0x0100000b\n" /* icache.iall */
            ".long 0x0190000b\n"
            "fence.i\n"
            ::: "memory");
        return;
    }
    Start = (UINT64)(UINTN)Addr & ~(C906_CACHE_LINE - 1ULL);
    End = ((UINT64)(UINTN)Addr + (UINT64)Size + C906_CACHE_LINE - 1ULL) &
          ~(C906_CACHE_LINE - 1ULL);
    for (A = Start; A < End; A += C906_CACHE_LINE) {
        __asm__ volatile(".insn i 0x0B, 0, x0, %0, 0x025" ::"r"(A) : "memory"); /* cva */
    }
    __asm__ volatile(
        ".long 0x0190000b\n" /* sync.s */
        ".long 0x0100000b\n" /* icache.iall */
        ".long 0x0190000b\n"
        "fence.i\n"
        ::: "memory");
#endif
}

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}
void HalDebugWriteHex32(UINT32 Value) {
    char Buf[9];
    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWrite(Buf);
}
void HalDebugHex64(UINT64 Value) {
    char Buf[17];
    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWrite(Buf);
}

/* HalCpuCount / Id / ticks / HalSmpStartApplicationProcessors → Smp.c（PR-A14） */
