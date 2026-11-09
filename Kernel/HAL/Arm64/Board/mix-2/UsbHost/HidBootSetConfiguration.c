/*
 * UsbHost/HidBootSetConfiguration.c — Clear_TT_Buffer（USB2 §11.24.2.3）
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

void UsbHostClearTransactionTranslator(UINT32 HubSlotId, UINT32 HubPort,
                                       UINT32 DeviceAddress) {
    /* Device_Address 可为 0（SET_ADDRESS 前 TT 态） */
    if (HubSlotId == 0) {
        return;
    }
    (void)UsbHostHubClearTransactionTranslatorBuffer(HubSlotId, HubPort, DeviceAddress, 0u,
                                                     1u);
    (void)UsbHostHubClearTransactionTranslatorBuffer(HubSlotId, HubPort, DeviceAddress, 0u,
                                                     0u);
}

#else

void UsbHostClearTransactionTranslator(UINT32 HubSlotId, UINT32 HubPort,
                                       UINT32 DeviceAddress) {
    (void)HubSlotId;
    (void)HubPort;
    (void)DeviceAddress;
}

#endif
