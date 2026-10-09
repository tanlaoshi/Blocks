/*
 * HalXhci.h — xHCI 控制器门面（K14 最小）
 *
 * 【初学者】
 * Core/Usb 模块只认这张脸；X64 真读写 MMIO，其它 Arch 走 stub。
 * 本刀：附着虚址、主机复位、读端口 CCS。不建环、不枚举。
 */
#ifndef HAL_XHCI_H
#define HAL_XHCI_H

#include "BootTypes.h"

/* 附着已 Map 的 MMIO 虚址；校验 CAPLENGTH。成功 0 */
int HalXhciAttach(UINT64 Virt);
int HalXhciReady(void);
/* 主机控制器复位（HCRST）；超时也返回非 0，调用方软成功 */
int HalXhciReset(void);
/* 根口数量（HCSPARAMS1.MaxPorts）；未就绪则 0 */
UINT32 HalXhciPortCount(void);
/*
 * 读端口连接状态。Port 从 1 起。
 * 返回：1=CCS 置位，0=未连接，-1=无效。
 */
int HalXhciPortCcs(UINT32 Port);

#endif
