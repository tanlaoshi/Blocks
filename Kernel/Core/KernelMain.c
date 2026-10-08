/*
 * KernelMain.c — Common 大门（桩）
 *
 * 三架构 KernelHandoff 填完 BOOT_INFO 后进入此处。
 * X64：在此启用 4GiB 早期恒等页表（EarlyIdentity），再空转。
 * 完整模块表未迁入。
 */
#include "BootInfo.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "EarlyIdentity.h"
#endif

void KernelMain(const BOOT_INFO *Info) {
    BootInfoSet(Info);

#if defined(__x86_64__) || defined(_M_X64)
    if (EarlyIdentitySetup() == 0) {
        EarlyIdentityEnable();
    }
#endif

    for (;;) {
    }
}
