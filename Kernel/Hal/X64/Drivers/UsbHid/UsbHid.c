/*
 * UsbHid.c — K52 编排：起控制器 → 枚举 → 对外 Poll
 */
#include "Private.h"

UINT64 gCap;
UINT64 gOp;
UINT64 gDb;
UINT64 gRt;
UINT32 gCtxSize;
UINT32 gMaxPorts;
UINT32 gMaxSlots;
int gHidUp;
int gKbdOk;
int gMouseOk;

UINT64 *gDcbaa;
Trb *gCmdRing;
RingState gCmd;
Trb *gEvtRing;
UINT32 gEvtDeq;
UINT32 gEvtCcs;
UINT8 *gErst;
UINT8 *gInCtx;
UINT8 *gCtrlBuf;

volatile UINT32 gCmdDone;
volatile UINT32 gCmdCode;
volatile UINT32 gCmdSlot;
volatile UINT32 gXferDone;
volatile UINT32 gXferCode;

HidDev gKbd;
HidDev gMouse;
HidDev *gXferDev;

char gCharQ[32];
UINT8 gCharLen;
HAL_MOUSE_PACKET gMouseQ[16];
UINT8 gMouseQr;
UINT8 gMouseQw;

int HalUsbHidInitialize(void) {
    UINT64 Virt;

    gHidUp = 0;
    gKbdOk = 0;
    gMouseOk = 0;
    Zero(&gKbd, sizeof(gKbd));
    Zero(&gMouse, sizeof(gMouse));
    gCharLen = 0;
    gMouseQr = 0;
    gMouseQw = 0;

    if (!UsbXhciReady()) {
        HalSerialWriteChannel(SLOG_USB, "Hid: skip (no xhci)\n");
        return 0;
    }
    Virt = UsbXhciVirt();
    if (Virt == 0) {
        HalSerialWriteChannel(SLOG_USB, "Hid: skip (virt=0)\n");
        return 0;
    }
    gCap = Virt;

    if (ControllerStart() != 0) {
        HalSerialWriteChannel(SLOG_USB, "Hid: WARN controller start fail\n");
        return 0;
    }
    HalSerialWriteChannel(SLOG_USB, "Hid: controller ok\n");

    if (EnumAndBind() != 0) {
        HalSerialWriteChannel(SLOG_USB, "Hid: WARN no kbd/tablet\n");
        gHidUp = 1; /* 控制器在跑；输入回落 PS/2 */
        return 0;
    }

    gHidUp = 1;
    if (gKbdOk) {
        HalSerialWriteChannel(SLOG_USB, "Hid: kbd ok\n");
    }
    if (gMouseOk) {
        HalSerialWriteChannel(SLOG_USB, gMouse.Absolute ? "Hid: tablet ok\n" : "Hid: mouse ok\n");
    }
    return 0;
}

int HalUsbHidKeyboardReady(void) {
    return gHidUp && gKbdOk;
}

int HalUsbHidMouseReady(void) {
    return gHidUp && gMouseOk;
}

void HalUsbHidService(void) {
    if (!gHidUp) {
        return;
    }
    ProcessEvents();
}

int HalUsbHidPollChar(char *Out) {
    UINT8 i;

    if (Out == 0 || !HalUsbHidKeyboardReady()) {
        return 0;
    }
    HalUsbHidService();
    if (gCharLen == 0) {
        return 0;
    }
    *Out = gCharQ[0];
    gCharLen--;
    for (i = 0; i < gCharLen; i++) {
        gCharQ[i] = gCharQ[i + 1u];
    }
    return 1;
}

int HalUsbHidPollMouse(HAL_MOUSE_PACKET *Out) {
    if (Out == 0 || !HalUsbHidMouseReady()) {
        return 0;
    }
    HalUsbHidService();
    if (gMouseQr == gMouseQw) {
        return 0;
    }
    *Out = gMouseQ[gMouseQr];
    gMouseQr = (UINT8)((gMouseQr + 1u) % 16u);
    return 1;
}
