/*
 * Driver.c — Driver 模块表项（Device 壳 + 枚举入口）
 *
 * 【初学者】
 * - Core 层设备框架编排；真驱动在 Hal/ 注册（块/网/USB 等）。
 * - 入口：DriverInitialize → DeviceInitialize + DeviceEnumerateAll。
 * - 边界：本刀不扫 PCI；只做占位与日志。
 */
#include "Driver.h"
#include "Device.h"
#include "HalSerial.h"
#include "SerialConfig.h"

/*
 * DriverInitialize — Driver 模块表入口
 *
 * 做什么：Device 壳初始化 + 空枚举 + 日志。
 * 谁调用：ModulesRunFull（VirtualMemory 之后 Video 之前）。
 * 返回：0。
 */
int DriverInitialize(void) {
    DeviceInitialize();
    DeviceEnumerateAll();
    HalSerialWriteChannel(SLOG_DRV, "Driver: shell ok\n");
    return 0;
}
