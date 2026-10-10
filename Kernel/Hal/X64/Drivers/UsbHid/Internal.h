/*
 * Internal.h — UsbHid 内部（K52 · QEMU 根口键鼠）
 *
 * 【初学者】
 * - Hal/X64/Drivers/UsbHid：xHCI TRB/环/枚举共享声明；协议宏勿随意改名。
 * - 对外：HalUsbHid.h；编排 UsbHid.c。
 */
#ifndef USB_HID_INTERNAL_H
#define USB_HID_INTERNAL_H

#include "BootTypes.h"
#include "HalDma.h"
#include "HalPs2Mouse.h"
#include "HalSerial.h"
#include "HalUsbHid.h"
#include "SerialConfig.h"
#include "Usb.h"

#define TRANSFER_RING_SIZE     32u
#define EVENT_RING_SIZE      64u
#define DEVICE_CONTEXT_SLOTS   16u
#define CONTEXT_BYTES     2048u
#define INPUT_CONTEXT_BYTES   2112u

#define PORTSC_CCS    (1u << 0)
#define PORTSC_PED    (1u << 1)
#define PORTSC_PR     (1u << 4)
#define PORTSC_PP     (1u << 9)
#define PORTSC_PRC    (1u << 21)
#define PORTSC_WRC    (1u << 19)
#define PORTSC_CHANGE \
    ((1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | \
     (1u << 22) | (1u << 23))
#define PORTSC_RO \
    (PORTSC_CCS | (1u << 3) | (0xFu << 10) | (1u << 30))

#define USBCMD_RS     (1u << 0)
#define USBCMD_HCRST  (1u << 1)
#define USBSTS_HCH    (1u << 0)
#define USBSTS_EINT   (1u << 2)
#define USBSTS_CNR    (1u << 11)

#define TRB_C         (1u << 0)
#define TRB_TC        (1u << 1)
#define TRB_ISP       (1u << 2)
#define TRB_IOC       (1u << 5)
#define TRB_IDT       (1u << 6)
#define TRB_TYPE(t)   ((UINT32)(t) << 10)
#define TRB_SLOT(s)   ((UINT32)(s) << 24)
#define TRB_TRT_IN    (3u << 16)
#define TRB_TRT_OUT   (2u << 16)
#define TRB_DIR_IN    (1u << 16)
#define TRB_NORMAL    1u
#define TRB_SETUP     2u
#define TRB_DATA      3u
#define TRB_STATUS    4u
#define TRB_LINK      6u
#define TRB_ENABLE_SLOT   9u
#define TRB_ADDRESS_DEV   11u
#define TRB_CONFIG_EP     12u
#define TRB_EVALUATE_CTX  13u
#define TRB_TRANSFER_EVENT 32u
#define TRB_CMD_COMPLETION 33u
#define CC_SUCCESS        1u
#define CC_SHORT_PACKET   13u

typedef struct {
    UINT64 Parameter;
    UINT32 Status;
    UINT32 Control;
} __attribute__((packed, aligned(16))) TRANSFER_REQUEST_BLOCK;

typedef struct {
    UINT32 EnqueueIndex;
    UINT32 ProducerCycleState;
    UINT32 Size;
} RING_STATE;

typedef struct {
    UINT8 BmRequestType;
    UINT8 BRequest;
    UINT16 WValue;
    UINT16 WIndex;
    UINT16 WLength;
} __attribute__((packed)) SETUP_PACKET;

typedef struct {
    UINT32 SlotIdentifier;
    UINT32 Port;
    UINT8 Speed;
    UINT8 Interface;
    UINT8 EndpointAddress;
    UINT8 Interval;
    UINT8 Absolute; /* mouse tablet */
    UINT8 Protocol;
    UINT16 MaxPacketSize;
    UINT32 InterruptDeviceContextIndex;
    UINT8 *DeviceContext;
    TRANSFER_REQUEST_BLOCK *EndpointZeroRing;
    RING_STATE EndpointZero;
    TRANSFER_REQUEST_BLOCK *InterruptRing;
    RING_STATE Interrupt;
    UINT8 Report[8];
    UINT8 ReportReady;
    UINT8 PreviousKeys[8];
} USB_HID_DEVICE;

/* ---- 状态（UsbHid.c） ---- */
extern UINT64 gCapabilityBase;
extern UINT64 gOperationalBase;
extern UINT64 gDoorbellBase;
extern UINT64 gRuntimeBase;
extern UINT32 gContextSize;
extern UINT32 gMaxPorts;
extern UINT32 gMaxSlots;
extern int gDriverReady;
extern int gKeyboardOk;
extern int gMouseOk;

extern UINT64 *gDeviceContextBaseAddressArray;
extern TRANSFER_REQUEST_BLOCK *gCommandRing;
extern RING_STATE gCommand;
extern TRANSFER_REQUEST_BLOCK *gEventRing;
extern UINT32 gEventDequeue;
extern UINT32 gEventConsumerCycleState;
extern UINT8 *gEventRingSegmentTable;
extern UINT8 *gInputContext;
extern UINT8 *gControlBuffer;

extern volatile UINT32 gCommandDone;
extern volatile UINT32 gCommandCode;
extern volatile UINT32 gCommandSlot;
extern volatile UINT32 gTransferDone;
extern volatile UINT32 gTransferCode;

extern USB_HID_DEVICE gKeyboard;
extern USB_HID_DEVICE gMouse;
extern USB_HID_DEVICE *gTransferDevice;

extern char gCharQueue[32];
extern UINT8 gCharLength;
extern HAL_MOUSE_PACKET gMouseQueue[16];
extern UINT8 gMouseQueueRead;
extern UINT8 gMouseQueueWrite;

/* ---- 工具 ---- */
static inline void MemoryFence(void) {
    __asm__ volatile("mfence" ::: "memory");
}
static inline void CpuPause(void) {
    __asm__ volatile("pause");
}
static inline UINT64 PhysicalAddress(const void *P) {
    return (UINT64)(UINTN)P;
}
static inline UINT32 Read32(UINT64 A) {
    return *(volatile UINT32 *)(UINTN)A;
}
static inline void Write32(UINT64 A, UINT32 V) {
    *(volatile UINT32 *)(UINTN)A = V;
}
static inline UINT64 Read64(UINT64 A) {
    return *(volatile UINT64 *)(UINTN)A;
}
static inline void Write64(UINT64 A, UINT64 V) {
    *(volatile UINT64 *)(UINTN)A = V;
}
static inline void MemoryZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}
static inline UINT32 TransferRequestBlockType(UINT32 C) {
    return (C >> 10) & 0x3Fu;
}
static inline UINT64 PortRegister(UINT32 Port1) {
    return gOperationalBase + 0x400u + (UINT64)(Port1 - 1u) * 0x10u;
}
static inline UINT32 PortStatusControlNeutral(UINT32 V) {
    return (V & PORTSC_RO) | (V & ~PORTSC_CHANGE & ~PORTSC_RO & ~(1u << 1));
}
/* Input Control Context 占 1 个 Context Size；其后 Slot、EP1… */
static inline UINT8 *InputContextSlot(void) {
    return gInputContext + (UINTN)gContextSize;
}
static inline UINT8 *InputContextEndpoint(UINT32 DeviceContextIndex) {
    return gInputContext + (UINTN)gContextSize * (1u + (UINTN)DeviceContextIndex);
}

