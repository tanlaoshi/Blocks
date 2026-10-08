/*
 * Serial.h — Boot 串口 API（Serial.c）
 */
#ifndef TOY_BOOT_SERIAL_H
#define TOY_BOOT_SERIAL_H

#include <Uefi.h>

void EFIAPI BootSerialInitialize(void);
int  EFIAPI BootSerialPresent(void);
void EFIAPI BootSerialWrite(const char *Text);
void EFIAPI BootSerialPrintf(CONST CHAR8 *Fmt, ...);
/* ToyBoot 横幅（COM1 探测结果） */
void EFIAPI BootSerialBanner(void);

#endif
