/*
 * Private.h — 键鼠内部共享。
 * 不包含 xHCI 私头，也不放传输环结构。环在 Drivers/XHCI/。
 */
#ifndef USB_HID_PRIVATE_H
#define USB_HID_PRIVATE_H

#include "BootTypes.h"
#include "PCIe.h"
#include "Hal.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "SpinLock.h"
#include "UsbHostController.h"

#define USB_WVALUE_DT_DEVICE  0x0100u
#define USB_WVALUE_DT_CONFIG  0x0200u
#define USB_WVALUE_DT_HUB     0x2900u
#define USB_HID_REPORT_IN     0x0100u
#define PORTSC_CCS            (1u << 0)
#define KBD_Q 16
#define MOUSE_Q 32
#define DCBAA_SLOTS 16
#define HUB_PORT_CONNECTION   (1u << 0)
#define HUB_PORT_ENABLE       (1u << 1)
#define HUB_PORT_RESET        (1u << 4)
#define HUB_PORT_POWER        (1u << 8)
#define HUB_C_PORT_CONNECTION (1u << 16)
#define HUB_C_PORT_RESET      (1u << 20)
#define HUB_FEAT_PORT_RESET   4
#define HUB_FEAT_PORT_POWER   8
#define HUB_FEAT_C_PORT_CONNECTION 16
#define HUB_FEAT_C_PORT_RESET 20

#ifndef USB_KEYBOARD_REPORT_DEFINED
#define USB_KEYBOARD_REPORT_DEFINED
typedef struct {
    UINT8 ModifierKeys;
    UINT8 Reserved;
    UINT8 KeyCode[6];
} __attribute__((packed)) USB_KEYBOARD_REPORT;
#endif

#ifndef USB_MOUSE_REPORT_DEFINED
#define USB_MOUSE_REPORT_DEFINED
typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
    INT8   Wheel;
    UINT8  Absolute;
} USB_MOUSE_REPORT;
#endif

extern UINT64 gOperationalBase;
extern UINT32 gCtxSize;
extern UINT32 gMaxPorts;
extern UINT32 gPort1;
extern UINT8 gSpeed;
extern UINT32 gSlotId;
extern UINT32 gXferSlot;
extern UINT32 gIntrDci;
extern UINT8 gKbdIface;
extern UINT8 gKbdParseScore;
extern UINT8 gKbdEpAddr;
extern UINT16 gKbdMps;
extern UINT8 gKbdPollReport;
extern UINT32 gKbdRoute;
extern UINT8 gKbdHubSlot;
extern UINT8 gKbdTtPort;
extern USB_KEYBOARD_REPORT gKbdQ[KBD_Q];
extern volatile UINT32 gKeyboardWriteIndex;
extern volatile UINT32 gKeyboardReadIndex;

extern UINT32 gMouseSlotId;
extern UINT32 gMousePort;
extern UINT32 gMouseRoute;
extern UINT8 gMouseHubSlot;
extern UINT8 gMouseTtPort;
extern UINT32 gMouseIntrDci;
extern UINT8 gMouseIface;
extern UINT8 gMouseIfaceProto;
extern UINT8 gMouseParseScore;
extern UINT8 gMouseAbsolute;
extern UINT8 gMouseEpAddr;
extern UINT8 gMouseReportLen;
extern UINT8 gMouseXferLen;
extern UINT8 gMouseBuf[8];
extern int gMouseAbsX;
extern int gMouseAbsY;
extern int gMouseAbsInit;
extern UINT8 gMouseDevCtx[2048];
extern volatile UINT32 gMouseIntrDone;
extern volatile UINT32 gIntrReportReady;
extern volatile UINT32 gMouseReportReady;

extern UINT8 gDevCtx[2048];
extern UINT8 gInCtx[2048];
extern UINT8 gCtrlBuf[256];
extern UINT8 gReportBuf[8];
extern USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
extern volatile UINT32 gMouseWriteIndex;
extern volatile UINT32 gMouseReadIndex;
extern UINT32 gHubSlotId;
extern UINT32 gHubRootPort;
extern UINT8 gHubNumPorts;
extern UINT32 gPortNoHid;
extern UINT32 gPortNeedForcePr;
extern UINT8 gSlotEp0UsesKbdRing[DCBAA_SLOTS + 1];
extern UINT32 gCmdCode;
extern volatile UINT32 gIntrDone;
extern UINT8 gKbdReportPrev[8];
extern volatile UINT32 gGetReportBusy;
extern UINT8 gGetReportFails;
extern UINT8 gUseGetReport;
extern UINT8 gXferFast;
extern volatile UINT32 gStatIntrEvt;
extern volatile UINT32 gStatKbdPush;
extern SPIN_LOCK gHidQueueLock;

