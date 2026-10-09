/*
 * Serial.c — Serial 模块表项（early 已开串口；此处再确保一次）
 */
#include "Serial.h"
#include "HalSerial.h"

int SerialInitialize(void) {
    HalSerialInitialize();
    return 0;
}
