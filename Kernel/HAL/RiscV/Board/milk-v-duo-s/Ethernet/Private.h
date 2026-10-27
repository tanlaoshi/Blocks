/*
 * Ethernet/Private.h — Duo DWMAC + EPHY 内部（bd-duo-eth）
 */
#ifndef BOARD_ETHERNET_PRIVATE_H
#define BOARD_ETHERNET_PRIVATE_H

#include "Ethernet.h"
#include "Board.h"
#include "Compiler.h"
#include "Driver.h"
#include "DriverNet.h"
#include "Hal.h"
#include "HalDevices.h"
#include "SerialLog.h"
#include "Text.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

typedef struct {
    UINT32 Descriptor0;
    UINT32 Descriptor1;
    UINT32 Descriptor2;
    UINT32 Descriptor3;
} EthernetDmaDescriptor;

#define ETHERNET_ETH_HEADER_LENGTH 14u
#define ETHERNET_IP_HEADER_LENGTH 20u
#define ETHERNET_ICMP_HEADER_LENGTH 8u
#define ETHERNET_ETH_TYPE_ARP 0x0806u
#define ETHERNET_ETH_TYPE_IP 0x0800u
#define ETHERNET_ARP_OPERATION_REQUEST 1u
#define ETHERNET_ARP_OPERATION_REPLY 2u
#define ETHERNET_IP_PROTOCOL_ICMP 1u
#define ETHERNET_ICMP_ECHO_REQUEST 8u
#define ETHERNET_ICMP_ECHO_REPLY 0u
#define ETHERNET_ARP_CACHE_SIZE 4u
#define ETHERNET_PING_PAYLOAD_LENGTH 32u

typedef struct {
    UINT32 Ip;
    UINT8 Mac[6];
    int Valid;
} EthernetArpEntry;

typedef struct {
    UINT8 Destination[6];
    UINT8 Source[6];
    UINT16 EtherType;
} BLOCKS_PACKED EthernetEthHeader;

typedef struct {
    UINT16 HardwareType;
    UINT16 ProtocolType;
    UINT8 HardwareLength;
    UINT8 ProtocolLength;
    UINT16 Operation;
    UINT8 SenderMac[6];
    UINT32 SenderIp;
    UINT8 TargetMac[6];
    UINT32 TargetIp;
} BLOCKS_PACKED EthernetArpPacket;

typedef struct {
    UINT8 VersionIhl;
    UINT8 TypeOfService;
    UINT16 TotalLength;
    UINT16 Identification;
    UINT16 Fragment;
    UINT8 TimeToLive;
    UINT8 Protocol;
    UINT16 Checksum;
    UINT32 Source;
    UINT32 Destination;
} BLOCKS_PACKED EthernetIpHeader;

typedef struct {
    UINT8 Type;
    UINT8 Code;
    UINT16 Checksum;
    UINT16 Id;
    UINT16 Sequence;
} BLOCKS_PACKED EthernetIcmpHeader;

extern EthernetDmaDescriptor gDuoTransmitDescriptor[DUO_ETHERNET_DMA_RING_COUNT];
extern EthernetDmaDescriptor gDuoReceiveDescriptor[DUO_ETHERNET_DMA_RING_COUNT];
extern UINT8 gDuoTransmitBuffer[DUO_ETHERNET_DMA_RING_COUNT]
    [DUO_ETHERNET_DMA_BUFFER_SIZE];
extern UINT8 gDuoReceiveBuffer[DUO_ETHERNET_DMA_RING_COUNT]
    [DUO_ETHERNET_DMA_BUFFER_SIZE];

