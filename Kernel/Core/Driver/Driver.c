/*
 * Driver.c — Driver 模块表项（Device 壳 + 枚举入口）
 */
#include "Driver.h"
#include "Device.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

int DriverInitialize(void) {
    DeviceInitialize();
    DeviceEnumerateAll();
    HalSerialWriteChannel(TOY_SLOG_DRV, "Driver: shell ok\n");
    return 0;
}
