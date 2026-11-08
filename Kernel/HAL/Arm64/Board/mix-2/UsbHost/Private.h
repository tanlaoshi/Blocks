/*
 * UsbHost/Private.h — MIX 2 DWC3/xHCI bringup 私有（PR-MIX2-USB-2）
 *
 * 对表：xHCI 1.1 能力/操作寄存器；DWC3 Host 窗 = 控制器基址 +0。
 */
#ifndef USB_HOST_PRIVATE_H
#define USB_HOST_PRIVATE_H

#include "Board.h"
#include "BoardConfig.h"

#define DWC3_REGISTER_BASE           0x0A800000ULL
#define DWC3_GCTL_OFFSET             0xC110u
#define DWC3_GUSB2PHYCFG_OFFSET      0xC200u
#define DWC3_GUSB3PIPECTL_OFFSET     0xC2C0u
#define DWC3_GCTL_PRTCAPDIR_MASK     (3u << 12)
#define DWC3_GCTL_PRTCAP_HOST        (1u << 12)
#define DWC3_GCTL_CORESOFTRESET      (1u << 11)
#define DWC3_PHY_SOFTWARE_RESET      (1u << 31)

#define USB_COMMAND_RUN_STOP         (1u << 0)
#define USB_COMMAND_HOST_CONTROLLER_RESET (1u << 1)
#define USB_STATUS_HOST_CONTROLLER_HALTED (1u << 0)
#define USB_STATUS_EVENT_INTERRUPT   (1u << 2)
#define USB_STATUS_CONTROLLER_NOT_READY (1u << 6)

#define PORT_STATUS_CONNECT          (1u << 0)
#define PORT_STATUS_ENABLE           (1u << 1)
#define PORT_STATUS_RESET            (1u << 4)
#define PORT_STATUS_POWER            (1u << 9)
#define PORT_STATUS_SPEED_SHIFT      10
#define PORT_STATUS_CONNECT_CHANGE   (1u << 17)
#define PORT_STATUS_ENABLE_CHANGE    (1u << 18)
#define PORT_STATUS_RESET_CHANGE     (1u << 21)
#define PORT_STATUS_READ_ONLY \
    (PORT_STATUS_CONNECT | (1u << 3) | (0xFu << PORT_STATUS_SPEED_SHIFT) | (1u << 30))
#define PORT_STATUS_READ_WRITE_SET \
    (PORT_STATUS_ENABLE | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | \
     PORT_STATUS_POWER | (1u << 14) | (1u << 15) | (1u << 16) | (0x1Fu << 24))
#define PORT_STATUS_CHANGE_BITS \
    (PORT_STATUS_CONNECT_CHANGE | PORT_STATUS_ENABLE_CHANGE | PORT_STATUS_RESET_CHANGE | \
     (1u << 19) | (1u << 20) | (1u << 22) | (1u << 23))

#define TRANSFER_REQUEST_BLOCK_CYCLE (1u << 0)
#define TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION (1u << 5)
#define TRANSFER_REQUEST_BLOCK_IMMEDIATE_DATA (1u << 6)
#define TRANSFER_REQUEST_BLOCK_TYPE_SHIFT 10
#define TRANSFER_REQUEST_BLOCK_TYPE(Type) ((UINT32)(Type) << TRANSFER_REQUEST_BLOCK_TYPE_SHIFT)
#define TRANSFER_REQUEST_BLOCK_SLOT_SHIFT 24
#define TRANSFER_REQUEST_BLOCK_NORMAL 1u
#define TRANSFER_REQUEST_BLOCK_SETUP 2u
#define TRANSFER_REQUEST_BLOCK_DATA 3u
#define TRANSFER_REQUEST_BLOCK_STATUS 4u
#define TRANSFER_REQUEST_BLOCK_LINK 6u
#define TRANSFER_REQUEST_BLOCK_ENABLE_SLOT 9u
#define TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE 11u
#define TRANSFER_REQUEST_BLOCK_TRANSFER_EVENT 32u
#define TRANSFER_REQUEST_BLOCK_COMMAND_COMPLETION 33u
#define TRANSFER_REQUEST_BLOCK_TRANSFER_TYPE_IN (3u << 16)
#define TRANSFER_REQUEST_BLOCK_DIRECTION_IN (1u << 16)
#define COMPLETION_CODE_SUCCESS 1u
#define COMPLETION_CODE_SHORT_PACKET 13u

#define USB_DEVICE_CLASS_HUB 0x09u
#define USB_HUB_ADDRESS 1u

#define COMMAND_RING_TRANSFER_REQUEST_BLOCK_COUNT 32u
#define EVENT_RING_TRANSFER_REQUEST_BLOCK_COUNT 32u
#define TRANSFER_RING_TRANSFER_REQUEST_BLOCK_COUNT 16u
#define DEVICE_SLOT_COUNT_MAX 8u
#define SCRATCHPAD_BUFFER_COUNT_MAX 16u

typedef struct {
    UINT64 Parameter;
    UINT32 Status;
    UINT32 Control;
} __attribute__((packed, aligned(16))) TransferRequestBlock;

typedef struct {
    UINT32 EnqueueIndex;
    UINT32 ProducerCycleState;
    UINT32 BlockCount;
} RingState;

