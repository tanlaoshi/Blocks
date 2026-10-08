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
 *   3. KernelAttachEarly    串口 + 记下视频配置
 *   4. KernelModulesRunFull 跑模块表（当前只有 Serial）
 *   5. park                 后续模块挂上前停在这里
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

static void KernelAttachEarly(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);

    HalSerialInitialize();
    HalVideoSet(&Video);
    HalSerialWrite("KernelMain: early ok\n");
    if (Info != 0 && Info->FrameBufferSize != 0) {
        HalSerialWrite("KernelMain: frame buffer present\n");
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
