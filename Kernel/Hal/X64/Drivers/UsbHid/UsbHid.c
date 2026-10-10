/*
 * UsbHid.c — K52 编排：起控制器 → 枚举 → 对外 Poll
 */
#include "Internal.h"

UINT64 gCap;
UINT64 gOp;
UINT64 gDb;
UINT64 gRt;
UINT32 gCtxSize;
UINT32 gMaxPorts;
UINT32 gMaxSlots;
int gDriverReady;
int gKbdOk;
int gMouseOk;

UINT64 *gDcbaa;
TRANSFER_REQUEST_BLOCK *gCmdRing;
RING_STATE gCmd;
TRANSFER_REQUEST_BLOCK *gEvtRing;
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

USB_HID_DEVICE gKbd;
USB_HID_DEVICE gMouse;
USB_HID_DEVICE *gTransferDevice;

char gCharQ[32];
UINT8 gCharLen;
HAL_MOUSE_PACKET gMouseQ[16];
UINT8 gMouseQr;
UINT8 gMouseQw;

int HalUsbHidInitialize(void) {
    UINT64 Virt;

    gDriverReady = 0;
    gKbdOk = 0;
    gMouseOk = 0;
    Zero(&gKbd, sizeof(gKbd));
    Zero(&gMouse, sizeof(gMouse));
    gCharLen = 0;
    gMouseQr = 0;
    gMouseQw = 0;

    if (!UsbXhciReady()) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: skip (no xhci)\n");
        return 0;
    }
    Virt = UsbXhciVirt();
    if (Virt == 0) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: skip (virt=0)\n");
        return 0;
    }
    gCap = Virt;

    if (ControllerStart() != 0) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: WARN controller start fail\n");
        return 0;
    }
    HalSerialWriteChannel(SLOG_USB, "UsbHid: controller ok\n");

    if (EnumAndBind() != 0) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: WARN no kbd/tablet\n");
        gDriverReady = 1; /* 控制器在跑；输入回落 PS/2 */
        return 0;
    }

    gDriverReady = 1;
    if (gKbdOk) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: kbd ok\n");
    }
    if (gMouseOk) {
        HalSerialWriteChannel(SLOG_USB, gMouse.Absolute ? "UsbHid: tablet ok\n" : "UsbHid: mouse ok\n");
    }
    return 0;
}

int HalUsbHidKeyboardReady(void) {
    return gDriverReady && gKbdOk;
}

int HalUsbHidMouseReady(void) {
    return gDriverReady && gMouseOk;
}

void HalUsbHidService(void) {
    if (!gDriverReady) {
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
