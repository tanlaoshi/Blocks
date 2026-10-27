/*
 * Bluetooth/Private.h — btx-1 探针内部（勿出 XHCI.h）
 *
 * 板载 HCI 常见根口 CCS 无 PED（本机口8 portsc=0x6E1）。
 * 软 Address 不够；须一次 Port Reset（ForceUntilPed），非 HotPoll/NeedForce。
 */
#ifndef XHCI_BLUETOOTH_PRIVATE_H
#define XHCI_BLUETOOTH_PRIVATE_H

#include "Compiler.h"
#include "XHCI/Private.h"

#define BLUETOOTH_CONFIG_MAX 512
#define USB_CLASS_WIRELESS 0xE0u
#define USB_BLUETOOTH_SUBCLASS 0x01u
#define USB_BLUETOOTH_PROTOCOL_HCI 0x01u

extern UINT8 gBluetoothDeviceContext[2048];
extern UINT8 gBluetoothConfigBuffer[BLUETOOTH_CONFIG_MAX];
extern UINT32 gBluetoothSlot;
extern int gBluetoothUsbSeen;
extern UINT16 gBluetoothVendorId;
extern UINT16 gBluetoothProductId;
extern UINT32 gBluetoothRootPort;

void BluetoothRequeueHid(void);
void BluetoothReleaseSlot(void);
int BluetoothAddressRoot(UINT32 Port, UINT8 *SpeedOut);
int BluetoothFinishProbe(UINT32 RootPort);

#endif
