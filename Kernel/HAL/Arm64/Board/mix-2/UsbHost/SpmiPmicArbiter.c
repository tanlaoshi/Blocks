/*
 * UsbHost/SpmiPmicArbiter.c — MSM8998 SPMI PMIC arbiter 最小读写（v2/v3）
 *
 * 证据：chiron DT `spmi@800f000` reg core/chnls/obsrvr/cnfg；`qcom,ee=<0>`。
 * 对表 torvalds `spmi-pmic-arb.c`：v2 offset / fmt_cmd / find_apid / STATUS DONE。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define SPMI_CORE_BASE                 0x0800F000ULL
#define SPMI_CHANNELS_BASE             0x08400000ULL
#define SPMI_OBSERVER_BASE             0x09400000ULL
#define SPMI_CONFIG_BASE               0x0800A000ULL
#define SPMI_CORE_SIZE                 0x1000u

#define SPMI_ARBITER_VERSION_OFFSET    0x0000u
#define SPMI_ARBITER_VERSION_V2_MIN    0x20010000u
#define SPMI_ARBITER_VERSION_V5_MIN    0x50000000u

#define SPMI_COMMAND_OFFSET            0x00u
#define SPMI_STATUS_OFFSET             0x08u
#define SPMI_WRITE_DATA0_OFFSET        0x10u
#define SPMI_READ_DATA0_OFFSET         0x18u

#define SPMI_STATUS_DONE               (1u << 0)
#define SPMI_STATUS_FAILURE            (1u << 1)
#define SPMI_STATUS_DENIED             (1u << 2)
#define SPMI_STATUS_DROPPED            (1u << 3)

#define SPMI_OPERATION_EXTENDED_WRITE_LONG 0u
#define SPMI_OPERATION_EXTENDED_READ_LONG  1u

#define SPMI_EXECUTION_ENVIRONMENT     0u
#define SPMI_APPLICATION_IDENTIFIER_MAX 128u

static UINT16 sPeripheralToApplication[0x1000];
static UINT16 sLastApplicationIdentifier;

static UINT32 SpmiReadCore(UINT32 Offset) {
    return UsbHostReadMmio32(SPMI_CORE_BASE + (UINT64)Offset);
}

static UINT32 SpmiReadConfig(UINT32 Offset) {
    return UsbHostReadMmio32(SPMI_CONFIG_BASE + (UINT64)Offset);
}

static int SpmiFindApplicationIdentifier(UINT16 PeripheralIdentifier) {
    UINT16 ApplicationIdentifier;
    UINT32 MapOffset;
    UINT32 MapValue;
    UINT16 MappedPeripheral;

    if ((sPeripheralToApplication[PeripheralIdentifier] & 0x8000u) != 0) {
        return (int)(sPeripheralToApplication[PeripheralIdentifier] & 0x7FFFu);
    }

    /* 必须从 0 扫：上次 APID 偏高会漏掉更低的 PPID（USBIN 0x13 vs MISC 0x16） */
    for (ApplicationIdentifier = 0;
         ApplicationIdentifier < SPMI_APPLICATION_IDENTIFIER_MAX;
         ApplicationIdentifier++) {
        MapOffset = 0x800u + 4u * (UINT32)ApplicationIdentifier;
        if (MapOffset + 4u > SPMI_CORE_SIZE) {
            break;
        }
        (void)SpmiReadConfig(0x700u + 4u * (UINT32)ApplicationIdentifier);
        MapValue = SpmiReadCore(MapOffset);
        if (MapValue == 0) {
            continue;
        }
        MappedPeripheral = (UINT16)((MapValue >> 8) & 0xFFFu);
        sPeripheralToApplication[MappedPeripheral] =
            (UINT16)(ApplicationIdentifier | 0x8000u);
        if (MappedPeripheral == PeripheralIdentifier) {
            sLastApplicationIdentifier = ApplicationIdentifier;
            return (int)ApplicationIdentifier;
        }
    }
    return -1;
}

static int SpmiChannelOffset(UINT8 SlaveIdentifier, UINT16 Address) {
    UINT16 PeripheralIdentifier =
        (UINT16)(((UINT16)SlaveIdentifier << 8) | ((Address >> 8) & 0xFFu));
    int ApplicationIdentifier = SpmiFindApplicationIdentifier(PeripheralIdentifier);

    if (ApplicationIdentifier < 0) {
        return -1;
    }
    return (int)(0x1000u * SPMI_EXECUTION_ENVIRONMENT +
                 0x8000u * (UINT32)ApplicationIdentifier);
}

