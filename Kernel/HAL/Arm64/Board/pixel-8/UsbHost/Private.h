/* UsbHost/Private.h — Pixel 8 DWC3/xHCI bringup（对表 xHCI 1.1） */
#ifndef USB_HOST_PRIVATE_H
#define USB_HOST_PRIVATE_H

#include "Board.h"
#include "BoardConfig.h"
#include "HalSerial.h"

#define DWC3_REGISTER_BASE           BOARD_USB_DWC3_PHYSICAL_BASE
#define DWC3_GCTL_OFFSET             0xC110u
#define DWC3_GLOBAL_USER_CONTROL_OFFSET 0xC12Cu
#define DWC3_GUSB2PHYCFG_OFFSET      0xC200u
#define DWC3_GUSB3PIPECTL_OFFSET     0xC2C0u
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_OFFSET 0xC630u
#define DWC3_GCTL_PRTCAPDIR_MASK     (3u << 12)
#define DWC3_GCTL_PRTCAP_HOST        (1u << 12)
#define DWC3_GCTL_CORESOFTRESET      (1u << 11)
/*
 * GCTL.SOFITPSYNC：仅 freeclk quirk 时常置；zuma `adj-sof-accuracy=1`
 * 在 dwc3_exynos_core_init 里会清掉（覆盖 quirk）。须保持清。
 */
#define DWC3_GCTL_SOFTWARE_ITP_SYNCHRONIZE (1u << 10)
#define DWC3_PHY_SOFTWARE_RESET      (1u << 31)
/* GUCTL：对表 dwc3-exynos + ATF usb_dwc3_regs.h；19.2MHz → REFCLKPER=0x34 */
#define DWC3_GLOBAL_USER_CONTROL_HOST_IN_AUTO_RETRY (1u << 14)
#define DWC3_GLOBAL_USER_CONTROL_SPARSE_CONTROL_TRANSFER_ENABLE (1u << 17)
#define DWC3_GLOBAL_USER_CONTROL_NO_EXTRA_DELAY (1u << 21)
#define DWC3_GLOBAL_USER_CONTROL_REFERENCE_CLOCK_PERIOD_SHIFT 22
#define DWC3_GLOBAL_USER_CONTROL_REFERENCE_CLOCK_PERIOD_MASK \
    (0x3FFu << DWC3_GLOBAL_USER_CONTROL_REFERENCE_CLOCK_PERIOD_SHIFT)
#define DWC3_GLOBAL_USER_CONTROL_REFERENCE_CLOCK_PERIOD_19200KHZ 0x34u
/* 对表 snps,quirk-frame-length-adjustment=<0x20> + 19.2MHz ref */
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_30MHZ_MASK 0x3Fu
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_30MHZ_VALUE 0x20u
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_30MHZ_SIDEBAND_SELECT (1u << 7)
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_REFERENCE_CLOCK_MASK \
    (0x3FFFu << 8)
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_REFERENCE_CLOCK_LOW_POWER_SELECT \
    (1u << 23)
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_240MHZ_DECREMENT_MASK \
    (0x7Fu << 24)
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_240MHZ_DECREMENT_VALUE \
    (0x0Cu << 24)
#define DWC3_GLOBAL_FRAME_LENGTH_ADJUSTMENT_240MHZ_DECREMENT_PLUS_ONE \
    (1u << 31)

#define USB_COMMAND_RUN_STOP         (1u << 0)
#define USB_COMMAND_HOST_CONTROLLER_RESET (1u << 1)
#define USB_STATUS_HOST_CONTROLLER_HALTED (1u << 0)
#define USB_STATUS_EVENT_INTERRUPT   (1u << 2)
#define USB_STATUS_CONTROLLER_NOT_READY (1u << 6)

#define PORT_STATUS_CONNECT          (1u << 0)
#define PORT_STATUS_ENABLE           (1u << 1)
#define PORT_STATUS_RESET            (1u << 4)
#define PORT_STATUS_LINK_STATE_SHIFT 5
#define PORT_STATUS_LINK_STATE_MASK  (0xFu << PORT_STATUS_LINK_STATE_SHIFT)
#define PORT_STATUS_POWER            (1u << 9)
#define PORT_STATUS_SPEED_SHIFT      10
#define PORT_STATUS_CONNECT_CHANGE   (1u << 17)
#define PORT_STATUS_ENABLE_CHANGE    (1u << 18)
#define PORT_STATUS_RESET_CHANGE     (1u << 21)