int RegisterWaitClear(UINT64 Reg, UINT32 Mask, UINT32 Spins);
int RegisterWaitSet(UINT64 Reg, UINT32 Mask, UINT32 Spins);

/* Ring / Command / Event / Transfer / Controller / Enum / Report */
void RingInitialize(TRANSFER_REQUEST_BLOCK *Ring, RING_STATE *State, UINT32 Size);
void RingEnqueue(TRANSFER_REQUEST_BLOCK *Ring, RING_STATE *State, UINT64 Param, UINT32 Status, UINT32 Control);
void DoorbellRing(UINT32 Slot, UINT32 Target);
void DeviceContextBaseAddressArraySet(UINT32 Slot, UINT64 P);

int CommandSubmit(UINT64 Param, UINT32 Control, UINT32 *SlotOut);
void EventProcess(void);

int ControlTransfer(SETUP_PACKET *Setup, void *Data);
int DescriptorGet(UINT16 TypeIndex, UINT16 Length, void *Buffer);

int ControllerStart(void);
int EnumerateAndBind(void);
/* Setup.c：SetConfig + 中断 EP；失败 -1 */
int DeviceConfigure(USB_HID_DEVICE *Device, int WantKeyboard);

void ReportKeyboard(const UINT8 *Report);
void ReportMouse(USB_HID_DEVICE *Device, UINT8 TransferLength);
void InterruptQueue(USB_HID_DEVICE *Device);

#endif
