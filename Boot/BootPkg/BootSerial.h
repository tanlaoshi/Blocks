/*
 * BootSerial.h — Boot 阶段 COM1 串口
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