#define DWC3_GUSB2PHYCFG_ULPI_UTMI           (1u << 4)
#define DWC3_GUSB2PHYCFG_SUSPEND_USB20       (1u << 6)
#define DWC3_GUSB2PHYCFG_ENABLE_SLEEP_MODE   (1u << 8)
#define DWC3_GUSB2PHYCFG_PHYSICAL_INTERFACE_SHIFT 3
#define DWC3_GUSB2PHYCFG_USB_TURNAROUND_SHIFT 10
#define DWC3_GUSB2PHYCFG_USB_TURNAROUND_MASK (0xFu << DWC3_GUSB2PHYCFG_USB_TURNAROUND_SHIFT)
#define DWC3_GUSB2PHYCFG_UTMI_TURNAROUND_8BIT 9u
#define DWC3_GUSB2PHYCFG_U2_FREE_CLOCK_EXISTS (1u << 30)

#define PORT_STATUS_READ_ONLY \
    (PORT_STATUS_CONNECT | (1u << 3) | (0xFu << PORT_STATUS_SPEED_SHIFT) | (1u << 30))
#define PORT_STATUS_READ_WRITE_SET \
    (PORT_STATUS_LINK_STATE_MASK | PORT_STATUS_POWER | (0x3u << 14) | (0x7u << 25))
#define PORT_STATUS_CHANGE_BITS \
    (PORT_STATUS_CONNECT_CHANGE | PORT_STATUS_ENABLE_CHANGE | PORT_STATUS_RESET_CHANGE | \
     (1u << 19) | (1u << 20) | (1u << 22) | (1u << 23))

#define TRANSFER_REQUEST_BLOCK_CYCLE (1u << 0)
#define TRANSFER_REQUEST_BLOCK_CHAIN (1u << 4) /* Control TD：Setup/Data 须 CH=1 */
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
#define TRANSFER_REQUEST_BLOCK_DISABLE_SLOT 10u
#define TRANSFER_REQUEST_BLOCK_ADDRESS_DEVICE 11u
#define TRANSFER_REQUEST_BLOCK_CONFIGURE_ENDPOINT 12u
#define TRANSFER_REQUEST_BLOCK_EVALUATE_CONTEXT 13u
#define TRANSFER_REQUEST_BLOCK_RESET_ENDPOINT 14u
#define TRANSFER_REQUEST_BLOCK_STOP_ENDPOINT 15u
#define TRANSFER_REQUEST_BLOCK_SET_TR_DEQUEUE 16u
/* Address Device TRB bit9：BSR=1 不发 SET_ADDRESS（§4.6.5 / §6.4.3.4） */
#define TRANSFER_REQUEST_BLOCK_BLOCK_SET_ADDRESS_REQUEST (1u << 9)
#define TRANSFER_REQUEST_BLOCK_TRANSFER_EVENT 32u
#define TRANSFER_REQUEST_BLOCK_COMMAND_COMPLETION 33u
#define TRANSFER_REQUEST_BLOCK_TRANSFER_TYPE_IN (3u << 16)
#define TRANSFER_REQUEST_BLOCK_DIRECTION_IN (1u << 16)
#define TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_SHORT_PACKET (1u << 2)
#define COMPLETION_CODE_SUCCESS 1u
#define COMPLETION_CODE_SHORT_PACKET 13u

#define USB_DEVICE_CLASS_HUB 0x09u
#define USB_DEVICE_CLASS_HID 0x03u
#define USB_HID_BOOT_SUBCLASS 1u
#define USB_INTERFACE_PROTOCOL_BOOT_KEYBOARD 1u
#define USB_HUB_ADDRESS 1u
#define USB_CONTROL_DATA_CAPACITY 256u