static UINT32 SpmiFormatCommand(UINT32 Operation, UINT16 Address, UINT8 ByteCountMinusOne) {
    return (Operation << 27) | (((UINT32)Address & 0xFFu) << 4) |
           ((UINT32)ByteCountMinusOne & 0x7u);
}

static int SpmiWaitDone(UINT64 ChannelBase) {
    UINTN Guard;
    UINT32 Status;

    for (Guard = 0; Guard < 10000u; Guard++) {
        Status = UsbHostReadMmio32(ChannelBase + SPMI_STATUS_OFFSET);
        if ((Status & SPMI_STATUS_DONE) != 0) {
            if ((Status & (SPMI_STATUS_FAILURE | SPMI_STATUS_DENIED |
                           SPMI_STATUS_DROPPED)) != 0) {
                return -1;
            }
            return 0;
        }
        UsbHostStallYield(50u);
    }
    return -1;
}

int UsbHostSpmiWriteByte(UINT8 SlaveIdentifier, UINT16 Address, UINT8 Value) {
    int Channel;
    UINT64 ChannelBase;
    UINT32 Command;

    Channel = SpmiChannelOffset(SlaveIdentifier, Address);
    if (Channel < 0) {
        return -1;
    }
    ChannelBase = SPMI_CHANNELS_BASE + (UINT64)(UINT32)Channel;
    UsbHostWriteMmio32(ChannelBase + SPMI_WRITE_DATA0_OFFSET, Value);
    Command = SpmiFormatCommand(SPMI_OPERATION_EXTENDED_WRITE_LONG, Address, 0);
    UsbHostWriteMmio32(ChannelBase + SPMI_COMMAND_OFFSET, Command);
    return SpmiWaitDone(ChannelBase);
}

int UsbHostSpmiReadByte(UINT8 SlaveIdentifier, UINT16 Address, UINT8 *ValueOut) {
    int Channel;
    UINT64 WriteChannelBase;
    UINT64 ReadChannelBase;
    UINT32 Command;
    UINT32 Data;

    if (ValueOut == 0) {
        return -1;
    }
    Channel = SpmiChannelOffset(SlaveIdentifier, Address);
    if (Channel < 0) {
        return -1;
    }
    WriteChannelBase = SPMI_CHANNELS_BASE + (UINT64)(UINT32)Channel;
    ReadChannelBase = SPMI_OBSERVER_BASE + (UINT64)(UINT32)Channel;
    Command = SpmiFormatCommand(SPMI_OPERATION_EXTENDED_READ_LONG, Address, 0);
    UsbHostWriteMmio32(ReadChannelBase + SPMI_COMMAND_OFFSET, Command);
    if (SpmiWaitDone(ReadChannelBase) != 0) {
        return -1;
    }
    Data = UsbHostReadMmio32(ReadChannelBase + SPMI_READ_DATA0_OFFSET);
    *ValueOut = (UINT8)(Data & 0xFFu);
    (void)WriteChannelBase;
    return 0;
}

UINT32 UsbHostSpmiArbiterVersion(void) {
    return SpmiReadCore(SPMI_ARBITER_VERSION_OFFSET);
}

int UsbHostSpmiArbiterProbe(void) {
    UINT32 Version;

    Version = UsbHostSpmiArbiterVersion();
    /* v2/v3 同 offset；拒 v1 与 ≥v5（APID 模型不同） */
    if (Version < SPMI_ARBITER_VERSION_V2_MIN ||
        Version >= SPMI_ARBITER_VERSION_V5_MIN) {
        return -1;
    }
    return 0;
}

#else

UINT32 UsbHostSpmiArbiterVersion(void) {
    return 0;
}

int UsbHostSpmiArbiterProbe(void) {
    return -1;
}

int UsbHostSpmiWriteByte(UINT8 SlaveIdentifier, UINT16 Address, UINT8 Value) {
    (void)SlaveIdentifier;
    (void)Address;
    (void)Value;
    return -1;
}

int UsbHostSpmiReadByte(UINT8 SlaveIdentifier, UINT16 Address, UINT8 *ValueOut) {
    (void)SlaveIdentifier;
    (void)Address;
    (void)ValueOut;
    return -1;
}

#endif
