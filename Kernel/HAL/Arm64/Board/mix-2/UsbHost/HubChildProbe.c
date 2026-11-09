/*
 * UsbHost/HubChildProbe.c — 已寻址槽：GetDesc / SetConfig / Boot 键配置
 *
 * 返回：0=键已配；1=子设备是 hub（槽已寻址，待 FinishSetup）；-1=失败。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

static UINT32 sProbeStickyTier;

void UsbHostProbeTierSet(UINT32 Tier) {
    sProbeStickyTier = Tier;
}

static void WriteLittleEndian32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

static int EvaluateEndpointZeroRing(UINT32 SlotId, UINT32 MaxPacketSize) {
    TransferRequestBlock Command;
    UINT32 CompletionCode = 0;
    UINT8 *Input;
    UINT8 *Output;
    UINT32 ContextSize;
    UINTN Index;
    UINT64 Dequeue;

    if (MaxPacketSize < 8u) {
        MaxPacketSize = 8u;
    }
    if (MaxPacketSize > 64u) {
        MaxPacketSize = 64u;
    }
    UsbHostBindEndpointZeroRing(SlotId);
    Dequeue = UsbHostEndpointZeroRingPhysical(SlotId) | 1ULL;
    ContextSize = UsbHostContextSizeBytes();
    Input = UsbHostInputContextBuffer();
    Output = UsbHostDeviceContextSlot(SlotId);
    for (Index = 0; Index < ContextSize * 33u; Index++) {
        Input[Index] = 0;
    }
    WriteLittleEndian32(Input + 4u, (1u << 1));
    WriteLittleEndian32(Input + ContextSize * 2u + 4u,
                        (3u << 1) | (4u << 3) | (MaxPacketSize << 16));
    WriteLittleEndian32(Input + ContextSize * 2u + 8u, (UINT32)Dequeue);
    WriteLittleEndian32(Input + ContextSize * 2u + 12u, (UINT32)(Dequeue >> 32));
    WriteLittleEndian32(Input + ContextSize * 2u + 16u, 8u);
    UsbHostCacheCleanRange(Input, ContextSize * 33u);
    UsbHostCacheCleanRange(Output, ContextSize * 32u);
    Command.Parameter = (UINT64)(UINTN)Input;
    Command.Status = 0;
    Command.Control =
        TRANSFER_REQUEST_BLOCK_TYPE(TRANSFER_REQUEST_BLOCK_EVALUATE_CONTEXT) |
        TRANSFER_REQUEST_BLOCK_INTERRUPT_ON_COMPLETION |
        (SlotId << TRANSFER_REQUEST_BLOCK_SLOT_SHIFT);
    if (UsbHostXhciSubmitCommand(&Command, &CompletionCode, 0) != 0 ||
        CompletionCode != COMPLETION_CODE_SUCCESS) {
        return -1;
    }
    return 0;
}

static UINT32 ProbeFailSticky(UINT32 Kind, UINT32 HubPort, UINT32 DeviceSpeed) {
    /* 二层：Kind+0x10 → 34xxxx，避免与一层 24xxxx 去重撞车 */
    UINT32 KindField = Kind + ((sProbeStickyTier != 0) ? 0x10u : 0);
    return (KindField << 16) | ((DeviceSpeed & 0xFu) << 12) |
           ((UsbHostXhciControlLastCompletionCode() & 0xFFu) << 4) | (HubPort & 0xFu);
}

