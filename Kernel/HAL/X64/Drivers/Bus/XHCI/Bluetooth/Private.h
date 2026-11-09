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
#define HCI_OPCODE_SET_EVENT_MASK 0x0C01u
#define HCI_OPCODE_WRITE_LE_HOST_SUPPORTED 0x0C6Du
#define HCI_OPCODE_READ_BUFFER_SIZE 0x1005u
#define HCI_OPCODE_READ_BD_ADDR 0x1009u
#define HCI_OPCODE_DISCONNECT 0x0406u
#define HCI_OPCODE_LE_SET_EVENT_MASK 0x2001u
#define HCI_OPCODE_LE_CREATE_CONNECTION 0x200Du
#define HCI_OPCODE_LE_CREATE_CONNECTION_CANCEL 0x200Eu
#define HCI_OPCODE_LE_SET_SCAN_PARAMETERS 0x200Bu
#define HCI_OPCODE_LE_SET_SCAN_ENABLE 0x200Cu
#define HCI_EVENT_DISCONNECTION_COMPLETE 0x05u
#define HCI_EVENT_INQUIRY_COMPLETE 0x01u
#define HCI_EVENT_INQUIRY_RESULT 0x02u
#define HCI_EVENT_REMOTE_NAME_COMPLETE 0x07u
#define HCI_EVENT_COMMAND_COMPLETE 0x0Eu
#define HCI_EVENT_COMMAND_STATUS 0x0Fu
#define HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS 0x13u
#define HCI_EVENT_INQUIRY_RESULT_RSSI 0x22u
#define HCI_EVENT_EXTENDED_INQUIRY_RESULT 0x2Fu
#define HCI_EVENT_LE_META 0x3Eu
#define HCI_EIR_NAME_SHORT 0x08u
#define HCI_EIR_NAME_COMPLETE 0x09u
/*
 * 常驻泵门闩：仅 HostPumpStart 写 BLUETOOTH_HOST_PUMP_ON；Stop/Configure 写 0。
 * 勿用「非 0 即真」——BSS 垃圾会让 ArmEvent 早退 → xfer-cc=0。
 */
#define BLUETOOTH_HOST_PUMP_ON 0x42544D31u /* 'BTM1' */

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
extern volatile UINT32 gBluetoothEventRemain;
extern volatile UINT32 gBluetoothAclInDone;
extern volatile UINT32 gBluetoothAclInCode;
extern volatile UINT32 gBluetoothAclInRemain;
extern volatile UINT32 gBluetoothAclOutDone;
extern volatile UINT32 gBluetoothAclOutCode;
extern UINT32 gBluetoothHostPumpActive;
extern UINT32 gBluetoothAclPacketsCompleted;

static inline int BluetoothHostPumpIsActive(void) {
    return gBluetoothHostPumpActive == BLUETOOTH_HOST_PUMP_ON ? 1 : 0;
}

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
void BluetoothHostArmEventEndpoint(void);
void BluetoothHostArmAclIn(void);
void BluetoothHostPumpConsumeEvent(void);
void BluetoothHostPumpConsumeAclIn(void);
void BluetoothHostPumpAclInReset(void);
int BluetoothHostEventTake(UINT8 *Out, UINT32 Capacity, UINT32 *OutLength);
void BluetoothHostEventQueueFlush(void);
int BluetoothHostPumpWaitTakeEvent(void);
void BluetoothHostPump(void);
int BluetoothHostPumpStart(void);
void BluetoothHostPumpStop(void);
int BluetoothLeHostSupportApply(void);
int BluetoothLeScanEntryGet(UINTN OneBasedIndex, UINT8 Address[6],
                            UINT8 *AddressTypeOut);
int BluetoothLeConnectionHandleGet(UINT16 *Out);
int BluetoothLePeerAddressGet(UINT8 Out[6], UINT8 *AddressTypeOut);
int BluetoothLeIsConnected(void);
void BluetoothLeConnectionStateClear(void);
void BluetoothLeConnectionStateSet(UINT16 Handle, const UINT8 Address[6],
                                   UINT8 AddressType);
