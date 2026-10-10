/*
 * UsbHid.c — K52 编排：起控制器 → 枚举 → 对外 Poll
 */
#include "Internal.h"

UINT64 gCapabilityBase;
UINT64 gOperationalBase;
UINT64 gDoorbellBase;
UINT64 gRuntimeBase;
UINT32 gContextSize;
UINT32 gMaxPorts;
UINT32 gMaxSlots;
int gDriverReady;
int gKeyboardOk;
int gMouseOk;

UINT64 *gDeviceContextBaseAddressArray;
TRANSFER_REQUEST_BLOCK *gCommandRing;
RING_STATE gCommand;
TRANSFER_REQUEST_BLOCK *gEventRing;
UINT32 gEventDequeue;
UINT32 gEventConsumerCycleState;
UINT8 *gEventRingSegmentTable;
UINT8 *gInputContext;
UINT8 *gControlBuffer;

volatile UINT32 gCommandDone;
volatile UINT32 gCommandCode;
volatile UINT32 gCommandSlot;
volatile UINT32 gTransferDone;
volatile UINT32 gTransferCode;

USB_HID_DEVICE gKeyboard;
USB_HID_DEVICE gMouse;
USB_HID_DEVICE *gTransferDevice;

char gCharQueue[32];
UINT8 gCharLength;
HAL_MOUSE_PACKET gMouseQueue[16];
UINT8 gMouseQueueRead;
UINT8 gMouseQueueWrite;

int HalUsbHidInitialize(void) {
    UINT64 Virt;

    gDriverReady = 0;
    gKeyboardOk = 0;
    gMouseOk = 0;
    MemoryZero(&gKeyboard, sizeof(gKeyboard));
    MemoryZero(&gMouse, sizeof(gMouse));
    gCharLength = 0;
    gMouseQueueRead = 0;
    gMouseQueueWrite = 0;

    if (!UsbXhciReady()) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: skip (no xhci)\n");
        return 0;
    }
    Virt = UsbXhciVirt();
    if (Virt == 0) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: skip (virt=0)\n");
        return 0;
    }
    gCapabilityBase = Virt;

    if (ControllerStart() != 0) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: WARN controller start fail\n");
        return 0;
    }
    HalSerialWriteChannel(SLOG_USB, "UsbHid: controller ok\n");

    if (EnumerateAndBind() != 0) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: WARN no kbd/tablet\n");
        gDriverReady = 1; /* 控制器在跑；输入回落 PS/2 */
        return 0;
    }

    gDriverReady = 1;
    if (gKeyboardOk) {
        HalSerialWriteChannel(SLOG_USB, "UsbHid: kbd ok\n");
    }
    if (gMouseOk) {
        HalSerialWriteChannel(SLOG_USB, gMouse.Absolute ? "UsbHid: tablet ok\n" : "UsbHid: mouse ok\n");
    }
    return 0;
}

int HalUsbHidKeyboardReady(void) {
    return gDriverReady && gKeyboardOk;
}

int HalUsbHidMouseReady(void) {
    return gDriverReady && gMouseOk;
}

void HalUsbHidService(void) {
    if (!gDriverReady) {
        return;
    }
    EventProcess();
}

int HalUsbHidPollChar(char *Out) {
    UINT8 i;

    if (Out == 0 || !HalUsbHidKeyboardReady()) {
        return 0;
    }
    HalUsbHidService();
    if (gCharLength == 0) {
        return 0;
    }
    *Out = gCharQueue[0];
    gCharLength--;
    for (i = 0; i < gCharLength; i++) {
        gCharQueue[i] = gCharQueue[i + 1u];
    }
    return 1;
}

int HalUsbHidPollMouse(HAL_MOUSE_PACKET *Out) {
    if (Out == 0 || !HalUsbHidMouseReady()) {
        return 0;
    }
    HalUsbHidService();
    if (gMouseQueueRead == gMouseQueueWrite) {
        return 0;
    }
    *Out = gMouseQueue[gMouseQueueRead];
    gMouseQueueRead = (UINT8)((gMouseQueueRead + 1u) % 16u);
    return 1;
}
