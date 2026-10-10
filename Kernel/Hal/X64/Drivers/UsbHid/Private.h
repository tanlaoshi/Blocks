/*
 * Private.h — UsbHid 内部（K52 · QEMU 根口键鼠）
 */
#ifndef USB_HID_PRIVATE_H
#define USB_HID_PRIVATE_H

#include "BootTypes.h"
#include "HalDma.h"
#include "HalPs2Mouse.h"
#include "HalSerial.h"
#include "HalUsbHid.h"
#include "SerialConfig.h"
#include "Usb.h"

#define RING_SIZE     32u
#define EVT_SIZE      64u
#define DCBAA_SLOTS   16u
#define CTX_BYTES     2048u
#define INCTX_BYTES   2112u

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
} __attribute__((packed, aligned(16))) Trb;

typedef struct {
    UINT32 Enq;
    UINT32 Pcs;
    UINT32 Size;
} RingState;

typedef struct {
    UINT8 BmRequestType;
    UINT8 BRequest;
    UINT16 WValue;
    UINT16 WIndex;
    UINT16 WLength;
} __attribute__((packed)) SetupPkt;

typedef struct {
    UINT32 SlotId;
    UINT32 Port;
    UINT8 Speed;
    UINT8 Iface;
    UINT8 EpAddr;
    UINT8 Interval;
    UINT8 Absolute; /* mouse tablet */
    UINT8 Proto;
    UINT16 Mps;
    UINT32 IntrDci;
    UINT8 *DevCtx;
    Trb *Ep0Ring;
    RingState Ep0;
    Trb *IntrRing;
    RingState Intr;
    UINT8 Report[8];
    UINT8 ReportReady;
    UINT8 PrevKeys[8];
} HidDev;

/* ---- 状态（UsbHid.c） ---- */
extern UINT64 gCap;
extern UINT64 gOp;
extern UINT64 gDb;
extern UINT64 gRt;
extern UINT32 gCtxSize;
extern UINT32 gMaxPorts;
extern UINT32 gMaxSlots;
extern int gHidUp;
extern int gKbdOk;
extern int gMouseOk;

extern UINT64 *gDcbaa;
extern Trb *gCmdRing;
extern RingState gCmd;
extern Trb *gEvtRing;
extern UINT32 gEvtDeq;
extern UINT32 gEvtCcs;
extern UINT8 *gErst;
extern UINT8 *gInCtx;
extern UINT8 *gCtrlBuf;

extern volatile UINT32 gCmdDone;
extern volatile UINT32 gCmdCode;
extern volatile UINT32 gCmdSlot;
extern volatile UINT32 gXferDone;
extern volatile UINT32 gXferCode;

extern HidDev gKbd;
extern HidDev gMouse;
extern HidDev *gXferDev;

extern char gCharQ[32];
extern UINT8 gCharLen;
extern HAL_MOUSE_PACKET gMouseQ[16];
extern UINT8 gMouseQr;
extern UINT8 gMouseQw;

/* ---- 工具 ---- */
static inline void Fence(void) {
    __asm__ volatile("mfence" ::: "memory");
}
static inline void Pause(void) {
    __asm__ volatile("pause");
}
static inline UINT64 Phys(const void *P) {
    return (UINT64)(UINTN)P;
}
static inline UINT32 Rd32(UINT64 A) {
    return *(volatile UINT32 *)(UINTN)A;
}
static inline void Wr32(UINT64 A, UINT32 V) {
    *(volatile UINT32 *)(UINTN)A = V;
}
static inline UINT64 Rd64(UINT64 A) {
    return *(volatile UINT64 *)(UINTN)A;
}
static inline void Wr64(UINT64 A, UINT64 V) {
    *(volatile UINT64 *)(UINTN)A = V;
}
static inline void Zero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}
static inline UINT32 TrbType(UINT32 C) {
    return (C >> 10) & 0x3Fu;
}
static inline UINT64 PortReg(UINT32 Port1) {
    return gOp + 0x400u + (UINT64)(Port1 - 1u) * 0x10u;
}
static inline UINT32 PortscNeutral(UINT32 V) {
    return (V & PORTSC_RO) | (V & ~PORTSC_CHANGE & ~PORTSC_RO & ~(1u << 1));
}
/* Input Control Context 占 1 个 Context Size；其后 Slot、EP1… */
static inline UINT8 *InSlot(void) {
    return gInCtx + (UINTN)gCtxSize;
}
static inline UINT8 *InEp(UINT32 Dci) {
    return gInCtx + (UINTN)gCtxSize * (1u + (UINTN)Dci);
}

int WaitClear(UINT64 Reg, UINT32 Mask, UINT32 Spins);
int WaitSet(UINT64 Reg, UINT32 Mask, UINT32 Spins);

/* Ring / Command / Event / Transfer / Controller / Enum / Report */
void InitRing(Trb *Ring, RingState *St, UINT32 Size);
void Enqueue(Trb *Ring, RingState *St, UINT64 Param, UINT32 Status, UINT32 Control);
void RingDoorbell(UINT32 Slot, UINT32 Target);
void DcbaaSet(UINT32 Slot, UINT64 P);

int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut);
void ProcessEvents(void);

int ControlXfer(SetupPkt *Setup, void *Data);
int GetDesc(UINT16 TypeIndex, UINT16 Length, void *Buf);

int ControllerStart(void);
int EnumAndBind(void);
/* Setup.c：SetConfig + 中断 EP；失败 -1 */
int FinishHid(HidDev *D, int WantKbd);

void ReportKbdFeed(const UINT8 *Rep);
void ReportMouseFeed(HidDev *D, UINT8 XferLen);
void QueueIntr(HidDev *D);

#endif
