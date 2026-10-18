/*
 * Private.h — CDC 收发共用的全局量。定义在 Cdc.c。
 * gCdcSlot / gCdcPort / gCdcClaimed / gCdcBulkInDci / gCdcBulkOutDci
 * 已在 XHCI/Private.h，这里不重复。
 */
#ifndef CDC_PRIVATE_H
#define CDC_PRIVATE_H

#include "XHCI/Private.h"

extern UINT16 gCdcBulkOutMps;
extern UINT16 gCdcBulkInMps;
extern UINT32 gCdcRoute;
extern UINT8 gCdcHubSlot;
extern UINT8 gCdcTtPort;
extern UINT8 gCdcDevCtx[2048];
extern XHCI_TRB gCdcBulkOutRing[RING_SIZE];
extern RING_STATE gCdcBulkOut;
extern XHCI_TRB gCdcBulkInRing[RING_SIZE];
extern RING_STATE gCdcBulkIn;

#endif