void UsbHostFormatHexadecimal32(char *Out, UINT32 Value);
void UsbHostCopyText(char *Destination, const char *Source, UINTN Capacity);
void UsbHostAppendText(char *Destination, const char *Source, UINTN Capacity);
void UsbHostStallYield(UINTN Iterations);
void UsbHostCacheCleanRange(void *Address, UINTN Size);
void UsbHostCacheCleanInvalidateRange(void *Address, UINTN Size);

volatile UINT32 *UsbHostMmio32(UINT64 Address);
UINT32 UsbHostReadMmio32(UINT64 Address);
void UsbHostWriteMmio32(UINT64 Address, UINT32 Value);
UINT64 UsbHostReadMmio64(UINT64 Address);
void UsbHostWriteMmio64(UINT64 Address, UINT64 Value);

int UsbHostXhciTransferInitialize(UINT64 CapabilityBase, UINT64 OperationalBase,
                                  UINT64 DoorbellBase, UINT64 RuntimeBase,
                                  UINT32 ContextSizeBytes, UINT32 MaxSlots,
                                  UINT32 ScratchpadCount);
int UsbHostXhciSubmitCommand(TransferRequestBlock *Command, UINT32 *CompletionCode,
                             UINT32 *SlotIdOut);
void UsbHostEndpointZeroEnqueue(const TransferRequestBlock *Block);
int UsbHostWaitTransferEvent(UINT32 *CompletionCode);
void UsbHostRingEndpointZeroDoorbell(UINT32 SlotId);
UINT8 *UsbHostSetupPacketBuffer(void);
UINT8 *UsbHostControlDataBuffer(void);
int UsbHostXhciControlTransfer(UINT32 SlotId, UINT8 RequestType, UINT8 Request,
                               UINT16 Value, UINT16 Index, void *Data, UINT16 Length);
UINT8 *UsbHostDeviceContextSlot(UINT32 SlotId);
UINT8 *UsbHostInputContextBuffer(void);
UINT32 UsbHostContextSizeBytes(void);
void UsbHostBindEndpointZeroRing(UINT32 SlotId);
void UsbHostSetDeviceContextBase(UINT32 SlotId, UINT64 Physical);
UINT64 UsbHostEndpointZeroRingPhysical(void);

/* 线刷 DT qusb@c012000 + phy-msm-qusb-v2 power-on（clamp_dig_n=1） */
int UsbHostQusbPhyInitialize(void);
/* CC attach 后重踢：解钳 + init-seq + 清 GUSB2 suspend */
int UsbHostQusbPhyAttachKick(void);
/* 厂商非 v2：PORT_UTMI_STATUS@0xF4；v2 仅对照 */
UINT8 UsbHostQusbLineStateRead(void);
UINT8 UsbHostQusbClampByte(void);

/* ssusb reg[1] QSCRATCH@0xC016000：HS UTMI-as-PIPE + Host VBUS override */
int UsbHostQscratchConfigureForHost(void);
UINT32 UsbHostQscratchGeneralConfiguration(void);

/* SPMI PMIC arbiter v2 + PMI8998 Type-C DFP */
int UsbHostSpmiArbiterProbe(void);
UINT32 UsbHostSpmiArbiterVersion(void);
int UsbHostSpmiReadByte(UINT8 SlaveIdentifier, UINT16 Address, UINT8 *ValueOut);
int UsbHostSpmiWriteByte(UINT8 SlaveIdentifier, UINT16 Address, UINT8 Value);
int UsbHostTypeCForceHost(void);
void UsbHostTypeCMaintainAfterAttach(void);
int UsbHostTypeCAnalogSwitchForceUsb(void);
UINT8 UsbHostTypeCAnalogSwitchByte(void);
int UsbHostTypeCLastResult(void);
UINT8 UsbHostTypeCControlByte(void);
UINT8 UsbHostTypeCStatus1Byte(void);
UINT8 UsbHostTypeCStatus2Byte(void);
UINT8 UsbHostTypeCStatus4Byte(void);
UINT8 UsbHostOtgConfigurationByte(void);
UINT8 UsbHostOtgStatusByte(void);
UINT8 UsbHostCommandOtgByte(void);
UINT8 UsbHostUsbinCommandByte(void);
UINT8 UsbHostOtgEngineeringByte(void);
UINT8 UsbHostOtgInterruptStatusByte(void);
int UsbHostOtgVbusEnable(void);
void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable);

int UsbHostXhciStart(void);
UINT32 UsbHostXhciMaxPorts(void);
UINT64 UsbHostXhciPortStatusAddress(UINT32 PortIndexOneBased);

int UsbHostRootPortPower(UINT32 PortIndexOneBased);
int UsbHostRootPortReset(UINT32 PortIndexOneBased);
UINT32 UsbHostRootPortStatus(UINT32 PortIndexOneBased);
int UsbHostEnumerateHubOnPort(UINT32 PortIndexOneBased, UINT32 *HubAddressOut);
/* 枚举粘性码：0=idle；A1=hub ok；10xx=rst|PORTSC；20=slot 30=addr 40=desc 5x=class */
UINT32 UsbHostEnumerateStatusCode(void);
void UsbHostEnumerateStatusCodeClear(void);

#endif /* USB_HOST_PRIVATE_H */
