/*
 * Serial.c — InitializeSerial（开机模块编织）
 *
 * 人话：尽早挂串口收发中断，后面模块才能打日志；并给 Library 挂 LibWrite。
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
