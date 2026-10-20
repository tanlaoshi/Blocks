/*
 * HalPlat.c — PR-S3-hal-x64-1：架构名 / ELF / 调试 / 平台探测
 */
#include "Hal.h"
#include "BootInfo.h"

const char *HalArchName(void) {
    return "x86_64";
}

const char *HalCpuInfo(void) {
    return "x86-64 (Blocks HAL)";
}

/* PR-A4：ELF e_machine = EM_X86_64 */
UINT16 HalElfMachine(void) {
    return 62;
}

HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 8:  /* R_X86_64_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 1:  /* R_X86_64_64 */
        return HAL_ELF_RELOC_ABS64;
    case 6:  /* R_X86_64_GLOB_DAT */
        return HAL_ELF_RELOC_GLOB_DAT;
    case 7:  /* R_X86_64_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 5:  /* R_X86_64_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    (void)Addr;
    (void)Size;
}

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}

void HalDebugWriteHex32(UINT32 Value) {
    char Buf[12];

    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWrite(Buf);
}

void HalDebugHex64(UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWrite(Buf);
}

void HalCpuPark(void) {
    __asm__ volatile ("cli; hlt");
}

int HalHasFrameBuffer(void) {
    const BOOT_INFO *Info = BootInfoGet();

    return (Info != 0 && Info->FrameBufferSize != 0) ? 1 : 0;
}

/*
 * input-f3-console（x64）：两旗均恒 0 — 桌面走 Full + 默认钉核表。
 * ConsoleOnly / Pin 行为语义见 Arm/RiscV HalPlat 与 Scheduler/Start.c 头；
 * 改 x64 串口子集或强制钉 BSP 是行为刀，勿与 HAL_INPUT_CPU_MIN（F2）混改。
 */
int HalConsoleOnly(void) {
    return 0;
}

int HalPinInteractiveToBootstrap(void) {
    return 0;
}

int HalPlatformIsVirtSerialConsole(void) {
    return 0;
}

int HalCpuIsHypervisor(void) {
    UINT32 Eax;
    UINT32 Ebx;
    UINT32 Ecx;
    UINT32 Edx;

    __asm__ volatile("cpuid"
                     : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx)
                     : "a"(1)
                     : "memory");
    if ((Ecx & (1u << 31)) != 0) {
        return 1;
    }
    /*
     * 部分 QEMU/TCG 未置 ECX.31，但仍有 0x40000000 品牌叶。
     * 真机通常无此叶；只认常见签名，避免误判。
     */
    __asm__ volatile("cpuid"
                     : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx)
                     : "a"(0x40000000u)
                     : "memory");
    if (Eax < 0x40000000u) {
        return 0;
    }
    /* "KVMKVMKVM\0\0\0" */
    if (Ebx == 0x4B4D564Bu && Ecx == 0x564B4D56u && Edx == 0x0000004Du) {
        return 1;
    }
    /* "TCGTCGTCGTCG" */
    if (Ebx == 0x54434754u && Ecx == 0x43544743u && Edx == 0x47435447u) {
        return 1;
    }
    return 0;
}

void HalVirtPlatformIdleLoop(void) {
    for (;;) {
        HalCpuPark();
    }
}

void HalTimerPoll(void) {
}

/* SmpBoot.c 提供 HalCpuCount / HalCpuGetId / HalSmpStartApplicationProcessors */

void HalSmpNoteDtb(UINT64 DtbPhys) {
    (void)DtbPhys;
}
