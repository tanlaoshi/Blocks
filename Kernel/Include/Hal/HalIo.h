/*
 * HalIo.h — 端口读写（x86 为 in/out；其它架构可为空操作或 MMIO 映射）
 */
#ifndef HAL_IO_H
#define HAL_IO_H

#include "BootTypes.h"

UINT8  HalIoRead8(UINT16 Port);
UINT16 HalIoRead16(UINT16 Port);
UINT32 HalIoRead32(UINT16 Port);
void   HalIoWrite8(UINT16 Port, UINT8 Value);
void   HalIoWrite16(UINT16 Port, UINT16 Value);
void   HalIoWrite32(UINT16 Port, UINT32 Value);

#endif
