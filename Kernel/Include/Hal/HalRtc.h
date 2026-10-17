/*
 * HalRtc.h — 固件实时钟
 */
#ifndef HAL_RTC_H
#define HAL_RTC_H

#include "BootTypes.h"

/* 成功 0；无 Runtime / GetTime 失败返回非 0（失败时勿再调以免踩缺页） */
int HalRtcGetTime(UINT16 *Year, UINT8 *Month, UINT8 *Day,
                  UINT8 *Hour, UINT8 *Minute, UINT8 *Second);

#endif