int UsbHostProbeChildSlot(UINT32 KeyboardSlotId, UINT32 RootPort, UINT32 HubSlotId,
                             UINT32 HubPort, UINT32 DeviceSpeed) {
    UINT8 *ControlData = UsbHostControlDataBuffer();
    UINT16 Total = 0;
    UINT8 MaxPacketSize0;
    /* 与 Address / HubEnumerate 初值一致：HS/FS=64，LS=8 */
    UINT32 AddressMps = (DeviceSpeed == 3u || DeviceSpeed == 1u) ? 64u : 8u;

    /*
     * Address 已一步 BSR0。坞内二级：Clear_TT_Buffer（USB2 §11.24.2.3；真机 37a4 后仍
     * 直取 18 字节→341044）。对表 X64 DeviceControl GetDeviceDesc：先 8 拿
     * bMaxPacketSize0 → Evaluate → 再 18。
     */
    if (sProbeStickyTier != 0) {
        UINT8 *Output = UsbHostDeviceContextSlot(KeyboardSlotId);
        UINT32 DeviceAddress;

        UsbHostCacheCleanInvalidateRange(Output, UsbHostContextSizeBytes() * 32u);
        DeviceAddress = (UINT32)Output[12] & 0xFFu;
        UsbHostClearTransactionTranslator(HubSlotId, HubPort, DeviceAddress);
        if (DeviceAddress != 0u) {
            UsbHostClearTransactionTranslator(HubSlotId, HubPort, 0u);
        }
        UsbHostKeyboardStatusCodeSet(0x37A0u | (HubPort & 0xFu));
    }
    UsbHostStallMilliseconds(50u);
    UsbHostKeyboardStatusCodeSet(
        ((sProbeStickyTier != 0) ? 0x3770u : 0x2770u) | (HubPort & 0xFu));
    if (UsbHostXhciControlTransfer(KeyboardSlotId, 0x80u, 6u, 0x0100u, 0, ControlData,
                                   8u) != 0) {
        UsbHostKeyboardStatusCodeSet(ProbeFailSticky(0x24u, HubPort, DeviceSpeed));
        return -1;
    }
    UsbHostKeyboardStatusCodeSet(
        ((sProbeStickyTier != 0) ? 0x3780u : 0x2780u) | (HubPort & 0xFu));
    MaxPacketSize0 = ControlData[7];
    if (MaxPacketSize0 != 8u && MaxPacketSize0 != 16u && MaxPacketSize0 != 32u &&
        MaxPacketSize0 != 64u) {
        MaxPacketSize0 = (UINT8)AddressMps;
    }
    if (MaxPacketSize0 != AddressMps) {
        if (EvaluateEndpointZeroRing(KeyboardSlotId, MaxPacketSize0) != 0) {
            UsbHostKeyboardStatusCodeSet(ProbeFailSticky(0x27u, HubPort, DeviceSpeed));
            return -1;
        }
        AddressMps = MaxPacketSize0;
    }
    if (UsbHostXhciControlTransfer(KeyboardSlotId, 0x80u, 6u, 0x0100u, 0, ControlData,
                                   18u) != 0) {
        UsbHostKeyboardStatusCodeSet(ProbeFailSticky(0x28u, HubPort, DeviceSpeed));
        return -1;
    }
    UsbHostKeyboardStatusCodeSet(
        ((sProbeStickyTier != 0) ? 0x37D0u : 0x27D0u) | (HubPort & 0xFu));
    if (ControlData[4] == USB_DEVICE_CLASS_HUB) {
        UsbHostKeyboardStatusCodeSet(0x260u | (HubPort & 0xFu));
        return 1;
    }
    {
        UINT8 NumberOfConfigurations = ControlData[17];
        UINT8 ConfigurationIndex;
        UINT8 DummyInterface = 0;
        UINT8 DummyEndpoint = 0;
        UINT16 DummyMaxPacketSize = 0;
        UINT8 DummyInterval = 0;

        if (NumberOfConfigurations == 0) {
            NumberOfConfigurations = 1u;
        }
        if (NumberOfConfigurations > 4u) {
            NumberOfConfigurations = 4u;
        }
        UsbHostKeyboardStatusCodeSet(0x2C00u | NumberOfConfigurations);
        if (MaxPacketSize0 != AddressMps) {
            if (EvaluateEndpointZeroRing(KeyboardSlotId, MaxPacketSize0) != 0) {
                UsbHostKeyboardStatusCodeSet(ProbeFailSticky(0x27u, HubPort, DeviceSpeed));
                return -1;
            }
        }
        UsbHostStallMilliseconds(10u);
        for (ConfigurationIndex = 0; ConfigurationIndex < NumberOfConfigurations;
             ConfigurationIndex++) {
            UINT16 Value = (UINT16)(0x0200u | ConfigurationIndex);

            UsbHostKeyboardStatusCodeSet(
                ((sProbeStickyTier != 0) ? 0x3790u : 0x2790u) | (HubPort & 0xFu));
            if (UsbHostXhciControlTransfer(KeyboardSlotId, 0x80u, 6u, Value, 0,
                                           ControlData, 9u) != 0) {
                UsbHostKeyboardStatusCodeSet(
                    ProbeFailSticky(0x25u, HubPort, DeviceSpeed));
                continue;
            }
            Total = (UINT16)(ControlData[2] | ((UINT16)ControlData[3] << 8));
            if (Total < 9u) {
                Total = 9u;
            }
            if (Total > USB_CONTROL_DATA_CAPACITY) {
                Total = (UINT16)USB_CONTROL_DATA_CAPACITY;
            }
            if (UsbHostXhciControlTransfer(KeyboardSlotId, 0x80u, 6u, Value, 0,
                                           ControlData, Total) != 0) {
                UsbHostKeyboardStatusCodeSet(
                    ((sProbeStickyTier != 0) ? 0x35A0u : 0x25A0u) | (HubPort & 0xFu));
                continue;
            }
            /* 诊断：wTotalLength + bNumInterfaces（42＝无中断 IN） */
            UsbHostKeyboardStatusCodeSet(0x2E0000u | (Total & 0xFFFFu));
            UsbHostKeyboardStatusCodeSet(0x2F00u | ControlData[4]);
            if (UsbHostParseBootKeyboardEndpoint(ControlData, Total, &DummyInterface,
                                                 &DummyEndpoint, &DummyMaxPacketSize,
                                                 &DummyInterval) != 0) {
                /* 头报 Total 过短/截断时再取满缓冲 */
                if (Total < USB_CONTROL_DATA_CAPACITY) {
                    if (UsbHostXhciControlTransfer(KeyboardSlotId, 0x80u, 6u, Value, 0,
                                                   ControlData,
                                                   (UINT16)USB_CONTROL_DATA_CAPACITY) ==
                        0) {
                        Total = (UINT16)USB_CONTROL_DATA_CAPACITY;
                        UsbHostKeyboardStatusCodeSet(0x2E0000u | Total);
                        UsbHostKeyboardStatusCodeSet(0x2F00u | ControlData[4]);
                        if (UsbHostParseBootKeyboardEndpoint(
                                ControlData, Total, &DummyInterface, &DummyEndpoint,
                                &DummyMaxPacketSize, &DummyInterval) == 0) {
                            goto ProbeConfigureKeyboard;
                        }
                    }
                }
                /* 接口 class（config+iface 至少 18 字节） */
                if (Total >= 18u) {
                    UsbHostKeyboardStatusCodeSet(0x4A00u | ControlData[14]);
                }
                continue;
            }
        ProbeConfigureKeyboard:
            UsbHostKeyboardStatusCodeSet(0x2B00u | ConfigurationIndex);
            if (sProbeStickyTier != 0) {
                UsbHostKeyboardStatusCodeSet(0x37C0u | (HubPort & 0xFu));
            }
            return UsbHostHidBootKeyboardConfigure(KeyboardSlotId, RootPort, HubSlotId,
                                                   HubPort, DeviceSpeed);
        }
        if (Total >= 18u) {
            UsbHostKeyboardStatusCodeSet(0x4A00u | ControlData[14]);
        }
        UsbHostKeyboardStatusCodeSet(0x42u);
        return -1;
    }
}

#else

void UsbHostProbeTierSet(UINT32 Tier) {
    (void)Tier;
}

int UsbHostProbeChildSlot(UINT32 KeyboardSlotId, UINT32 RootPort, UINT32 HubSlotId,
                             UINT32 HubPort, UINT32 DeviceSpeed) {
    (void)KeyboardSlotId;
    (void)RootPort;
    (void)HubSlotId;
    (void)HubPort;
    (void)DeviceSpeed;
    return -1;
}

#endif
