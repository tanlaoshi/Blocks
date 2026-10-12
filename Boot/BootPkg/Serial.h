/*
 * Serial.h — Boot 串口（实现在 Serial.c）
 *
 * 谁调用：Boot.c 开头 Initialize + Banner；其余文件用 Printf / Write。
 * 直接编程 0x3F8 的 16550，不走 UEFI 的 EFI_SERIAL_IO_PROTOCOL。
 */
#ifndef BOOT_SERIAL_H
#define BOOT_SERIAL_H

#include <Uefi.h>

void EFIAPI BootSerialInitialize(void);
int  EFIAPI BootSerialPresent(void);
void EFIAPI BootSerialWrite(const char *Text);
void EFIAPI BootSerialPrintf(CONST CHAR8 *Fmt, ...);
/* Boot 横幅（COM1 探测结果） */
void EFIAPI BootSerialBanner(void);

#endif
