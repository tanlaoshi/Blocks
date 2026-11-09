/*
 * UsbHost/HidBootKeyboardLeds.c — Boot 锁灯 SET_REPORT（对表 X64/Duo）
 *
 * HID 1.11 §8.3：bit0 Num / bit1 Caps / bit2 Scroll。
 * 真机：Configure 末或首中断立刻 SET_REPORT → 2711。只在锁键翻转时发。
 * 失败 ClearTT 再试一次；仍败 sticky 4Ecc，不拆 a2。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define HID_SET_REPORT_OUTPUT_VALUE 0x0200u

static UINT32 sKeyboardSlotId;
static UINT32 sHubSlotId;
static UINT32 sHubPortOneBased;
static UINT8 sKeyboardInterface;
static UINT8 sKeyboardLedsBound;

void UsbHostHidBootKeyboardLedsBind(UINT32 KeyboardSlotId, UINT8 Interface,
                                    UINT32 HubSlotId, UINT32 HubPortOneBased) {
    sKeyboardSlotId = KeyboardSlotId;
    sKeyboardInterface = Interface;
    sHubSlotId = HubSlotId;
    sHubPortOneBased = HubPortOneBased;
    sKeyboardLedsBound = (KeyboardSlotId != 0) ? 1u : 0;
}

void UsbHostHidBootKeyboardLedsPush(void) {
    UINT8 LedByte;
    UINT8 *Output;
    UINT32 DeviceAddress;
    UINTN Try;

    if (sKeyboardLedsBound == 0 || sKeyboardSlotId == 0) {
        return;
    }
    LedByte = UsbHostHidBootKeyboardLedOutputByte();
    for (Try = 0; Try < 2u; Try++) {
        if (Try != 0 && sHubSlotId != 0) {
            Output = UsbHostDeviceContextSlot(sKeyboardSlotId);
            UsbHostCacheCleanInvalidateRange(Output, UsbHostContextSizeBytes() * 32u);
            DeviceAddress = (UINT32)Output[12] & 0xFFu;
            UsbHostClearTransactionTranslator(sHubSlotId, sHubPortOneBased, DeviceAddress);
            if (DeviceAddress != 0u) {
                UsbHostClearTransactionTranslator(sHubSlotId, sHubPortOneBased, 0u);
            }
        }
        if (UsbHostXhciControlTransfer(sKeyboardSlotId, 0x21u, 0x09u,
                                       HID_SET_REPORT_OUTPUT_VALUE, sKeyboardInterface,
                                       &LedByte, 1u) == 0) {
            return;
        }
    }
    UsbHostKeyboardStatusCodeSet(0x4E00u |
                                 (UsbHostXhciControlLastCompletionCode() & 0xFFu));
}

#else

void UsbHostHidBootKeyboardLedsBind(UINT32 KeyboardSlotId, UINT8 Interface,
                                    UINT32 HubSlotId, UINT32 HubPortOneBased) {
    (void)KeyboardSlotId;
    (void)Interface;
    (void)HubSlotId;
    (void)HubPortOneBased;
}

void UsbHostHidBootKeyboardLedsPush(void) {
}

#endif
