/*
 * KernelHandoff.c — BringUp/X64：编排 UEFI_BOOT_CONFIG → BOOT_INFO → KernelMain
 *
 * 入口栈切换在同目录 KernelEntry.S。
 * 译包见 HandoffConfig.c；内存图见 HandoffMemory.c；平台指针见 HandoffPlatform.c。
 * 交接包 UefiBootConfig.h 与 Boot/BootPkg/ 那份内容相同。
 */
#include "HandoffPrivate.h"
#include "Kernel.h"
#include "HalSerial.h"
#include "EarlyIdentity.h"

void KernelHandoff(UEFI_BOOT_CONFIG *BootConfig) {
    static BOOT_INFO Info;
    static UEFI_BOOT_CONFIG CfgCopy;

    if (BootConfig == 0) {
        for (;;) {
        }
    }

    CfgCopy = *BootConfig;
    HandoffFillConfig(&CfgCopy, &Info);
    HandoffFillPlatform(&CfgCopy);
    HandoffFillMemory(&CfgCopy, &Info, &CfgCopy);
    BootInfoSet(&Info);
    HalSerialInitialize();
    /* PMM/VMM 前开 4GiB 恒等；栈已在 KernelEntry 换到窗内 */
    if (EarlyIdentitySetup() == 0) {
        EarlyIdentityEnable();
    }
    KernelMain();
    for (;;) {
    }
}
