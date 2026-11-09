/*
 * UsbHost/HidBootConfigure.c — SET_PROTOCOL/SET_IDLE + ConfigEP 中断 IN
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define KEYBOARD_STATUS_LOG_CAPACITY 32u

static UINT32 sKeyboardStatusCode;
static UINT32 sKeyboardStatusLog[KEYBOARD_STATUS_LOG_CAPACITY];
static UINT32 sKeyboardStatusLogCount;
static UINT32 sKeyboardStatusLogFrozen;

static void WriteLittleEndian32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

static UINT32 ReadLittleEndian32(const UINT8 *Source) {
    return (UINT32)Source[0] | ((UINT32)Source[1] << 8) | ((UINT32)Source[2] << 16) |
           ((UINT32)Source[3] << 24);
}

UINT32 UsbHostKeyboardStatusCode(void) {
    return sKeyboardStatusCode;
}

UINT32 UsbHostKeyboardStatusLogCount(void) {
    return sKeyboardStatusLogCount;
}

UINT32 UsbHostKeyboardStatusLogAt(UINT32 Index) {
    if (Index >= sKeyboardStatusLogCount) {
        return 0;
    }
    return sKeyboardStatusLog[Index];
}

void UsbHostKeyboardStatusCodeClear(void) {
    UINTN Index;

    sKeyboardStatusCode = 0;
    sKeyboardStatusLogCount = 0;
    sKeyboardStatusLogFrozen = 0;
    for (Index = 0; Index < KEYBOARD_STATUS_LOG_CAPACITY; Index++) {
        sKeyboardStatusLog[Index] = 0;
    }
}

void UsbHostKeyboardLogFreeze(void) {
    sKeyboardStatusLogFrozen = 1u;
    UsbHostPaintKeyboardStatus();
}

void UsbHostKeyboardStatusCodeSet(UINT32 Code) {
    UINTN Index;

    /*
     * 首趟扫完 Freeze 后不再追加（重扫曾让 k 数字一直滚）。
     * 例外：0xA2 ConfigEP 成仍写入。
     */
    if (sKeyboardStatusLogFrozen != 0 && Code != 0xA2u) {
        return;
    }
    sKeyboardStatusCode = Code;
    for (Index = 0; Index < sKeyboardStatusLogCount; Index++) {
        if (sKeyboardStatusLog[Index] == Code) {
            return;
        }
    }
    if (sKeyboardStatusLogCount < KEYBOARD_STATUS_LOG_CAPACITY) {
        sKeyboardStatusLog[sKeyboardStatusLogCount] = Code;
        sKeyboardStatusLogCount++;
    } else {
        for (Index = 1; Index < KEYBOARD_STATUS_LOG_CAPACITY; Index++) {
            sKeyboardStatusLog[Index - 1u] = sKeyboardStatusLog[Index];
        }
        sKeyboardStatusLog[KEYBOARD_STATUS_LOG_CAPACITY - 1u] = Code;
    }
    UsbHostPaintKeyboardStatus();
}

static UINT8 FullSpeedInterval(UINT8 BInterval) {
    UINT8 Logarithm = 0;
    UINT8 Value;

    if (BInterval == 0) {
        BInterval = 1u;
    }
    Value = BInterval;
    while (Value > 1u) {
        Value = (UINT8)(Value >> 1);
        Logarithm++;
    }
    return (UINT8)(Logarithm + 3u);
}

static void HidClassSetup(UINT32 KeyboardSlotId, UINT8 Interface) {
    /* SET_PROTOCOL(Boot)=0；SET_IDLE(0) */
    (void)UsbHostXhciControlTransfer(KeyboardSlotId, 0x21u, 0x0Bu, 0, Interface, 0, 0);
    (void)UsbHostXhciControlTransfer(KeyboardSlotId, 0x21u, 0x0Au, 0, Interface, 0, 0);
}

