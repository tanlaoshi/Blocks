/*
 * Bluetooth/Private.h — 板载 USB HCI 内部（btx-1…5；勿出 XHCI.h）
 *
 * 本机：8087:0A2B @口08。命令 Class Control；事件 Interrupt IN。
 */
#ifndef XHCI_BLUETOOTH_PRIVATE_H
#define XHCI_BLUETOOTH_PRIVATE_H

#include "Compiler.h"
#include "XHCI/Private.h"

#define BLUETOOTH_CONFIG_MAX 512
/* Intr 事件缓冲：对表 MPS（常见 16/64）；过大 TD 易 babble，Mode2 EIR 暂不用 */
#define BLUETOOTH_EVENT_MAX 64
#define BLUETOOTH_COMMAND_MAX 256
#define BLUETOOTH_NAME_MAX 248
#define USB_CLASS_WIRELESS 0xE0u
#define USB_BLUETOOTH_SUBCLASS 0x01u
#define USB_BLUETOOTH_PROTOCOL_HCI 0x01u
#define HCI_OPCODE_INQUIRY 0x0401u
#define HCI_OPCODE_INQUIRY_CANCEL 0x0402u
#define HCI_OPCODE_REMOTE_NAME_REQUEST 0x0419u
#define HCI_OPCODE_REMOTE_NAME_CANCEL 0x041Au
#define HCI_OPCODE_RESET 0x0C03u
#define HCI_OPCODE_WRITE_LOCAL_NAME 0x0C13u
/* Intel 厂商：冷启常在 bootloader，标准 Reset 会 CC status=0x01；须灌 .sfi */
#define HCI_OPCODE_INTEL_RESET 0xFC01u
#define HCI_OPCODE_INTEL_VERSION 0xFC05u
#define HCI_OPCODE_INTEL_SECURE_SEND 0xFC09u
#define HCI_OPCODE_INTEL_BOOT_PARAMETERS 0xFC0Du
#define HCI_OPCODE_WRITE_SCAN_ENABLE 0x0C1Au
#define HCI_OPCODE_WRITE_PAGE_TIMEOUT 0x0C18u
#define HCI_OPCODE_WRITE_CLASS_OF_DEVICE 0x0C24u
#define HCI_OPCODE_WRITE_INQUIRY_MODE 0x0C45u
#define HCI_OPCODE_WRITE_EXTENDED_INQUIRY_RESPONSE 0x0C52u
#define HCI_OPCODE_READ_BD_ADDR 0x1009u
#define HCI_EVENT_INQUIRY_COMPLETE 0x01u
#define HCI_EVENT_INQUIRY_RESULT 0x02u
#define HCI_EVENT_REMOTE_NAME_COMPLETE 0x07u
#define HCI_EVENT_COMMAND_COMPLETE 0x0Eu
#define HCI_EVENT_COMMAND_STATUS 0x0Fu
#define HCI_EVENT_INQUIRY_RESULT_RSSI 0x22u
#define HCI_EVENT_EXTENDED_INQUIRY_RESULT 0x2Fu
#define HCI_EIR_NAME_SHORT 0x08u
#define HCI_EIR_NAME_COMPLETE 0x09u

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
extern UINT16 gBluetoothEventMaxPacketSize;
extern UINT16 gBluetoothAclInMaxPacketSize;
extern UINT16 gBluetoothAclOutMaxPacketSize;
extern int gBluetoothConfigured;
extern int gBluetoothHciResetOk;
extern int gBluetoothReady;
extern int gBluetoothDiscoverable;
extern UINT8 gBluetoothAddress[6];
extern char gBluetoothLocalName[64];
extern volatile UINT32 gBluetoothEventDone;
extern volatile UINT32 gBluetoothEventCode;
extern volatile UINT32 gBluetoothAclInDone;
extern volatile UINT32 gBluetoothAclInCode;
extern volatile UINT32 gBluetoothAclOutDone;
extern volatile UINT32 gBluetoothAclOutCode;

extern XHCI_TRB gBluetoothEndpoint0Ring[RING_SIZE];
extern RING_STATE gBluetoothEndpoint0;
extern XHCI_TRB gBluetoothEventRing[RING_SIZE];
extern RING_STATE gBluetoothEvent;
extern XHCI_TRB gBluetoothAclInRing[RING_SIZE];
extern RING_STATE gBluetoothAclIn;
extern XHCI_TRB gBluetoothAclOutRing[RING_SIZE];
extern RING_STATE gBluetoothAclOut;
extern UINT8 gBluetoothEventBuffer[BLUETOOTH_EVENT_MAX];

void BluetoothEndpoint0Ring(XHCI_TRB **RingOut, RING_STATE **StateOut);
void BluetoothRequeueHid(void);
void BluetoothReleaseSlot(void);
int BluetoothAddressRoot(UINT32 Port, UINT8 *SpeedOut);
int BluetoothFinishProbe(UINT32 RootPort, UINT8 Speed);
int BluetoothConfigure(UINT32 RootPort, UINT8 Speed);
int BluetoothHciReset(void);
void BluetoothHciArmEvent(void);
int BluetoothHciWaitArmed(UINT32 TimeoutMs);
void BluetoothHciArmAclIn(void);
int BluetoothHciWaitAclIn(UINT32 TimeoutMs);
int BluetoothHciSendRaw(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen);
/* bootloader 下 0xFC09：Bulk OUT 发、Bulk IN 收事件（对表 btusb） */
int BluetoothHciSendBulk(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen);
int BluetoothHciCommand(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen,
                        UINT8 *StatusOut, UINT8 *ReturnData, UINT8 ReturnMax,
                        UINT8 *ReturnLenOut);
int BluetoothHciCommandTimeout(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen,
                               UINT8 *StatusOut, UINT8 *ReturnData,
                               UINT8 ReturnMax, UINT8 *ReturnLenOut,
                               UINT32 TimeoutMilliseconds);
int BluetoothIntelSecureSend(UINT8 FragmentType, const UINT8 *Data,
                             UINT32 Length);
int BluetoothIntelSecureSendRsaHeader(const UINT8 *Firmware,
                                      UINTN FirmwareBytes);
int BluetoothIntelSecureSendPayload(const UINT8 *Firmware,
                                    UINTN FirmwareBytes);
int BluetoothIntelBuildFirmwarePath(char *Out, UINTN Cap, UINT8 HardwareVariant,
                                    UINT16 DeviceRevisionId);
int BluetoothIntelReadFirmwareBlob(const char *Path, UINT8 **OutBlob,
                                   UINTN *OutBytes, UINT32 *OutPages);
int BluetoothIntelFirmwareLoad(void);
int BluetoothBringUpIdentity(void);
int BluetoothSetDiscoverable(int On);
int BluetoothMatchTransfer(UINT32 EventSlot, UINT32 Endpoint, UINT64 TransferRing,
                           UINT32 Code, UINT32 Remain);

#endif
