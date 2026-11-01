/*
 * Cpu.c — CpuInitialize（开机模块表；BringUp 胶水）
 *
 * HAL / 定时器 / syscall；其后 COM1 再探 + FPU 岛自检。virt 顺带挂 virtio 输入。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "SerialLog.h"

int CpuInitialize(void) {
    if (HalInitialize() != 0) {
        return -1;
    }
    HalTimerInitialize();
    HalSyscallInitialize();
    /*
     * VirtDesktop 表无 USB 行：Pi/virt 有屏须在此挂主机（鼠/键）。
     * IsVirtSerialConsole：无屏 virt 亦挂 virtio-input。
     * x64 Full 走模块表 USB（两门皆 0）；勿再只认 IsVirtSerialConsole——
     * 否则 Pi 拆 CoopDrain 后门后 DWC2 永不 Probe → 无鼠。
     */
    if (HalPlatformUseVirtDesktop() || HalPlatformIsVirtSerialConsole()) {
        (void)HalUsbInitialize();
    }

    /*
     * IDT/LAPIC 已就绪：再探一次 COM1（首启 Serial 可能尚无中断），开 RX IRQ。
     * 勿在此扫 USB-UART（挂盘之后再认，板载 COM1 不影响）。
     */
    LogBoot("Boot: COM1 after Cpu\n");
    HalSerialRetryIfMissing();
    HalSerialEnableRxIrq();
    if (HalSerialPresent()) {
        LogBoot("Boot: COM1 Ready\n");
    } else {
        LogBoot("Boot: COM1 Miss\n");
    }

    /* TTF/SSE 岛：须在 BSP、拉 AP 前做一次（原挂在 SerialEarly 名下，名实不符） */
    if (HalFpuSelfTest() == 0) {
        LogBoot("Boot: fpu island ok\n");
    } else {
        LogBoot("Boot: fpu island skip\n");
    }
    return 0;
}