void BluetoothLeAbortPending(void);
void BluetoothLeLastErrorSet(UINT16 Opcode, UINT8 Status);
void BluetoothLeLastErrorGet(UINT16 *OpcodeOut, UINT8 *StatusOut);
void BluetoothAclInboundFlush(void);
void BluetoothAclInboundPush(const UINT8 *Payload, UINT16 Length);
int BluetoothAclInboundTake(UINT8 *Out, UINT16 Capacity, UINT16 *OutLength);
void BluetoothAclFlowReset(void);
int BluetoothAclFlowInitialize(void);
void BluetoothAclCreditReturn(UINT16 CompletedPackets);
int BluetoothAclCreditQuery(UINT16 *PacketLengthOut, UINT16 *PacketsFreeOut);
int BluetoothAclOutSend(UINT16 ConnectionHandle, const UINT8 *Payload,
                        UINT16 PayloadLength);
int BluetoothAclOutTest(void);
int BluetoothSmpAesE(const UINT8 Key[16], const UINT8 Plain[16], UINT8 Out[16]);
int BluetoothSmpC1(const UINT8 Key[16], const UINT8 Random[16],
                   const UINT8 PairingRequest[7],
                   const UINT8 PairingResponse[7], UINT8 InitiatorAddressType,
                   const UINT8 InitiatorAddress[6], UINT8 ResponderAddressType,
                   const UINT8 ResponderAddress[6], UINT8 Out[16]);
int BluetoothSmpS1(const UINT8 Key[16], const UINT8 Random1[16],
                   const UINT8 Random2[16], UINT8 Out[16]);
void BluetoothSmpFillRandom(UINT8 Out[16]);
int BluetoothSmpStartEncryption(UINT16 Handle, const UINT8 ShortTermKey[16]);
int BluetoothSmpWaitEncryptionChange(UINT16 Handle, UINT32 TimeoutMs);
int XhciBluetoothSmpJustWorks(UINT32 TimeoutMs);
UINT16 BluetoothAttMaximumTransmissionUnitGet(void);
void BluetoothAttMaximumTransmissionUnitReset(void);
int BluetoothAttSend(UINT16 Handle, const UINT8 *AttributeProtocolDataUnit,
                     UINT16 Length);
int BluetoothAttTake(UINT8 *AttributeProtocolDataUnitOut, UINT16 Capacity,
                     UINT16 *LengthOut, UINT32 TimeoutMs);
int BluetoothAttExpect(UINT8 WantOpcode, UINT8 *AttributeProtocolDataUnitOut,
                       UINT16 Capacity, UINT16 *LengthOut, UINT32 TimeoutMs,
                       UINT16 Stage);
int BluetoothAttExchangeMaximumTransmissionUnit(UINT16 Handle,
                                                UINT16 ClientReceiveMaximum,
                                                UINT32 TimeoutMs);
int BluetoothAttFindByTypeValue(UINT16 Handle, UINT16 StartHandle,
                                UINT16 EndHandle, UINT16 TypeUuid,
                                const UINT8 *Value, UINT16 ValueLength,
                                UINT8 *ResponseOut, UINT16 ResponseCapacity,
                                UINT16 *ResponseLengthOut, UINT32 TimeoutMs,
                                UINT16 Stage);
int BluetoothAttReadByType(UINT16 Handle, UINT16 StartHandle, UINT16 EndHandle,
                           UINT16 TypeUuid, UINT8 *ResponseOut,
                           UINT16 ResponseCapacity, UINT16 *ResponseLengthOut,
                           UINT32 TimeoutMs, UINT16 Stage);
int BluetoothAttFindInformation(UINT16 Handle, UINT16 StartHandle,
                                UINT16 EndHandle, UINT8 *ResponseOut,
                                UINT16 ResponseCapacity,
                                UINT16 *ResponseLengthOut, UINT32 TimeoutMs,
                                UINT16 Stage);
int BluetoothAttWriteRequest(UINT16 Handle, UINT16 AttributeHandle,
                             const UINT8 *Value, UINT16 ValueLength,
                             UINT32 TimeoutMs, UINT16 Stage);
