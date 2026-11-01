/*
 * HalI2c.h — 通用 I2C 契约（三架构共用，板级实现）
 *
 * 谁实现：HAL/<Arch>/Board/<board>/I2c/I2c.c（强符号）
 *         没实现的板子落到 HAL/Common/I2c/I2cDefault.c（弱符号，一律 -1）
 * 谁用：   Services/Shell/Commands/I2c.c，以及将来的传感器/EEPROM/触摸驱动
 *
 * 约定：
 *   · 只有"单主 + 7 位地址"这一种形态（够传感器/EEPROM/触摸用）。
 *   · 每次调用自带 START/STOP（不做 combined repeat-start），寄存器读 = 写寄存器号 + 读数据两步，
 *     这是绝大多数 I2C 器件的标准时序。
 *   · 返回 0 = 成功；非 0 = 无应答/总线错/不支持。绝不把"无应答"当成 0。
 */
#ifndef HAL_I2C_H
#define HAL_I2C_H

#include "BootTypes.h"

/* 探测地址是否有器件应答（0 = 有 ACK） */
int HalI2cProbe(UINT8 Address);

/* 从器件寄存器读 Len 字节（先写寄存器号，再读） */
int HalI2cRead(UINT8 Address, UINT8 Register, UINT8 *Buffer, UINT32 Length);

/* 向器件寄存器写 Len 字节 */
int HalI2cWrite(UINT8 Address, UINT8 Register, const UINT8 *Buffer, UINT32 Length);

/* 本板 I2C 总线是否可用（0 = 不可用），用于命令层给出准确提示 */
int HalI2cAvailable(void);

/*
 * 诊断：把关键寄存器填进 Out[]（板级自己定义含义；没实现返回 0）
 *   Pi: [0]=GPFSEL0 [1]=DIV [2]=C [3]=S [4]=最后一次探测的返回值
 */
int HalI2cDiagRead(UINT32 Out[5]);

/*
 * 读 SDA/SCL 的静止电平（把两脚临时切成输入，读完恢复 I2C 复用）。
 *   ！两根都应是 1（上拉）；任一根是 0 说明被拉死/接错到 GND。
 * 返回 0 = 成功。
 */
int HalI2cLineLevels(int *OutSdaHigh, int *OutSclHigh);

#endif