/* USB2 hub wPortStatus / features（对表 X64 Private.h） */
#define HUB_PORT_CONNECTION (1u << 0)
#define HUB_PORT_ENABLE (1u << 1)
#define HUB_PORT_RESET (1u << 4)
#define HUB_PORT_POWER (1u << 8)
#define HUB_PORT_LOW_SPEED (1u << 9)
#define HUB_PORT_HIGH_SPEED (1u << 10)
#define HUB_FEATURE_PORT_RESET 4u
#define HUB_FEATURE_PORT_POWER 8u
#define HUB_FEATURE_CONNECTION_CHANGE 16u
#define HUB_FEATURE_RESET_CHANGE 20u
#define TRANSFER_REQUEST_BLOCK_ENDPOINT_SHIFT 16

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
void UsbHostStallMilliseconds(UINT32 Milliseconds);
void UsbHostCacheCleanRange(void *Address, UINTN Size);
void UsbHostCacheCleanInvalidateRange(void *Address, UINTN Size);

volatile UINT32 *UsbHostMmio32(UINT64 Address);
UINT32 UsbHostReadMmio32(UINT64 Address);
void UsbHostWriteMmio32(UINT64 Address, UINT32 Value);
UINT64 UsbHostReadMmio64(UINT64 Address);
void UsbHostWriteMmio64(UINT64 Address, UINT64 Value);

int UsbHostXhciTransferInitialize(UINT64 CapabilityBase, UINT64 OperationalBase,
                                  UINT64 DoorbellBase, UINT64 RuntimeBase,
                                  UINT32 ContextSizeBytes, UINT32 SlotLimit,
                                  UINT32 ScratchpadCount);
int UsbHostXhciSubmitCommand(TransferRequestBlock *Command, UINT32 *CompletionCode,
                             UINT32 *SlotIdOut);
void UsbHostEndpointZeroEnqueue(UINT32 SlotId, const TransferRequestBlock *Block);
int UsbHostWaitTransferEvent(UINT32 *CompletionCode);
int UsbHostWaitTransferEventLimited(UINT32 *CompletionCode, UINTN GuardLimit,
                                    UINT32 EndpointIdOrZero);
void UsbHostRingEndpointZeroDoorbell(UINT32 SlotId);
void UsbHostRingDoorbell(UINT32 SlotId, UINT32 EndpointTarget);
UINT8 *UsbHostSetupPacketBuffer(void);
UINT8 *UsbHostControlDataBuffer(void);
int UsbHostXhciControlTransfer(UINT32 SlotId, UINT8 RequestType, UINT8 Request,
                               UINT16 Value, UINT16 Index, void *Data, UINT16 Length);
UINT32 UsbHostXhciControlLastCompletionCode(void);
UINT8 *UsbHostDeviceContextSlot(UINT32 SlotId);
UINT8 *UsbHostInputContextBuffer(void);
UINT32 UsbHostContextSizeBytes(void);
void UsbHostBindEndpointZeroRing(UINT32 SlotId);
void UsbHostSetDeviceContextBase(UINT32 SlotId, UINT64 Physical);
UINT64 UsbHostEndpointZeroRingPhysical(UINT32 SlotId);
int UsbHostXhciDisableSlot(UINT32 SlotId);
int UsbHostXhciConfigureEndpoint(UINT32 SlotId);
void UsbHostRecoverEndpointZero(UINT32 SlotId);
int UsbHostEndpointZeroSync(UINT32 SlotId, UINT32 *StopCompletionOut,
                            UINT32 *ResetCompletionOut,
                            UINT32 *DequeueCompletionOut);
void UsbHostInterruptRingInitialize(UINT32 SlotId, UINT32 DeviceContextIndex,
                                    UINT32 TransferLength);
UINT64 UsbHostInterruptRingPhysical(void);
UINT8 *UsbHostInterruptReportBuffer(void);
int UsbHostInterruptSubmitOnceDetailed(UINT32 *CompletionCodeOut);
void UsbHostDrainTransferEvents(void);

void UsbHostPrepareUsb2PhysicalInterface(void);
int UsbHostXhciStart(void);
UINT32 UsbHostXhciMaxPorts(void);
UINT64 UsbHostXhciPortStatusAddress(UINT32 PortIndexOneBased);
int UsbHostXhciProtocolPortsProbe(UINT64 CapabilityBase, UINT32 MaxPorts);
UINT32 UsbHostXhciUsb2PortBitMask(void);
UINT32 UsbHostXhciUsb3PortBitMask(void);
UINT32 UsbHostXhciUsb2ProtocolSlotType(void);