int BluetoothAttWriteCommand(UINT16 Handle, UINT16 AttributeHandle,
                             const UINT8 *Value, UINT16 ValueLength);
int BluetoothAttReadRequest(UINT16 ConnectionHandle, UINT16 AttributeHandle,
                            UINT8 *ValueOut, UINT16 ValueCapacity,
                            UINT16 *ValueLengthOut, UINT32 TimeoutMs,
                            UINT16 Stage);
int BluetoothAttReadBlobRequest(UINT16 ConnectionHandle, UINT16 AttributeHandle,
                                UINT16 ValueOffset, UINT8 *ValueOut,
                                UINT16 ValueCapacity, UINT16 *ValueLengthOut,
                                UINT32 TimeoutMs, UINT16 Stage);
int BluetoothAttReadLong(UINT16 ConnectionHandle, UINT16 AttributeHandle,
                         UINT8 *ValueOut, UINT16 ValueCapacity,
                         UINT16 *ValueLengthOut, UINT32 TimeoutMs,
                         UINT16 Stage);
void BluetoothAttNotifyQueueFlush(void);
void BluetoothAttNotifyAcceptSet(int Accept);
int BluetoothAttNotifyAcceptGet(void);
UINT32 BluetoothAttNotifyDroppedGet(void);
int BluetoothAttNotifyQueueIsAttributeNotification(const UINT8 *AclPayload,
                                                   UINT16 Length);
void BluetoothAttNotifyQueuePushAcl(const UINT8 *AclPayload, UINT16 Length);
int BluetoothAttNotifyQueueTake(UINT8 *AttributeProtocolDataUnitOut,
                                UINT16 Capacity, UINT16 *LengthOut);
void BluetoothAttHidDiscoveryClear(void);
int BluetoothAttHidDiscoveryGet(UINT16 *ServiceStartOut, UINT16 *ServiceEndOut,
                                UINT16 *ReportMapHandleOut,
                                UINT16 *ReportCountOut,
                                const UINT16 **ReportValueHandlesOut);
int BluetoothAttHidDiscoveryGetClientConfiguration(
    UINT16 *ServiceStartOut, UINT16 *ServiceEndOut, UINT16 *ReportCountOut,
    const UINT16 **ReportValueHandlesOut,
    const UINT16 **ClientConfigurationHandlesOut);
int BluetoothAttHidDiscoveryGetProtocolMode(UINT16 *ProtocolModeHandleOut);
int BluetoothAttHidFindService(UINT16 ConnectionHandle, UINT32 TimeoutMs,
                               UINT16 *ServiceStartOut, UINT16 *ServiceEndOut);
int BluetoothAttHidFindReports(UINT16 ConnectionHandle, UINT16 ServiceStart,
                               UINT16 ServiceEnd, UINT32 TimeoutMs,
                               UINT16 *ReportMapValueHandleOut,
                               UINT16 *ProtocolModeValueHandleOut,
                               UINT16 *ReportCountOut,
                               UINT16 *ReportValueHandles,
                               UINT16 *ReportClientConfigurationHandles,
                               UINT16 ReportCapacity);
int BluetoothAttHidReportMapGet(const UINT8 **DataOut, UINT16 *LengthOut);
void BluetoothAttHidRecoverWiredHid(void);
void BluetoothAttHidDisarmClientConfiguration(UINT16 ConnectionHandle,
                                              const UINT16 *ConfigurationHandles,
                                              UINT16 Count);
void BluetoothHidInputNoteArmed(UINT16 ReportValueHandle, UINT8 ReportId);
int BluetoothHidInputAttach(void);
void BluetoothHidInputHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
int XhciBluetoothAttHidDiscover(UINT32 TimeoutMs);
int XhciBluetoothAttHidSubscribe(UINT32 TimeoutMs);
int XhciBluetoothMouseConnect(UINTN OneBasedIndex, UINT32 TimeoutMs);
void BluetoothLeLinkEventDemux(const UINT8 *Event, UINT32 Length);
int BluetoothLeLinkL2capSignalPush(const UINT8 *AclPayload, UINT16 Length);
void BluetoothLeLinkMaintainService(void);

#endif
