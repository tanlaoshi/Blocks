/*
 * SamsungUart.h — Exynos/S3C UART（Pixel 8 ttySAC0 / earlycon @BOARD_UART_BASE）
 *
 * 寄存器名对表 Linux serial_s3c；假定 ABL 已配好波特率，只做轮询收发。
 */
#ifndef HAL_SERIAL_SAMSUNG_UART_H
#define HAL_SERIAL_SAMSUNG_UART_H

#include "BootTypes.h"

void SamsungUartInitialize(void);
void SamsungUartPutByte(UINT8 Value);
int SamsungUartDataReady(void);
char SamsungUartReadByte(void);
void SamsungUartDrainTransmitter(void);

#endif /* HAL_SERIAL_SAMSUNG_UART_H */
