/*
 * KernelMain.c — 三架构合流后的操作系统大门
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
 *   1. BootInfoSet          保存开机说明书
 *   2. （仅 X64）EarlyIdentity  打开 4GiB 恒等页表
 *   3. KernelAttachEarly    串口 + 视频；有 FB 则色块 + Font 一行 ASCII
 *   4. KernelModulesRunFull 跑模块表（当前只有 Serial）
 *   5. park
 *
 * 【积木】本文件是胶水：只编排，不写分配页 / 调度政策。
 */
#include "BootInfo.h"
#include "HalCapability.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "KernelModules.h"

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
    HalSerialWrite(Line);
}

/* 右上角色块 + 一行 ASCII：证明 LFB 可写、点阵可画 */
static void KernelVideoSelfTest(UINT32 Width, UINT32 Height) {
    const UINT32 Box = 64;
    UINT32 X;
    UINT32 Y;

    if (Width < Box + 8 || Height < Box + 8) {
        return;
    }
    X = Width - Box - 8;
    Y = 8;
    HalVideoFillRect(X, Y, Box, Box, 0x00FFFF00u); /* 青黄：易看见 */
    HalSerialWrite("KernelMain: video self-test (top-right box)\n");

    /* 左上角白字：K2 Font */
    if (Width >= 80 && Height >= 24) {
        HalVideoDrawStringAt(8, 8, "Blocks K2", 0x00FFFFFFu);
        HalSerialWrite("KernelMain: font self-test (DrawString)\n");
    }
}

static void KernelAttachEarly(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);
    UINT32 Width = 0;
    UINT32 Height = 0;

    HalSerialInitialize();
    HalVideoSet(&Video);
    HalSerialWrite("KernelMain: early ok\n");

    HalVideoGetSize(&Width, &Height);
    if (Info != 0 && Info->FrameBufferSize != 0 && Width != 0 && Height != 0) {
        KernelLogFrameBufferSize(Width, Height);
        KernelVideoSelfTest(Width, Height);
    }
}

void KernelMain(const BOOT_INFO *Info) {
    BootInfoSet(Info);

#if defined(__x86_64__) || defined(_M_X64)
    if (EarlyIdentitySetup() == 0) {
        EarlyIdentityEnable();
    }
#endif

    KernelAttachEarly();

    if (KernelModulesRunFull() != 0) {
        HalSerialWrite("KernelMain: modules failed\n");
        KernelParkForever();
    }

    HalSerialWrite("KernelMain: modules done; park\n");
    KernelParkForever();
}