int UsbHostRootPortPower(UINT32 PortIndexOneBased);
int UsbHostRootPortReset(UINT32 PortIndexOneBased);
UINT32 UsbHostRootPortStatus(UINT32 PortIndexOneBased);
int UsbHostEnumerateHubOnPort(UINT32 PortIndexOneBased, UINT32 *HubAddressOut);
UINT32 UsbHostEnumerateStatusCode(void);
void UsbHostEnumerateStatusCodeClear(void);
void UsbHostEnumerateStatusCodeSet(UINT32 Code);
UINT32 UsbHostHubSlotId(void);
UINT32 UsbHostHubRootPortIndex(void);
UINT32 UsbHostHubPortSpeed(void);
UINT32 UsbHostKeyboardStatusCode(void);
UINT32 UsbHostKeyboardStatusLogCount(void);
UINT32 UsbHostKeyboardStatusLogAt(UINT32 Index);
void UsbHostKeyboardStatusCodeClear(void);
void UsbHostKeyboardLogFreeze(void);

int UsbHostFinishHubSetup(UINT32 HubSlotId, UINT32 RootPortIndex, UINT32 PortSpeed,
                          UINT32 *PortCountOut);
int UsbHostHubGetPortStatus(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT32 *StatusOut);
int UsbHostHubSetPortFeature(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT16 Feature);
int UsbHostHubClearPortFeature(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT16 Feature);
int UsbHostFindBootKeyboardBehindHub(UINT32 HubSlotId, UINT32 RootPortIndex,
                                     UINT32 HubPortSpeedValue, UINT32 HubPortCount,
                                     UINT32 *KeyboardSlotOut);
/* IssueSetAddress≠0：BSR1→BSR0；＝0：只 BSR1（嵌套口先 addr0 GetDesc） */
int UsbHostAddressKeyboardOnHubPort(UINT32 HubSlotId, UINT32 RootPort,
                                    UINT32 ParentRouteString, UINT32 HubPortOneBased,
                                    UINT32 DeviceSpeed, UINT32 IssueSetAddress,
                                    UINT32 *KeyboardSlotOut);
int UsbHostChildSetAddress(UINT32 SlotId, UINT32 DeviceSpeed,
                           UINT32 EndpointZeroMaxPacket);
int UsbHostProbeChildSlot(UINT32 SlotId, UINT32 RootPort, UINT32 HubSlotId,
                          UINT32 HubPort, UINT32 DeviceSpeed);
void UsbHostProbeTierSet(UINT32 Tier);
UINT32 UsbHostChildSlotSpeed(UINT32 HubPortStatus);
int UsbHostHidBootKeyboardConfigure(UINT32 KeyboardSlotId, UINT32 RootPort,
                                    UINT32 HubSlotId, UINT32 HubPortOneBased,
                                    UINT32 DeviceSpeed);
int UsbHostParseBootKeyboardEndpoint(const UINT8 *Configuration, UINT16 Total,
                                     UINT8 *InterfaceOut, UINT8 *EndpointAddressOut,
                                     UINT16 *MaxPacketSizeOut, UINT8 *IntervalOut);
char UsbHostHidBootUsageToAscii(UINT8 Usage, UINT8 Modifier);
int UsbHostHidBootKeyboardPoll(void);
void UsbHostPaintKeyboardStatus(void);
void UsbHostKeyboardStatusCodeSet(UINT32 Code);

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable,
                           UINT32 HubAddress);

/*
 * splash 行位：112＝阶段；176 起 p1…tc（×5）；其后 KEY / k。
 * 行距 64（PaintLine 清高 ≈56）。
 */
#define USB_HOST_SPLASH_LINE_STRIDE 64u
#define USB_HOST_SPLASH_LINE_PORTS 176u
#define USB_HOST_SPLASH_LINE_REPEATER                                      \
    (USB_HOST_SPLASH_LINE_PORTS + USB_HOST_SPLASH_LINE_STRIDE)
