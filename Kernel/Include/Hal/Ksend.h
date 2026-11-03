/*
 * Ksend.h — 增量写卡门面（三架构 Common）
 *
 * 人话：不拔卡，PC 只发和卡上不一样的块，写完可选复位。
 * 传输：串口（备份）或 TCP（网络热更主路；有线/Wi‑Fi 同 listen）。
 * 实现：HAL/Common/Ksend/；壳 Commands/Ksend.c 弱引用。
 */
#ifndef HAL_KSEND_H
#define HAL_KSEND_H

#include "BootTypes.h"

int KsendInfo(void);
int KsendSelfTest(void);
int KsendTransfer(const char *TargetPath, int Reboot, int Backup);
/* Port=0 → 默认 26992；WaitMilliseconds=0 → 一直等连接 */
int KsendServe(UINT16 Port, UINT32 WaitMilliseconds);
UINT16 KsendServeDefaultPort(void);

#endif /* HAL_KSEND_H */