extern int gDuoMacOk;
extern int gDuoPhysicalOk;
extern int gDuoDmaOk;
extern int gDuoBound;
extern UINT32 gDuoPhysicalAddress;
extern UINT8 gDuoMac[6];
extern UINT32 gDuoIp;
extern UINT32 gDuoTransmitIndex;
extern UINT32 gDuoReceiveIndex;
extern UINT32 gDuoTransmitDone;
extern UINT32 gDuoReceiveFrames;
extern int gDuoLwIpReceive;
extern EthernetArpEntry gDuoArpCache[ETHERNET_ARP_CACHE_SIZE];
extern int gDuoPingWait;
extern UINT32 gDuoPingTarget;
extern UINT16 gDuoPingId;
extern UINT16 gDuoPingSequence;

void DuoMemoryCopy(void *Destination, const void *Source, UINTN Length);
void DuoMemorySet(void *Destination, UINT8 Value, UINTN Length);
UINT16 DuoByteSwap16(UINT16 Value);
UINT32 DuoByteSwap32(UINT32 Value);
volatile UINT32 *DuoRegister(UINT64 Address);
void DuoSpin(UINT32 Count);
void DuoDelayMilliseconds(UINT32 Milliseconds);
void DuoMarkHex(const char *Tag, UINT32 Value);
void DuoMarkHex16(UINT32 Value);
int DuoVersionIsLive(UINT32 Version);

int DuoMediaIndependentInterfaceWaitIdle(void);
int DuoMediaIndependentInterfaceRead(UINT32 PhysicalAddress, UINT32 RegisterIndex, UINT16 *OutValue);
int DuoMediaIndependentInterfaceWrite(UINT32 PhysicalAddress, UINT32 RegisterIndex, UINT16 Value);

void DuoPhysicalInitialize(void);
void DuoPhysicalConfigure(void);
int DuoPhysicalIdentityValid(UINT16 Identifier1, UINT16 Identifier2);
int DuoPhysicalFind(UINT32 *OutAddress, UINT16 *OutIdentifier1,
               UINT16 *OutIdentifier2);
int DuoPhysicalReadBasicStatus(UINT32 Address, UINT16 *OutStatus);
int DuoPhysicalRestartAutoneg(UINT32 Address);
int DuoPhysicalProbe(void);
int DuoPhysicalLinkUp(void);

int DuoDmaSoftwareReset(void);
void DuoDmaCacheSync(void *Address, UINTN Length);
int DuoDmaWaitOwnClear(volatile EthernetDmaDescriptor *Descriptor);
void DuoDmaProgramMacAddress(void);
void DuoDmaRearmReceive(UINT32 Index);
int DuoDmaProbe(void);

void DuoMacAdjustFromPhysical(void);
int DuoDmaStartWire(void);

int DuoArpLookup(UINT32 Ip, UINT8 Mac[6]);
void DuoArpLearn(UINT32 Ip, const UINT8 Mac[6]);
void DuoEthernetSendArpRequest(UINT32 TargetIp);
void DuoEthernetHandleArp(const EthernetArpPacket *Arp);
void DuoEthernetHandleIcmp(const EthernetIpHeader *Ip, const UINT8 *Payload,
                           UINTN PayloadLength, const UINT8 *SourceMac);
void DuoEthernetHandleFrame(const UINT8 *Frame, UINTN FrameLength);

void DuoEthernetPoll(void);
int DuoEthernetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMilliseconds);

int DuoEthernetReady(void);
void DuoEthernetGetMac(UINT8 Mac[6]);
UINT32 DuoEthernetGetIp(void);
void DuoEthernetSetIp(UINT32 Ip);
void DuoEthernetFormatIp(UINT32 Ip, char *Buffer, int BufferLength);
int DuoEthernetParseIp(const char *Text, UINT32 *Ip);
UINT16 DuoEthernetChecksum(const void *Data, UINTN Length);
void DuoEthernetGetStats(UINT32 *TransmitDone, UINT32 *ReceiveFrames);
void DuoEthernetSetLwIpReceive(int Enable);
int DuoEthernetSendIp(UINT32 DestinationIp, UINT8 Protocol, const void *Payload,
                      UINTN Length);
int DuoEthernetPing(const char *Host, int TimeoutMilliseconds);

extern const DRIVER gEthernetDriver;

#endif
