/*
 * UsbHost/HubFinishSetup.c — hub SET_CONFIG / HubDesc / Evaluate Hub Slot
 *
 * 对表 X64 FinishHubSetup + EvaluateHubSlot（含 USB2 TTT）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

static void WriteLittleEndian32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

static int HubControl(UINT32 HubSlotId, UINT8 RequestType, UINT8 Request, UINT16 Value,
                      UINT16 Index, void *Data, UINT16 Length) {
    return UsbHostXhciControlTransfer(HubSlotId, RequestType, Request, Value, Index, Data,
                                      Length);
}

static int EvaluateHubSlotContext(UINT32 HubSlotId, UINT32 RootPort, UINT32 PortSpeed,
                                  UINT32 HubPortCount, UINT32 ThinkTime,
                                  UINT32 MultiTransactionTranslator) {
    UINT8 *Input;
    UINT8 *Output;
    UINT32 ContextSize;
    UINTN Index;
    UINT32 SlotWord0;
    UINT32 SlotWord1;
    UINT32 SlotWord2;

    ContextSize = UsbHostContextSizeBytes();
    Input = UsbHostInputContextBuffer();
    Output = UsbHostDeviceContextSlot(HubSlotId);
    for (Index = 0; Index < ContextSize * 33u; Index++) {
        Input[Index] = 0;
    }
    WriteLittleEndian32(Input + 4u, (1u << 0));
    for (Index = 0; Index < ContextSize; Index++) {
        Input[ContextSize + Index] = Output[Index];
    }
    /*
     * 对表 X64 EvaluateHubSlot / xHCI §6.2.2：拷 Output 后 OR Hub；
     * TTT 在 DWORD2 bits16..17（勿写入 DWORD0 Route）。
     */
    SlotWord0 = (UINT32)Output[0] | ((UINT32)Output[1] << 8) | ((UINT32)Output[2] << 16) |
                ((UINT32)Output[3] << 24);
    if (((SlotWord0 >> 27) & 0x1Fu) < 1u) {
        SlotWord0 = (SlotWord0 & ~(0x1Fu << 27)) | (1u << 27);
    }
    SlotWord0 |= (1u << 26);
    if (MultiTransactionTranslator != 0 && PortSpeed < 4u) {
        SlotWord0 |= (1u << 25);
    } else {
        SlotWord0 &= ~(1u << 25);
    }
    if (PortSpeed != 0) {
        SlotWord0 = (SlotWord0 & ~(0xFu << 20)) | ((PortSpeed & 0xFu) << 20);
    }
    SlotWord1 = ((RootPort & 0xFFu) << 16) | ((HubPortCount & 0xFFu) << 24);
    SlotWord2 = (UINT32)Output[8] | ((UINT32)Output[9] << 8) |
                ((UINT32)Output[10] << 16) | ((UINT32)Output[11] << 24);
    if (PortSpeed < 4u) {
        SlotWord2 = (SlotWord2 & ~(3u << 16)) | ((ThinkTime & 3u) << 16);
    } else {
        SlotWord2 &= ~(3u << 16);
    }
    WriteLittleEndian32(Input + ContextSize + 0u, SlotWord0);
    WriteLittleEndian32(Input + ContextSize + 4u, SlotWord1);
    WriteLittleEndian32(Input + ContextSize + 8u, SlotWord2);
    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);
    return UsbHostXhciConfigureEndpoint(HubSlotId);
}

int UsbHostFinishHubSetup(UINT32 HubSlotId, UINT32 RootPortIndex, UINT32 PortSpeed,
                          UINT32 *PortCountOut) {
    UINT8 *ControlData = UsbHostControlDataBuffer();
    UINT8 HubDescriptor[16];
    UINT32 HubPortCount = 4u;
    UINT32 ThinkTime = 0;
    UINT32 MultiTransactionTranslator = 0;
    UINT8 ConfigurationValue = 1u;
    UINTN Index;

    if (HubControl(HubSlotId, 0x80u, 6u, 0x0100u, 0, ControlData, 18u) == 0 &&
        ControlData[4] == USB_DEVICE_CLASS_HUB && ControlData[7] == 2u &&
        PortSpeed == 3u) {
        MultiTransactionTranslator = 1u;
    }
    if (HubControl(HubSlotId, 0x80u, 6u, 0x0200u, 0, ControlData, 9u) != 0) {
        return -1;
    }
    ConfigurationValue = ControlData[5] != 0 ? ControlData[5] : 1u;
    if (HubControl(HubSlotId, 0x00u, 9u, ConfigurationValue, 0, 0, 0) != 0) {
        return -1;
    }

    for (Index = 0; Index < sizeof(HubDescriptor); Index++) {
        HubDescriptor[Index] = 0;
    }
    if (HubControl(HubSlotId, 0xA0u, 6u, 0x2900u, 0, HubDescriptor,
                   (UINT16)sizeof(HubDescriptor)) == 0 &&
        HubDescriptor[2] != 0) {
        HubPortCount = HubDescriptor[2];
        if (HubPortCount > 15u) {
            HubPortCount = 15u;
        }
        ThinkTime = (HubDescriptor[3] >> 5) & 3u;
    }

    if (EvaluateHubSlotContext(HubSlotId, RootPortIndex, PortSpeed, HubPortCount,
                               ThinkTime, MultiTransactionTranslator) != 0) {
        return -1;
    }
    if (PortCountOut != 0) {
        *PortCountOut = HubPortCount;
    }
    return 0;
}

int UsbHostHubGetPortStatus(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT32 *StatusOut) {
    UINT8 Buffer[4];

    if (HubControl(HubSlotId, 0xA3u, 0u, 0, (UINT16)HubPortOneBased, Buffer, 4u) != 0) {
        return -1;
    }
    if (StatusOut != 0) {
        *StatusOut = (UINT32)Buffer[0] | ((UINT32)Buffer[1] << 8) |
                     ((UINT32)Buffer[2] << 16) | ((UINT32)Buffer[3] << 24);
    }
    return 0;
}

int UsbHostHubSetPortFeature(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT16 Feature) {
    return HubControl(HubSlotId, 0x23u, 3u, Feature, (UINT16)HubPortOneBased, 0, 0);
}

int UsbHostHubClearPortFeature(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT16 Feature) {
    return HubControl(HubSlotId, 0x23u, 1u, Feature, (UINT16)HubPortOneBased, 0, 0);
}

#else

int UsbHostFinishHubSetup(UINT32 HubSlotId, UINT32 RootPortIndex, UINT32 PortSpeed,
                          UINT32 *PortCountOut) {
    (void)HubSlotId;
    (void)RootPortIndex;
    (void)PortSpeed;
    (void)PortCountOut;
    return -1;
}

int UsbHostHubGetPortStatus(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT32 *StatusOut) {
    (void)HubSlotId;
    (void)HubPortOneBased;
    (void)StatusOut;
    return -1;
}

int UsbHostHubSetPortFeature(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT16 Feature) {
    (void)HubSlotId;
    (void)HubPortOneBased;
    (void)Feature;
    return -1;
}

int UsbHostHubClearPortFeature(UINT32 HubSlotId, UINT32 HubPortOneBased, UINT16 Feature) {
    (void)HubSlotId;
    (void)HubPortOneBased;
    (void)Feature;
    return -1;
}

#endif