#define USB_HOST_SPLASH_LINE_PHYSICAL_BASE                                 \
    (USB_HOST_SPLASH_LINE_REPEATER + USB_HOST_SPLASH_LINE_STRIDE)
#define USB_HOST_SPLASH_LINE_PHYSICAL_COMMON                               \
    (USB_HOST_SPLASH_LINE_PHYSICAL_BASE + USB_HOST_SPLASH_LINE_STRIDE)
#define USB_HOST_SPLASH_LINE_TYPE_C                                        \
    (USB_HOST_SPLASH_LINE_PHYSICAL_COMMON + USB_HOST_SPLASH_LINE_STRIDE)
#define USB_HOST_SPLASH_LINE_KEYBOARD                                      \
    (USB_HOST_SPLASH_LINE_TYPE_C + USB_HOST_SPLASH_LINE_STRIDE)

int UsbHostHighSpeedInterIntegratedCircuitWrite(UINT8 DeviceAddress,
                                                  const UINT8 *Bytes,
                                                  UINT32 Length);
int UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
    UINT8 DeviceAddress, UINT8 RegisterAddress, UINT8 *Bytes, UINT32 Length);
void UsbHostUniversalSerialInterfaceEnableInterIntegratedCircuit(void);
UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticControl(void);
UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticUniversalSerial(void);
UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticInterrupt(void);
UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticTransfer(void);

int UsbHostTypeCForceHost(void);
void UsbHostTypeCRefreshStatusBytes(void);
UINT8 UsbHostTypeCLastResultCode(void);
UINT8 UsbHostTypeCCcStatusByte(void);
UINT8 UsbHostTypeCPowerStatusByte(void);
UINT8 UsbHostTypeCRoleControlByte(void);
UINT8 UsbHostTypeCUsbSwitchByte(void);
UINT8 UsbHostTypeCBuckBoostByte(void);

int UsbHostRepeaterEnableHost(void);
void UsbHostRepeaterRefreshStatusBytes(void);
UINT8 UsbHostRepeaterLastResultCode(void);
UINT8 UsbHostRepeaterRevisionByte(void);
UINT8 UsbHostRepeaterGlobalConfigurationByte(void);
UINT8 UsbHostRepeaterPort0ConfigurationByte(void);
UINT8 UsbHostRepeaterPort1ConfigurationByte(void);
UINT8 UsbHostRepeaterLowPowerUartPort1Byte(void);

int UsbHostClockEnableEmbeddedUsb(void);
UINT8 UsbHostClockEnableStatusByte(void);
int UsbHostDualRoleDevicePhysicalInitialize(void);
UINT8 UsbHostDualRoleDevicePhysicalLastResultCode(void);
UINT32 UsbHostDualRoleDevicePhysicalClockReset(void);
UINT32 UsbHostDualRoleDevicePhysicalUtmi(void);
UINT32 UsbHostDualRoleDevicePhysicalPowerManagement(void);
int UsbHostAliveClockPowerManagerEnableEmbeddedUsbRails(void);
UINT8 UsbHostAliveClockPowerManagerLastResultCode(void);
UINT32 UsbHostAliveClockPowerManagerStatusBits(void);
int UsbHostPhysicalEnableRepeaterMode(void);
void UsbHostPhysicalRetriggerHostModeEnable(void);
void UsbHostPhysicalRefreshCommonControl(void);
void UsbHostPhysicalStatusClear(void);
void UsbHostPhysicalStatusSetResultCode(UINT8 ResultCode);
void UsbHostPhysicalStatusSetFoundBasePage(UINT32 Page);
void UsbHostPhysicalStatusSetStrapAndCommon(UINT32 Strap, UINT32 Common);
void UsbHostPhysicalStatusSetEmbeddedUsbState(UINT8 State);
UINT8 UsbHostPhysicalLastResultCode(void);
UINT8 UsbHostPhysicalEmbeddedUsbState(void);
UINT8 UsbHostPhysicalLineState(void);
UINT8 UsbHostPhysicalUtmiPullDown(void);
UINT32 UsbHostPhysicalCommonControl(void);
UINT32 UsbHostPhysicalStrapControl(void);
UINT32 UsbHostPhysicalFoundBasePage(void);

#endif /* USB_HOST_PRIVATE_H */