int UsbHostHidBootKeyboardConfigure(UINT32 KeyboardSlotId, UINT32 RootPort,
                                    UINT32 HubSlotId, UINT32 HubPortOneBased,
                                    UINT32 DeviceSpeed) {
    UINT8 *ControlData = UsbHostControlDataBuffer();
    UINT8 *Input;
    UINT8 *Output;
    UINT32 ContextSize;
    UINT16 Total;
    UINT8 ConfigurationValue;
    UINT8 Interface = 0;
    UINT8 EndpointAddress = 0;
    UINT16 MaxPacketSize = 8;
    UINT8 IntervalByte = 10;
    UINT32 EndpointNumber;
    UINT32 DeviceContextIndex;
    UINT32 AddFlags;
    UINT64 Dequeue;
    UINTN Index;
    UINT8 IntervalField;
    UINT32 SlotDword0;

    (void)RootPort;
    /*
     * Probe 已把完整配置描述符放进 ControlData。勿再 GetConfig（SetConfig 后重取
     * 曾无 sticky 失败）。先 parse → SetConfig → ConfigEP；Route/TT 原样拷 Output。
     */
    Total = (UINT16)(ControlData[2] | ((UINT16)ControlData[3] << 8));
    if (Total < 9u) {
        Total = 9u;
    }
    if (Total > USB_CONTROL_DATA_CAPACITY) {
        Total = (UINT16)USB_CONTROL_DATA_CAPACITY;
    }
    ConfigurationValue = ControlData[5] != 0 ? ControlData[5] : 1u;
    if (UsbHostParseBootKeyboardEndpoint(ControlData, Total, &Interface, &EndpointAddress,
                                         &MaxPacketSize, &IntervalByte) != 0) {
        UsbHostKeyboardStatusCodeSet(0x42u);
        return -1;
    }
    if (MaxPacketSize == 0 || MaxPacketSize > 64u) {
        MaxPacketSize = 8u;
    }
    UsbHostKeyboardStatusCodeSet(0x27B0u | (HubPortOneBased & 0xFu));
    if (UsbHostXhciControlTransfer(KeyboardSlotId, 0x00u, 9u, ConfigurationValue, 0, 0,
                                   0) != 0) {
        UsbHostKeyboardStatusCodeSet(
            (0x26u << 16) | ((DeviceSpeed & 0xFu) << 12) |
            ((UsbHostXhciControlLastCompletionCode() & 0xFFu) << 4) |
            (HubPortOneBased & 0xFu));
        return -1;
    }
    HidClassSetup(KeyboardSlotId, Interface);

    EndpointNumber = EndpointAddress & 0x0Fu;
    DeviceContextIndex = EndpointNumber * 2u + 1u;
    AddFlags = (1u << 0) | (1u << DeviceContextIndex);
    ContextSize = UsbHostContextSizeBytes();
    Input = UsbHostInputContextBuffer();
    Output = UsbHostDeviceContextSlot(KeyboardSlotId);
    UsbHostCacheCleanInvalidateRange(Output, ContextSize * 32u);
    for (Index = 0; Index < ContextSize * 33u; Index++) {
        Input[Index] = 0;
    }
    WriteLittleEndian32(Input + 4u, AddFlags);
    for (Index = 0; Index < ContextSize; Index++) {
        Input[ContextSize + Index] = Output[Index];
    }
    /* 只抬 Context Entries；Route/Speed/TT/RootPort 保持 Address 时 Output */
    SlotDword0 = ReadLittleEndian32(Input + ContextSize);
    SlotDword0 = (SlotDword0 & 0x07FFFFFFu) | (DeviceContextIndex << 27);
    WriteLittleEndian32(Input + ContextSize + 0u, SlotDword0);

    UsbHostInterruptRingInitialize(KeyboardSlotId, DeviceContextIndex, MaxPacketSize);
    Dequeue = UsbHostInterruptRingPhysical();
    IntervalField = (DeviceSpeed >= 3u)
                        ? (UINT8)((IntervalByte > 0) ? (IntervalByte - 1u) : 0)
                        : FullSpeedInterval(IntervalByte);
    WriteLittleEndian32(Input + ContextSize * (DeviceContextIndex + 1u) + 0u,
                        (UINT32)IntervalField << 16);
    WriteLittleEndian32(Input + ContextSize * (DeviceContextIndex + 1u) + 4u,
                        (3u << 1) | (7u << 3) | ((UINT32)MaxPacketSize << 16));
    WriteLittleEndian32(Input + ContextSize * (DeviceContextIndex + 1u) + 8u,
                        (UINT32)Dequeue);
    WriteLittleEndian32(Input + ContextSize * (DeviceContextIndex + 1u) + 12u,
                        (UINT32)(Dequeue >> 32));
    WriteLittleEndian32(Input + ContextSize * (DeviceContextIndex + 1u) + 16u,
                        (UINT32)MaxPacketSize | ((UINT32)MaxPacketSize << 16));

    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);
    if (UsbHostXhciConfigureEndpoint(KeyboardSlotId) != 0) {
        UsbHostKeyboardStatusCodeSet(0x43u);
        return -1;
    }
    UsbHostKeyboardStatusCodeSet(0xA2u);
    /* 只登记槽/接口；勿在此 SET_REPORT（真机立刻发 → 2711） */
    UsbHostHidBootKeyboardLedsBind(KeyboardSlotId, Interface, HubSlotId,
                                   HubPortOneBased);
    return 0;
}

#else

int UsbHostHidBootKeyboardConfigure(UINT32 KeyboardSlotId, UINT32 RootPort,
                                    UINT32 HubSlotId, UINT32 HubPortOneBased,
                                    UINT32 DeviceSpeed) {
    (void)KeyboardSlotId;
    (void)RootPort;
    (void)HubSlotId;
    (void)HubPortOneBased;
    (void)DeviceSpeed;
    return -1;
}

UINT32 UsbHostKeyboardStatusCode(void) {
    return 0;
}

UINT32 UsbHostKeyboardStatusLogCount(void) {
    return 0;
}

UINT32 UsbHostKeyboardStatusLogAt(UINT32 Index) {
    (void)Index;
    return 0;
}

void UsbHostKeyboardStatusCodeClear(void) {
}

void UsbHostKeyboardLogFreeze(void) {
}

void UsbHostKeyboardStatusCodeSet(UINT32 Code) {
    (void)Code;
}

#endif
