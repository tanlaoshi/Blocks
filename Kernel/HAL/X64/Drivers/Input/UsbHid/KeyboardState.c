/*
 * KeyboardState.c — 键盘槽、端点、报告缓冲。
 * 环、门铃、端口仍在 Xhci.c。
 */
#include "UsbHid/Private.h"

UINT32 gSlotId;
UINT32 gIntrDci;
UINT8  gKbdIface;
UINT8  gKbdParseScore;
UINT8  gKbdEpAddr;
UINT16 gKbdMps;
UINT8  gKbdEpInterval;
UINT8  gUseGetReport;
UINT8  gKbdPollReport;
UINT8  gKbdReportPrev[8];
volatile UINT32 gGetReportBusy;
UINT8  gGetReportFails;
UINT32 gKbdRoute;
UINT8  gKbdHubSlot;
UINT8  gKbdTtPort;

USB_KEYBOARD_REPORT gKbdQ[KBD_Q];
volatile UINT32 gKeyboardWriteIndex;
volatile UINT32 gKeyboardReadIndex;

UINT8 gReportBuf[8] __attribute__((aligned(64)));
