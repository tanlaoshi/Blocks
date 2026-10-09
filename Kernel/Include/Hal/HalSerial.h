/*
 * HalSerial.h — 串口 HAL 门面（仅已用 API）
 *
 * 【初学者】
 * 上层写 HalSerialWriteChannel / WriteShell；不要 #ifdef 架构。
 * X64 COM1 / Arm64 PL011 / RiscV 16550。
 * TX 受 ToySerialConfig.h 约束。SCREEN_LOG 时 GopEnable 开屏上滚（HAL 自持字）。
 */
#ifndef HAL_SERIAL_H
#define HAL_SERIAL_H

#include "BootTypes.h"

void HalSerialInitialize(void);
int HalSerialPresent(void);

void HalSerialWrite(const char *Text);
void HalSerialWriteChannel(int Channel, const char *Text);
void HalSerialWriteChannelHex32(int Channel, UINT32 Value);
void HalSerialWriteChannelHex64(int Channel, UINT64 Value);
void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits);

void HalSerialShellOwn(void);
int HalSerialShellOwned(void);
void HalSerialWriteShell(const char *Text);

int HalSerialDataReady(void);
char HalSerialReadChar(void);

/* video 就绪后：允许 boot 期把日志画到 FB（SCREEN_LOG=1） */
void HalSerialGopEnable(void);

#endif
