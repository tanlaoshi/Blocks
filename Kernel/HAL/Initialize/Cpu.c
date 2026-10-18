/*
 * Cpu.c — InitializeCpu（开机模块表；Hal 本层）
 *
 * HAL / 定时器 / syscall；其后 COM1 再探 + FPU 岛自检。virt 顺带挂 virtio 输入。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "ToySerialLog.h"

int InitializeCpu(void) {
    if (HalInitialize() != 0) {
        return -1;
    }
    HalTimerInitialize();
    HalSyscallInitialize();
    /* virt：仍在此挂 virtio-input；x86 真机在 file-system 前的 usb 模块（PR-H-msc-7a） */
    if (HalPlatformIsVirtSerialConsole()) {
        (void)HalUsbInitialize();
    }

    /*
     * IDT/LAPIC 已就绪：再探一次 COM1（首启 Serial 可能尚无中断），开 RX IRQ。
     * 勿在此扫 USB-UART（挂盘之后再认，板载 COM1 不影响）。
     */
    ToyLogBoot("Boot: COM1 after Cpu\n");
    HalSerialRetryIfMissing();
    HalSerialEnableRxIrq();
    if (HalSerialPresent()) {
        ToyLogBoot("Boot: COM1 Ready\n");
    } else {
        ToyLogBoot("Boot: COM1 Miss\n");
    }

    /* TTF/SSE 岛：须在 BSP、拉 AP 前做一次（原挂在 SerialEarly 名下，名实不符） */
    if (HalFpuSelfTest() == 0) {
        ToyLogBoot("Boot: fpu island ok\n");
    } else {
        ToyLogBoot("Boot: fpu island skip\n");
    }
    return 0;
}
