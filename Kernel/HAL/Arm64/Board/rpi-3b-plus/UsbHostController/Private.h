/*
 * UsbHostController/Private.h — Pi3B+ DWC2 主机内部（bd-pi-usb）
 */
#ifndef BOARD_USB_HOST_CONTROLLER_PRIVATE_H
#define BOARD_USB_HOST_CONTROLLER_PRIVATE_H

#include "../UsbHostController.h"
#include "Board.h"
#include "BoardConfig.h"
#include "Hal.h"
#include "SerialLog.h"
#include "DriverInput.h"
#if !BOARD_IS_VIRT
#include "Ethernet.h"
#endif

#define DWC2_DMA_BUF_SIZE  512u
#define DWC2_CTRL_RETRY    8u
/* BCM2837：DWC2/VC 见总线别名（U-Boot phys_to_bus；同 Mbox PHYS_TO_BUS） */
#define DWC2_PHYS_TO_BUS(P) (((UINT32)(UINTN)(P) & 0x3FFFFFFFu) | 0xC0000000u)

#define MOUSE_QUEUE_LENGTH 16
#define KEYBOARD_QUEUE_LENGTH 16

#define HUB_FEAT_PORT_RESET        4u
#define HUB_FEAT_PORT_POWER        8u
#define HUB_FEAT_C_PORT_CONNECTION 16u
#define HUB_FEAT_C_PORT_RESET      20u
#define HUB_FEAT_C_PORT_ENABLE     17u
#define HUB_STAT_CONNECT           (1u << 0)
#define HUB_STAT_ENABLE            (1u << 1)
#define HUB_STAT_RESET             (1u << 4)
#define HUB_STAT_LOW_SPEED         (1u << 9)
#define HUB_STAT_HIGH_SPEED        (1u << 10)

typedef struct {
    UINT8 BmRequestType;
    UINT8 BRequest;
    UINT16 WValue;
    UINT16 WIndex;
    UINT16 WLength;
} __attribute__((packed)) UsbSetup;

extern UINT8 gDmaBuffer[DWC2_DMA_BUF_SIZE];
extern UINT8 gSetupBuffer[8];
extern UINT8 gStatusBuffer[64];
extern UINT32 gRootSpeed;
extern UINT16 gEndpoint0MaxPacketSize;
extern UINT8 gHubAddress;
extern UINT8 gTransferHub;
extern UINT8 gTransferPort;
extern int gTransferDoSplit;
extern int gTransferLowSpeed;
extern UINT16 gTransferEndpoint0MaxPacketSize;

extern int gMouseOk;
extern UINT8 gMouseAddress;
extern UINT8 gMouseEndpoint;
extern UINT16 gMouseMaxPacketSize;
extern UINT8 gMouseToggle;
extern UINT8 gMouseHub;
extern UINT8 gMousePort;
extern int gMouseSplit;
extern int gMouseLowSpeed;
extern UINT32 gMouseX;
extern UINT32 gMouseY;
extern int gMouseAbsoluteInitialized;
extern UINT32 gMouseQueueX[MOUSE_QUEUE_LENGTH];
extern UINT32 gMouseQueueY[MOUSE_QUEUE_LENGTH];
extern UINT8 gMouseQueueButtons[MOUSE_QUEUE_LENGTH];
extern INT8 gMouseQueueWheel[MOUSE_QUEUE_LENGTH];
extern UINT8 gMouseQueueHead;
extern UINT8 gMouseQueueTail;

extern int gKeyboardOk;
extern UINT8 gKeyboardAddress;
extern UINT8 gKeyboardEndpoint;
extern UINT16 gKeyboardMaxPacketSize;
extern UINT8 gKeyboardToggle;
extern UINT8 gKeyboardHub;
extern UINT8 gKeyboardPort;
extern int gKeyboardSplit;
extern int gKeyboardLowSpeed;
extern HAL_KEYBOARD_REPORT gKeyboardQueue[KEYBOARD_QUEUE_LENGTH];
extern UINT8 gKeyboardQueueHead;
extern UINT8 gKeyboardQueueTail;

extern UINT8 gNextAddress;
extern UINT8 gPortDone;
extern int gEthernetAttached;
extern const INPUT_BACKEND gInputBackend;

volatile UINT32 *Dwc2Register(UINT32 Offset);
void Dwc2Spin(UINT32 N);
int Dwc2WaitBits(UINT32 Offset, UINT32 Mask, UINT32 Want, UINT32 Spins);
void Dwc2Hex32(UINT32 V);
void Dwc2Hex16(UINT16 V);
void Dwc2DelayMilliseconds(UINT32 Ms);
void Dwc2HostPortWrite(UINT32 Val);
void Dwc2FlushTransmitFifo(UINT32 Number);
void Dwc2FlushReceiveFifo(void);
void Dwc2HaltChannels(void);
int Dwc2RootPortReset(void);

void HostChannelInitialize(UINT8 DeviceAddress, UINT8 EndpointNumber, int EndpointIn, UINT8 EndpointType,
            UINT16 MaxPacketSize, int LowSpeed, int DoSplit);
int WaitChannelHalted(UINT32 *BytesLeftOut, UINT8 *PacketIdOut);
int TransferChunk(UINT8 *PacketId, int In, void *Buffer, UINT32 PacketCount,
                  UINT32 TransferLength, UINT32 *ActualLength, int OddFrame);
int ChunkMessage(UINT8 DeviceAddress, UINT8 EndpointNumber, UINT8 EndpointType, int In,
             UINT8 *PacketId, void *Buffer, int Length, UINT16 MaxPacketSize);
void TransferTargetHub(void);
void TransferTargetSplit(UINT8 Hub, UINT8 Port, int Low, UINT16 Endpoint0MaxPacketSize);
void TransferTargetHighSpeed(UINT8 Hub, UINT8 Port, UINT16 Endpoint0MaxPacketSize);
int ControlMessage(UINT8 DeviceAddress, UsbSetup *Setup, void *Data, int Length);

int HubGetPortStatus(UINT8 Hub, UINT8 Port, UINT16 *Status, UINT16 *Change);
int HubSetPortFeature(UINT8 Hub, UINT8 Port, UINT16 Feature, int Set);
int HubClearTtBuffer(UINT8 Hub, UINT8 Port, UINT8 DeviceAddress, UINT8 EndpointNumber,
                     int EndpointIn);
int HubResetPort(UINT8 Hub, UINT8 Port, UINT16 *StatusOut);
void HubScanPortsOnce(UINT8 Hub, UINT8 PortCount, UINT8 Pass);
void HubScanPorts(UINT8 Hub);

int ParseBootMouse(UINT8 *Config, UINT16 Total, UINT8 *InterfaceIndex, UINT8 *Endpoint,
                   UINT16 *MaxPacketSize, UINT8 *ScoreOut);
int ParseBootKeyboard(UINT8 *Config, UINT16 Total, UINT8 *InterfaceIndex, UINT8 *Endpoint,
                      UINT16 *MaxPacketSize, UINT8 *ScoreOut);
void HidSetBootIdle(UINT8 Address, UINT8 InterfaceIndex);
int EnumerateHidOnPort(UINT8 Hub, UINT8 Port, UINT16 PortStatus);
void HidPoll(void);

#if !BOARD_IS_VIRT
int EnumerateEthernetOnPort(UINT8 Hub, UINT8 Port);
#endif

int EnumerateRootHub(void);

#endif
