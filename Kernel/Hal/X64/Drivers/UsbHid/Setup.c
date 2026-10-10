/*
 * Setup.c — xHCI SET_ADDRESS / SET_CONFIGURATION 等控制传输
 *
 * 【初学者】
 * - UsbHid 子模块：枚举阶段 Configure 设备；配合 Command/Transfer 环。
 */
#include "Internal.h"

static UINT8 FullSpeedInterval(UINT8 BInterval) {
    UINT8 Log2 = 0;
    UINT8 V = BInterval ? BInterval : 1u;
    while (V > 1u) {
        V >>= 1;
        Log2++;
    }
    return (UINT8)(Log2 + 3u);
}

static int HidParse(UINT8 *Configuration, UINT16 Total, int WantKeyboard, USB_HID_DEVICE *Device) {
    UINT16 Offset = 0;
    UINT8 BestScore = 0;
    UINT8 CurrentScore = 0;
    UINT8 CurrentInterface = 0;
    UINT8 CurrentProtocol = 0;

    Device->Interface = 0;
    Device->EndpointAddress = 0;
    Device->MaxPacketSize = 8;
    Device->Interval = 10;
    Device->Protocol = 0xFF;
    Device->Absolute = 0;

    while (Offset + 2u <= Total) {
        UINT8 Length = Configuration[Offset];
        UINT8 Type = Configuration[Offset + 1u];
        if (Length < 2u || Offset + Length > Total) {
            break;
        }
        if (Type == 4u && Length >= 9u) {
            UINT8 Class = Configuration[Offset + 5u];
            UINT8 SubClass = Configuration[Offset + 6u];
            UINT8 Protocol = Configuration[Offset + 7u];
            CurrentScore = 0;
            CurrentInterface = Configuration[Offset + 2u];
            CurrentProtocol = Protocol;
            if (WantKeyboard) {
                if (Class == 3u && SubClass == 1u && Protocol == 1u) {
                    CurrentScore = 3;
                } else if (Class == 3u && SubClass == 1u && Protocol == 0u) {
                    CurrentScore = 2;
                }
            } else if (Class == 3u && Protocol != 1u) {
                if (SubClass == 1u && Protocol == 2u) {
                    CurrentScore = 3;
                } else if (SubClass == 1u) {
                    CurrentScore = 2;
                } else {
                    CurrentScore = 1;
                }
            }
        } else if (Type == 5u && Length >= 7u && CurrentScore != 0) {
            UINT8 Address = Configuration[Offset + 2u];
            UINT8 Attributes = Configuration[Offset + 3u];
            if ((Address & 0x80u) && ((Attributes & 0x03u) == 0x03u) && CurrentScore > BestScore) {
                BestScore = CurrentScore;
                Device->Interface = CurrentInterface;
                Device->EndpointAddress = Address;
                Device->MaxPacketSize = (UINT16)(Configuration[Offset + 4u] | (Configuration[Offset + 5u] << 8));
                Device->Interval = Configuration[Offset + 6u];
                Device->Protocol = CurrentProtocol;
                if (BestScore == 3u) {
                    break;
                }
            }
        }
        Offset = (UINT16)(Offset + Length);
    }
    if (BestScore == 0) {
        return 0;
    }
    if (!WantKeyboard && Device->Protocol != 2u) {
        Device->Absolute = 1;
    }
    return 1;
}

int DeviceConfigure(USB_HID_DEVICE *Device, int WantKeyboard) {
    SETUP_PACKET Setup;
    UINT16 Total;
    UINT8 ConfigurationValue;
    UINT32 *Slot;
    UINT32 *EndpointContext;
    UINT64 Deq;
    UINT8 Interval;
    UINT16 MaxPacketSize;
    UINT8 EndpointNumber;
    UINT8 In;

    gTransferDevice = Device;
    if (DescriptorGet(0x0100u, 8, gControlBuffer) < 0 || DescriptorGet(0x0100u, 18, gControlBuffer) < 0) {
        return -1;
    }
    if (DescriptorGet(0x0200u, 9, gControlBuffer) < 0) {
        return -1;
    }
    Total = (UINT16)(gControlBuffer[2] | (gControlBuffer[3] << 8));
    if (Total < 9u) {
        Total = 9;
    }
    if (Total > 512u) {
        Total = 512;
    }
    if (DescriptorGet(0x0200u, Total, gControlBuffer) < 0) {
        return -1;
    }
    if (!HidParse(gControlBuffer, Total, WantKeyboard, Device)) {
        return -1;
    }
    ConfigurationValue = gControlBuffer[5] ? gControlBuffer[5] : 1u;
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 0x09;
    Setup.WValue = ConfigurationValue;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlTransfer(&Setup, 0) < 0) {
        return -1;
    }
    if (Device->Protocol == 1u || Device->Protocol == 2u) {
        Setup.BmRequestType = 0x21;
        Setup.BRequest = 0x0B;
        Setup.WValue = 0;
        Setup.WIndex = Device->Interface;
        Setup.WLength = 0;
        (void)ControlTransfer(&Setup, 0);
    }
    Setup.BmRequestType = 0x21;
    Setup.BRequest = 0x0A;
    Setup.WValue = 0;
    Setup.WIndex = Device->Interface;
    Setup.WLength = 0;
    (void)ControlTransfer(&Setup, 0);

    EndpointNumber = Device->EndpointAddress & 0x0Fu;
    In = (Device->EndpointAddress & 0x80u) ? 1u : 0u;
    Device->InterruptDeviceContextIndex = (UINT32)EndpointNumber * 2u + In;
    MaxPacketSize = Device->MaxPacketSize;
    if (MaxPacketSize == 0 || MaxPacketSize > 64u) {
        MaxPacketSize = 8;
    }
    Device->MaxPacketSize = MaxPacketSize;

    MemoryZero(gInputContext, INPUT_CONTEXT_BYTES);
    *(UINT32 *)(void *)(gInputContext + 4) = (1u << 0) | (1u << Device->InterruptDeviceContextIndex);
    Slot = (UINT32 *)(void *)InputContextSlot();
    Slot[0] = (Device->InterruptDeviceContextIndex << 27) | ((UINT32)Device->Speed << 20);
    Slot[1] = (UINT32)Device->Port << 16;
    RingInitialize(Device->InterruptRing, &Device->Interrupt, TRANSFER_RING_SIZE);
    EndpointContext = (UINT32 *)(void *)InputContextEndpoint(Device->InterruptDeviceContextIndex);
    Interval = (Device->Speed >= 3u)
                   ? (UINT8)((Device->Interval > 0) ? (Device->Interval - 1u) : 0u)
                   : FullSpeedInterval(Device->Interval);
    EndpointContext[0] = (UINT32)Interval << 16;
    EndpointContext[1] = (3u << 1) | (7u << 3) | ((UINT32)MaxPacketSize << 16);
    Deq = PhysicalAddress(Device->InterruptRing) | 1ULL;
    EndpointContext[2] = (UINT32)Deq;
    EndpointContext[3] = (UINT32)(Deq >> 32);
    EndpointContext[4] = (UINT32)MaxPacketSize | ((UINT32)MaxPacketSize << 16);
    if (CommandSubmit(PhysicalAddress(gInputContext), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(Device->SlotIdentifier), 0) < 0) {
        return -1;
    }
    InterruptQueue(Device);
    return 0;
}

