/*
 * I2cDefault.c — 没有 I2C 的板子（x64、Duo 尚未实现）的弱实现
 *
 * 为什么必须存在：命令层（Services/Shell/Commands/I2c.c）三架构共编，
 * 链接时必须有 HalI2c* 的落点；有 I2C 的板子给强符号盖掉这里。
 * 别改：必须是 __attribute__((weak))。
 */
#include "HalI2c.h"

__attribute__((weak)) int HalI2cAvailable(void) {
    return 0;
}

__attribute__((weak)) int HalI2cProbe(UINT8 Address) {
    (void)Address;
    return -1;
}

__attribute__((weak)) int HalI2cRead(UINT8 Address, UINT8 Register, UINT8 *Buffer, UINT32 Length) {
    (void)Address;
    (void)Register;
    (void)Buffer;
    (void)Length;
    return -1;
}

__attribute__((weak)) int HalI2cWrite(UINT8 Address, UINT8 Register, const UINT8 *Buffer, UINT32 Length) {
    (void)Address;
    (void)Register;
    (void)Buffer;
    (void)Length;
    return -1;
}

__attribute__((weak)) int HalI2cDiagRead(UINT32 Out[5]) {
    (void)Out;
    return 0;
}

__attribute__((weak)) int HalI2cLineLevels(int *OutSdaHigh, int *OutSclHigh) {
    (void)OutSdaHigh;
    (void)OutSclHigh;
    return -1;
}
