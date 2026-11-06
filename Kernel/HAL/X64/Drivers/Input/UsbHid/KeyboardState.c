/*
 * KeyboardState.c — 键盘槽、端点、报告缓冲。
 * 环、门铃、端口仍在 Xhci.c。
 */
#include "Compiler.h"
#include "UsbHid/Private.h"

UINT32 gSlotId;
UINT32 gIntrDci;
UINT8  gKeyboardInterface;
UINT8  gKeyboardParseScore;
UINT8  gKeyboardEndpointAddress;
UINT16 gKeyboardMaxPacketSize;
UINT8  gKeyboardEndpointInterval;
UINT8  gUseGetReport;
UINT8  gKeyboardPollReport;
UINT8  gKeyboardReportPrevious[8];
volatile UINT32 gGetReportBusy;
UINT8  gGetReportFails;
UINT32 gKeyboardRoute;
UINT8  gKeyboardHubSlot;
UINT8  gKeyboardTransactionTranslatorPort;

USB_KEYBOARD_REPORT gKeyboardQueue[KEYBOARD_QUEUE];
volatile UINT32 gKeyboardWriteIndex;
volatile UINT32 gKeyboardReadIndex;

UINT8 gReportBuf[8] BLOCKS_ALIGNED(64);
