/*
 * UsbHost/AliveClockPowerManagerPrivate.h — ACPM 开 eUSB LDO 私有
 */
#ifndef ALIVE_CLOCK_POWER_MANAGER_PRIVATE_H
#define ALIVE_CLOCK_POWER_MANAGER_PRIVATE_H

#include "UsbHost/Private.h"

#define ALIVE_CLOCK_POWER_MAILBOX_PHYSICAL_BASE 0x15110000ULL
#define ALIVE_CLOCK_POWER_SHARED_MEMORY_PHYSICAL_BASE 0x15700000ULL
#define ALIVE_CLOCK_POWER_INITDATA_OFFSET 0xA000u

#define MAILBOX_INTERRUPT_GROUP1 0x40u
#define MAILBOX_INTERRUPT_MASK0 0x28u
#define MAILBOX_INTERRUPT_CLEAR0 0x24u

#define POWER_MANAGEMENT_INTEGRATED_CIRCUIT_TYPE 0x01u
#define POWER_MANAGEMENT_INTEGRATED_CIRCUIT_CHANNEL 2u
#define SPEEDY_CHANNEL 0u

#define FUNCTION_UPDATE 2u
#define PROTOCOL_SEQUENCE_SHIFT 16u
#define SEQUENCE_NUMBER_MAX 63u
#define POLL_GUARD_MAX 2000000u

#define LDO8_ENABLE_REGISTER 0x44u
#define LDO8_ENABLE_MASK 0x03u
#define LDO9_ENABLE_REGISTER 0x33u
#define LDO9_ENABLE_MASK 0xC0u
#define LDO19_ENABLE_REGISTER 0x3Du
#define LDO19_ENABLE_MASK 0xC0u

typedef struct {
    UINT32 Plugins;
    UINT32 PluginCount;
    UINT32 InterProcessChannelsOffset;
    UINT32 InterProcessChannelCount;
    UINT32 FrameworkPluginId;
    UINT32 PluginIdMax;
    UINT32 ApplicationProcessorChannelMax;
} AliveClockPowerFrameworkHeader;

typedef struct {
    UINT32 ReceiveRear;
    UINT32 ReceiveFront;
    UINT32 ReceiveBase;
    UINT32 ReceiveIndirectBuffer;
    UINT32 ReceiveIndirectBufferBase;
    UINT32 ReceiveIndirectBufferSize;
    UINT32 TransmitRear;
    UINT32 TransmitFront;
    UINT32 TransmitBase;
    UINT32 QueueLength;
    UINT32 QueueElementSize;
    UINT32 Credit;
    UINT32 MailboxAddress;
} AliveClockPowerChannelInfo;

typedef struct {
    UINT32 Identifier;
    UINT32 FieldPacked;
    INT32 Owner;
    UINT32 Type;
    AliveClockPowerChannelInfo Channel;
    UINT32 ApplicationProcessorPoll;
} AliveClockPowerChannelDescriptor;

typedef struct {
    volatile UINT32 *Rear;
    volatile UINT32 *Front;
    volatile UINT32 *Base;
    UINT32 ElementSizeBytes;
    UINT32 QueueLength;
    UINT32 HardwareIdentifier;
} AliveClockPowerRing;

extern UINT8 gAliveClockPowerLastResultCode;
extern UINT8 gAliveClockPowerLastReturnCode;
extern UINT32 gAliveClockPowerStatusBits;

void AliveClockPowerStallMicroseconds(UINT32 Microseconds);
int AliveClockPowerEnableEmbeddedUsbRails(AliveClockPowerRing *Transmit,
                                          AliveClockPowerRing *Receive,
                                          UINT64 MailboxBase,
                                          UINT8 *SequenceNumber);

#endif
