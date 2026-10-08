/*
 * VirtualMemory.c — PR-K5：认领早期恒等分页（最小子集）
 *
 * 【初学者】
 * X64：KernelMain 已 EarlyIdentityEnable；这里读 CR0.PG / CR3，
 * 并确认帧缓冲物理址落在 TOY_IDENTITY_BYTES 内（QEMU GOP 通常如此）。
 * Arm/RiscV：本刀成功返回（MMU 另刀），保证三架构可编。
 *
 * 【积木】框架壳；页表策略 Ops 以后再拆。
 */
#include "VirtualMemory.h"
#include "BootInfo.h"
#include "IdentityMap.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "EarlyIdentity.h"
#endif

static int gVmReady;

static void VmLog(const char *Text) {
    HalSerialWriteChannel(TOY_SLOG_MEM, Text);
}

#if defined(__x86_64__) || defined(_M_X64)
static void VmLogHex64(UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWriteChannel(TOY_SLOG_MEM, Buf);
}

static int Cr0PagingOn(void) {
    UINT64 Cr0;

    __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0));
    return (Cr0 & (1ull << 31)) ? 1 : 0;
}

static UINT64 ReadCr3(void) {
    UINT64 Cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(Cr3));
    return Cr3;
}
#endif

int VirtualMemoryMapIdentity(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return 0;
    }
    if (Phys >= TOY_IDENTITY_BYTES) {
        return -1;
    }
    if (Size > TOY_IDENTITY_BYTES - Phys) {
        return -1;
    }
    /* K5：窗内已由 EarlyIdentity 铺好；窗外映射留给后续刀 */
    return 0;
}

int VirtualMemoryInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gVmReady = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (!Cr0PagingOn()) {
        VmLog("VMM: CR0.PG off (EarlyIdentity missing?)\n");
        return -1;
    }
    {
        UINT64 Root = EarlyIdentityRoot();
        UINT64 Cr3 = ReadCr3();

        VmLog("VMM: PG on cr3=");
        VmLogHex64(Cr3);
        VmLog(" root=");
        VmLogHex64(Root);
        VmLog("\n");
        if (Root != 0 && (Cr3 & ~0xFFFull) != (Root & ~0xFFFull)) {
            VmLog("VMM: warn CR3!=EarlyRoot\n");
        }
    }
    if (Info != 0 && Info->FrameBufferBase != 0 && Info->FrameBufferSize != 0) {
        if (VirtualMemoryMapIdentity(Info->FrameBufferBase,
                                     Info->FrameBufferSize) != 0) {
            VmLog("VMM: FB outside identity window\n");
            return -1;
        }
        VmLog("VMM: FB in identity window\n");
    }
#else
    (void)Info;
    VmLog("VMM: stub ok (no MMU knife yet)\n");
#endif

    gVmReady = 1;
    return 0;
}

void VirtualMemoryEnable(void) {
#if defined(__x86_64__) || defined(_M_X64)
    /* 已在 EarlyIdentityEnable 打开；保留 API 与现网同形 */
    (void)gVmReady;
#else
    (void)gVmReady;
#endif
}
