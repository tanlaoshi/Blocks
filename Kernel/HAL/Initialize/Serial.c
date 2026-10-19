/*
 * Serial.c — SerialModuleInitialize（开机模块表；Hal 本层）
 *
 * 驱动里的 SerialInitialize 是 UART 本身。这里再开接收中断，并把串口交给 Library。
 *
 * 尽早挂串口；并给 Library 挂 LibWrite。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "LibWrite.h"

int SerialModuleInitialize(void) {
    HalSerialInitialize();
    HalSerialEnableRxIrq();
    LibWriteRegister(HalSerialWrite);
    return 0;
}