void BootLog(const char *Text);
void BootLogHex(const char *Prefix, UINT64 Value, int Digits);
void BootLogV(const char *Text);
void BootLogHexV(const char *Prefix, UINT64 Value, int Digits);
void EnumWhy(const char *Why);
UINT32 ReadMmio32(UINT64 Addr);
void DcbaaSet(UINT32 Slot, UINT64 Phys);
void DcbaaFlush(void);
int ResetPortEx(UINT32 Port1, int Force);
int ControlXfer(USB_SETUP *Setup, void *Data);
int GetDeviceDesc(void);
void DrainEvents(void);
int UsesIrq(void);
void FlushDma(const void *Ptr, UINTN Size);
void ZeroMemory(void *Ptr, UINTN Size);
void CopyMemory(void *Dst, const void *Src, UINTN Size);
UINT64 PointerToPhysical(const void *Ptr);
void StallMs(UINT32 Ms);

int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut);
int GetDesc(UINT16 TypeIndex, UINT16 Index, UINT16 Length, void *Buf);
int SetConfig(UINT8 Config);
int SetInterface(UINT8 Iface, UINT8 Alt);
int SetIdle(UINT8 Iface);
int SetProtocolBoot(UINT8 Iface);
void DisableSlot(UINT32 SlotId);
void RecoverEp0(UINT32 SlotId);
int AddressDeviceOnPort(UINT32 RootPort, UINT8 Speed, UINT32 *SlotOut,
                        UINT8 *DevCtx, UINT32 RouteString,
                        UINT8 ParentHubSlot, UINT8 TtPort,
                        int HubDevice, UINT8 HubNumPorts);
int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
                   int (*ParseFn)(UINT8 *, UINT16, UINT8, UINT8 *, UINT8 *,
                                  UINT16 *, UINT8 *),
                   int UseBootProto);
int IsHubDeviceDesc(void);
int ConfigHasHubIface(UINT8 *Cfg, UINT16 Total);
int ClaimHubOnRootPort(UINT32 RootPort, UINT8 Speed, UINT32 ExistingSlot);
int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed);
UINT32 PortReg(UINT32 Port1);
UINT8 PortSpeed(UINT32 Portsc);
int ResetPort(UINT32 Port1);

int ParseConfig(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval);
int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                     UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval);
int RealPcRejectMouseExtraAsKeyboard(UINT16 Total, UINT8 Speed);
int ConfigureIntr(UINT8 EpAddr, UINT16 Mps, UINT8 BInterval, UINT8 Speed,
                  UINT8 MouseEpAddr, UINT16 MouseMps, UINT8 MouseBInterval);
int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval,
                       UINT8 Speed);
void QueueIntr(void);
void QueueMouseIntr(void);
int SyncKeyboardIntr(void);
int PrepCompositeMouse(UINT16 Total, UINT8 Speed, UINT8 KbdIface, UINT8 KbdEp,
                       UINT8 *MouseEp, UINT16 *MouseMps, UINT8 *MouseIv);
int ClaimAddressedSlotAsMouse(UINT32 RootPort, UINT8 Speed, UINT16 Total,
                              UINT8 ConfigVal);
int HidGetInputReport(UINT8 Iface, void *Data, UINT16 Length);
void PollKbdGetReport(void);
int HubCtrl(UINT8 BmReq, UINT8 Req, UINT16 Value, UINT16 Index,
            UINT16 Len, void *Data);
int FinishHubSetup(UINT8 *OutNumPorts);
int HubGetPortStatus(UINT8 Port, UINT32 *OutSt);
int HubSetPortFeat(UINT8 Port, UINT16 Feat);
int HubClearPortFeat(UINT8 Port, UINT16 Feat);
UINT8 HubPortSpeed(UINT32 St);
int TryConfigureKeyboardSlot(UINT8 Speed);
int EnumHubChildrenForKeyboard(void);
int EnumHubChildrenForMouse(void);
int InitMouseOnPort(UINT32 Port1);
int InitMouseOnKeyboardSlot(void);
int InitMouseClaimPort(UINT32 Port1, UINT8 *SpeedOut);
int InitMouseFinishEp(UINT32 Port1, UINT8 Speed);
void InitMouseDeferred(void);
void MousePush(void);
int MousePresent(void);
void MouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
int DequeueMouse(USB_MOUSE_REPORT *Report);
void KbdPush(void);
int DequeueKeyboard(USB_KEYBOARD_REPORT *Report);
int KeyboardSetLeds(UINT8 Leds);
int HidKeyboardReady(void);
UINT8 KeyboardInterface(void);

int Init(UINT64 BaseAddress);
int EnableIrq(USB_CONTROLLER *Device);

#endif
