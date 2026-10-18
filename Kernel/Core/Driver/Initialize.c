/*
 * Initialize.c — InitializeDriver（开机模块表；VMM 之后）
 *
 * 枚举设备、注册驱动，并只 Probe Block（勿 ProbeAll）。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Driver.h"
#include "Device.h"
#include "ToySerialLog.h"

int InitializeDriver(void) {
    /*
     * 只 Probe Block：ATA PIO / AHCI / NVMe 在 VMM 后可绑 MMIO。
     * 勿 ProbeAll：ps2-kbd 会跑 Ps2InitHw；真机无 8042 时 STATUS 浮空 → 死循环。
     * Input / Net 仍由后续 usb / network 模块 Probe。
     */
    DeviceInitialize();
    DeviceEnumerateAll();
    ToyLogBoot("Boot: Device Enumerate Done\n");
    HalDriverRegister();
    ToyLogBoot("Boot: Driver Register OK\n");
    (void)DisplayHardwareInitialize();
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_BLOCK);
    ToyLogBoot("Boot: Driver Block Probe Done\n");
    return 0;
}
