/*
 * HalPlat.c — PR-S3-hal-arm-1：架构名 / ELF / 调试 / 平台探测
 */
#include "Hal.h"
#include "BootHandoff.h"
#include "BoardConfig.h"

int HalHasFrameBuffer(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();

    return (Info != 0 && Info->FrameBufferSize != 0) ? 1 : 0;
}

/*
 * input-f3-console（arm64）：
 *   ConsoleOnly = !FB → KernelMainVirt（只 Shell；不 Create Input/Gui）
 *   Pin = 0（解钉）；若改 1，Boot 将 Interactive/Input 全钉 0（盖过 F2 门槛）
 * 有 FB 时走 VirtDesktop + KernelMainCommon（可按 MIN Create Input@CPU2）。
 */
int HalConsoleOnly(void) {
    return HalHasFrameBuffer() ? 0 : 1;
}

int HalPinInteractiveToBootstrap(void) {
    return 0;
}

int HalPlatformIsVirtSerialConsole(void) {
    /*
     * 历史名：实际门控「用 Virt 桌面模块表」。
     * virt：始终 1。
     * 真机板：有 FB 时也走 VirtDesktop（勿 KernelModulesRunFull / PC 驱动表）。
     */
    if (BOARD_IS_VIRT) {
        return 1;
    }
    return HalHasFrameBuffer() ? 1 : 0;
}

int HalCpuIsHypervisor(void) {
    return BOARD_IS_VIRT ? 1 : 0;
}

void HalVirtIdleLoop(void) {
    HalSerialWrite("virt: idle loop (no console)\n");
    for (;;) {
        HalCpuHalt();
    }
}

const char *HalArchName(void) { return "aarch64"; }
const char *HalCpuInfo(void) {
#if BOARD_IS_VIRT
    return "ARM64 (virt A14)";
#else
    return "ARM64";
#endif
}

UINT16 HalElfMachine(void) {
    return 183; /* EM_AARCH64 */
}

/* PR-A12：AArch64 ELF reloc → Common Elf.c 可处理 kind */
HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 1027: /* R_AARCH64_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 257:  /* R_AARCH64_ABS64 */
        return HAL_ELF_RELOC_ABS64;
    case 1025: /* R_AARCH64_GLOB_DAT */
        return HAL_ELF_RELOC_GLOB_DAT;
    case 1026: /* R_AARCH64_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 1024: /* R_AARCH64_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    UINT8 *P = (UINT8 *)Addr;
    UINT8 *End;
    UINT64 Line = 64;

    if (!Addr || Size == 0) {
        return;
    }
    End = P + Size;
    for (; P < End; P += Line) {
        __asm__ volatile("dc cvau, %0" ::"r"(P) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
    for (P = (UINT8 *)Addr; P < End; P += Line) {
        __asm__ volatile("ic ivau, %0" ::"r"(P) : "memory");
    }
    __asm__ volatile("dsb ish\n isb" ::: "memory");
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
