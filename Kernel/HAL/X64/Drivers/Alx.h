/*
 * Alx.h — Qualcomm Atheros alx（AR8161…）对外 API（PR-N-alx-2）
 *
 * Probe/MAC + TX/RX + NIC_L2；Bind → NetAttachNic。
 */
#ifndef ALX_H
#define ALX_H

#include "BootTypes.h"

int AlxAccept(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 Bar, UINT16 Did);
int AlxSetup(void);
int AlxReady(void);
void AlxGetMac(UINT8 Mac[6]);
UINT16 AlxPciDid(void);
int AlxSendFrame(const UINT8 *Frame, UINTN Len);
void AlxPoll(void);
void AlxGetRxStats(UINT32 *OkOut, UINT32 *DropOut);
int AlxGetLink(int *UpOut, UINT32 *MbpsOut, int *FullDuplexOut);

#endif
