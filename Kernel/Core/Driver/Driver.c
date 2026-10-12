/*
 * Driver.c — InitializeDriver（开机模块编织）
 *
 * 人话：枚举设备、注册驱动，并只早 Probe Block 类（勿 ProbeAll）。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Driver.h"
#include "Device.h"
#include "ToySerialLog.h"

int InitializeDriver(void) {
    /*
     * PR-D2：只早 Probe Block（ATA PIO 无需 MMIO）。
     * 勿 ProbeAll：VMM 前 xHCI/AHCI/NVMe/Net 本会跳过，但 ps2-kbd 会跑 Ps2InitHw；
     * 真机无经典 8042 时 STATUS 常浮空 0xFF（OBF 永真）→ 排空 while 死循环，屏停 [Mod] driver。
     * Input / Net 仍由后续 usb / network 模块 Probe。
     */
    DeviceInitialize();
    DeviceEnumerateAll();
    ToyLogBoot("Boot: Device Enumerate Done\n");
    HalDriverRegister();
    ToyLogBoot("Boot: Driver Register OK\n");
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_BLOCK);
    ToyLogBoot("Boot: Driver Block Probe Done\n");
    return 0;
}
