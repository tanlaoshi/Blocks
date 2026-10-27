/*
 * Ethernet.h — Pi3B LAN9514（smsc95xx）USB 网；rnet-2
 *
 * 门面名对齐 Duo：BoardEthernet* / Register。
 */
#ifndef BOARD_ETHERNET_H
#define BOARD_ETHERNET_H

#include "BootTypes.h"

int BoardEthernetProbe(void);
void BoardEthernetRegister(void);
int BoardEthernetSendFrame(const UINT8 *Frame, UINTN FrameLength);
int BoardEthernetGetLink(int *Up, UINT32 *Megabits, int *FullDuplex);

/*
 * UsbHost 枚举到 0424:ec00 后回调：完成 Smsc 初始化。
 * Hub/Port 供后续 bulk 选路（HS、无 SPLIT）。
 */
int BoardEthernetUsbAttach(UINT8 DevAddr, UINT8 Hub, UINT8 Port,
                           UINT8 EpBulkIn, UINT8 EpBulkOut, UINT16 BulkMps);

#endif
