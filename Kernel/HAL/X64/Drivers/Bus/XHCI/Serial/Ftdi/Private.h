/*
 * Private.h — FTDI 收发和 Hub 共用的全局量。
 * 定义在 Ftdi.c；Bulk IN 环定义在 Receive.c。
 * gFtdiSlot / gFtdiPort / gFtdiClaimed / gFtdiBulkInDci / gFtdiBulkOutDci
 * 已在 XHCI/Private.h，这里不重复。
 */
#ifndef FTDI_PRIVATE_H
#define FTDI_PRIVATE_H

#include "XHCI/Private.h"

extern UINT16 gFtdiBulkOutMps;
extern UINT32 gFtdiRoute;
extern UINT8 gFtdiHubSlot;
extern UINT8 gFtdiTtPort;
extern UINT8 gFtdiDevCtx[2048];
extern XHCI_TRB gFtdiBulkOutRing[RING_SIZE];
extern RING_STATE gFtdiBulkOut;
extern XHCI_TRB gFtdiBulkInRing[RING_SIZE];

#endif
