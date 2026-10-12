/*
 * Serial.c — InitializeSerial（开机模块编织）
 *
 * 人话：尽早挂串口收发中断，后面模块才能打日志。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"

int InitializeSerial(void) {
    HalSerialInitialize();
    HalSerialEnableRxIrq();
    return 0;
}
