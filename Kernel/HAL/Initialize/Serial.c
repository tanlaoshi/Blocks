/*
 * Serial.c — InitializeSerial（开机模块表；Hal 本层）
 *
 * 尽早挂串口；并给 Library 挂 LibWrite。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "LibWrite.h"

int InitializeSerial(void) {
    HalSerialInitialize();
    HalSerialEnableRxIrq();
    LibWriteRegister(HalSerialWrite);
    return 0;
}
