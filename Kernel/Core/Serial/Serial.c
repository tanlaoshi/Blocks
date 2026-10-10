/*
 * Serial.c — Serial 模块表项（early 已开串口；此处再确保一次）
 *
 * 【初学者】
 * - Core 层模块表「Serial」；KernelAttachEarly 已 HalSerialInitialize。
 * - 入口：SerialInitialize（ModulesRunFull，幂等）。
 * - 边界：UART 实现在 Hal/HalSerial.c。
 */
#include "Serial.h"
#include "HalSerial.h"

/*
 * SerialInitialize — Serial 模块表入口（幂等）
 *
 * 做什么：再次 HalSerialInitialize。
 * 谁调用：ModulesRunFull 第一模块。
 * 返回：0。
 */
int SerialInitialize(void) {
    HalSerialInitialize();
    return 0;
}
