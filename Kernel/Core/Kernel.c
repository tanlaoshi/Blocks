/*
 * Kernel.c — 三架构合流后的操作系统大门
 *
 * 【初学者 · 上电走到这里之前】
 *
 *   X64:
 *     UEFI → BOOTX64.EFI → 读 Kernel.elf
 *       → KernelEntry.S（换早期栈）
 *       → KernelHandoff：UEFI_BOOT_CONFIG 译成 BOOT_INFO
 *       → KernelMain(Info)   ← 你在这里
 *
 *   Arm64 / RiscV virt:
 *     加载器放入 Kernel.elf → KernelEntry.S（清 BSS、设栈）
 *       → KernelHandoff：设备树 / 约定 RAM 填 BOOT_INFO
 *       → KernelMain(Info)   ← 同一扇门
 *
 * 【门里当前顺序】
 *   1. BootInfoStore + HalCapabilityObserveFrameBuffer
 *   2. （仅 X64）EarlyIdentity  打开 4GiB 恒等页表
 *   3. KernelAttachEarly    串口 + 视频配置
 *   4. ModulesRunFull（… → Cpu → Scheduler → Console）
 *   5. ConsoleRun（串口提示符）
 *
 * 【积木】本文件是胶水：只编排，不写分配页 / 调度政策。
 */
#include "BootInfo.h"
#include "Console.h"
#include "HalCapability.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Modules.h"
#include "ToySerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "EarlyIdentity.h"
#endif

static void KernelParkForever(void) {
    for (;;) {
        HalCpuPark();
    }
}

/* 无 libc：把无符号整数写进 Buf，返回写入长度（不含 '\\0'） */
static int KernelFormatUint(char *Buf, int Cap, UINT32 Value) {
    char Tmp[10];
    int N = 0;
    int i;

    if (Cap <= 0) {
        return 0;
    }
    if (Value == 0) {
        Buf[0] = '0';
        return 1;
    }
    while (Value > 0 && N < (int)sizeof(Tmp)) {
        Tmp[N++] = (char)('0' + (Value % 10u));
        Value /= 10u;
    }
    if (N > Cap) {
        N = Cap;
    }
    for (i = 0; i < N; i++) {
        Buf[i] = Tmp[N - 1 - i];
    }
    return N;
}

static void KernelLogFrameBufferSize(UINT32 Width, UINT32 Height) {
    char Line[40];
    int N = 0;
    const char *P = "KernelMain: FB ";

    while (*P && N < 24) {
        Line[N++] = *P++;
    }
    N += KernelFormatUint(Line + N, (int)sizeof(Line) - N - 4, Width);
    if (N < (int)sizeof(Line) - 1) {
        Line[N++] = 'x';
    }
    N += KernelFormatUint(Line + N, (int)sizeof(Line) - N - 2, Height);
    if (N < (int)sizeof(Line) - 1) {
        Line[N++] = '\n';
    }
    Line[N] = 0;
    HalSerialWriteChannel(TOY_SLOG_BOOT, Line);
}

static void KernelAttachEarly(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);
    UINT32 Width = 0;
    UINT32 Height = 0;

    HalSerialInitialize();
    HalVideoSet(&Video);
    /* SCREEN_LOG=1 时开始往 FB 上滚（受 TOY_SCREEN_LOG_*） */
    HalSerialGopEnable();
    HalSerialWriteChannel(TOY_SLOG_BOOT, "KernelMain: early ok\n");

    HalVideoGetSize(&Width, &Height);
    if (Info != 0 && Info->FrameBufferSize != 0 && Width != 0 && Height != 0) {
        KernelLogFrameBufferSize(Width, Height);
    }
}

void KernelMain(const BOOT_INFO *Info) {
    BootInfoStore(Info);
    HalCapabilityObserveFrameBuffer(Info != 0 ? Info->FrameBufferSize : 0);

#if defined(__x86_64__) || defined(_M_X64)
    if (EarlyIdentitySetup() == 0) {
        EarlyIdentityEnable();
    }
#endif

    KernelAttachEarly();

    if (ModulesRunFull() != 0) {
        HalSerialWriteChannel(TOY_SLOG_BOOT, "KernelMain: modules failed\n");
        KernelParkForever();
    }

    HalSerialWriteChannel(TOY_SLOG_BOOT, "KernelMain: modules done\n");
    ConsoleRun();
    KernelParkForever();
}
