/*
 * Bluetooth/Private.h — 板载 USB HCI 内部（btx-1/2；勿出 XHCI.h）
 *
 * 本机：8087:0A2B @口08，冷启 CCS 无 PED → 一次 Port Reset。
 * btx-2：SetConfig + 事件 Intr + ACL Bulk；HCI_Reset 走 Class Control。
 */
#ifndef XHCI_BLUETOOTH_PRIVATE_H
#define XHCI_BLUETOOTH_PRIVATE_H

#include "Compiler.h"
#include "XHCI/Private.h"

#define BLUETOOTH_CONFIG_MAX 512
#define BLUETOOTH_EVENT_MAX 64
#define USB_CLASS_WIRELESS 0xE0u
#define USB_BLUETOOTH_SUBCLASS 0x01u
#define USB_BLUETOOTH_PROTOCOL_HCI 0x01u
#define HCI_OPCODE_RESET 0x0C03u
#define HCI_EVENT_COMMAND_COMPLETE 0x0Eu

extern UINT8 gBluetoothDeviceContext[2048];
extern UINT8 gBluetoothConfigBuffer[BLUETOOTH_CONFIG_MAX];
extern UINT32 gBluetoothSlot;
extern int gBluetoothUsbSeen;
extern UINT16 gBluetoothVendorId;
extern UINT16 gBluetoothProductId;
extern UINT32 gBluetoothRootPort;
extern UINT8 gBluetoothSpeed;
extern UINT8 gBluetoothInterface;
extern UINT32 gBluetoothEventDci;
extern UINT32 gBluetoothAclInDci;
extern UINT32 gBluetoothAclOutDci;
extern UINT16 gBluetoothEventMps;
extern UINT16 gBluetoothAclInMps;
extern UINT16 gBluetoothAclOutMps;
extern int gBluetoothConfigured;
extern int gBluetoothHciResetOk;
extern volatile UINT32 gBluetoothEventDone;
extern volatile UINT32 gBluetoothEventCode;

extern XHCI_TRB gBluetoothEp0Ring[RING_SIZE];
extern RING_STATE gBluetoothEp0;
extern XHCI_TRB gBluetoothEventRing[RING_SIZE];
extern RING_STATE gBluetoothEvent;
extern XHCI_TRB gBluetoothAclInRing[RING_SIZE];
extern RING_STATE gBluetoothAclIn;
extern XHCI_TRB gBluetoothAclOutRing[RING_SIZE];
extern RING_STATE gBluetoothAclOut;
extern UINT8 gBluetoothEventBuffer[BLUETOOTH_EVENT_MAX];

void BluetoothEp0Ring(XHCI_TRB **RingOut, RING_STATE **StateOut);
void BluetoothRequeueHid(void);
void BluetoothReleaseSlot(void);
int BluetoothAddressRoot(UINT32 Port, UINT8 *SpeedOut);
int BluetoothFinishProbe(UINT32 RootPort, UINT8 Speed);
int BluetoothConfigure(UINT32 RootPort, UINT8 Speed);
int BluetoothHciReset(void);
int BluetoothMatchTransfer(UINT32 EventSlot, UINT32 Endpoint, UINT64 TransferRing,
                           UINT32 Code, UINT32 Remain);

#endif
