/*
 * UsbHost/HidBootEndpointParse.c — Boot/Report 键中断 IN（含 fallback）
 *
 * Boot Proto=1 优先；否则 Proto=0 HID；再否则首个中断 IN（MIX2 k42 教训）。
 * 再否则：无 Interface 上下文时仍收第一个中断 IN（坞复合描述符畸形）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

int UsbHostParseBootKeyboardEndpoint(const UINT8 *Configuration, UINT16 Total,
                                     UINT8 *InterfaceOut, UINT8 *EndpointAddressOut,
                                     UINT16 *MaxPacketSizeOut, UINT8 *IntervalOut) {
    UINT16 Offset = 0;
    UINT8 CurrentInterface = 0xFFu;
    UINT8 CurrentClass = 0xFFu;
    UINT8 CurrentSubclass = 0xFFu;
    UINT8 CurrentProtocol = 0xFFu;
    int FoundReport = 0;
    UINT8 ReportInterface = 0;
    UINT8 ReportEndpoint = 0;
    UINT16 ReportMaxPacketSize = 8;
    UINT8 ReportInterval = 10;
    int FoundFallback = 0;
    UINT8 FallbackInterface = 0;
    UINT8 FallbackEndpoint = 0;
    UINT16 FallbackMaxPacketSize = 8;
    UINT8 FallbackInterval = 10;
    int FoundAnyInterruptIn = 0;
    UINT8 AnyInterface = 0;
    UINT8 AnyEndpoint = 0;
    UINT16 AnyMaxPacketSize = 8;
    UINT8 AnyInterval = 10;

    while (Offset + 2u <= Total) {
        UINT8 Length = Configuration[Offset];
        UINT8 Type = Configuration[Offset + 1u];

        if (Length < 2u || Offset + Length > Total) {
            break;
        }
        if (Type == 4u && Length >= 9u) {
            CurrentInterface = Configuration[Offset + 2u];
            CurrentClass = Configuration[Offset + 5u];
            CurrentSubclass = Configuration[Offset + 6u];
            CurrentProtocol = Configuration[Offset + 7u];
        }
        if (Type == 5u && Length >= 7u && (Configuration[Offset + 2u] & 0x80u) != 0 &&
            (Configuration[Offset + 3u] & 0x03u) == 0x03u) {
            int HidLike =
                (CurrentClass == USB_DEVICE_CLASS_HID || CurrentClass == 0xFFu) &&
                (CurrentSubclass == USB_HID_BOOT_SUBCLASS || CurrentSubclass == 0);
            int BootKeyboard =
                HidLike != 0 &&
                (CurrentProtocol == USB_INTERFACE_PROTOCOL_BOOT_KEYBOARD);
            int ReportKeyboard = HidLike != 0 && (CurrentProtocol == 0);
            UINT8 Endpoint = Configuration[Offset + 2u];
            UINT16 PacketSize =
                (UINT16)(Configuration[Offset + 4u] |
                         ((UINT16)Configuration[Offset + 5u] << 8));
            UINT8 Interval = Configuration[Offset + 6u];

            if (BootKeyboard != 0) {
                if (InterfaceOut != 0) {
                    *InterfaceOut = CurrentInterface != 0xFFu ? CurrentInterface : 0;
                }
                if (EndpointAddressOut != 0) {
                    *EndpointAddressOut = Endpoint;
                }
                if (MaxPacketSizeOut != 0) {
                    *MaxPacketSizeOut = PacketSize;
                }
                if (IntervalOut != 0) {
                    *IntervalOut = Interval;
                }
                return 0;
            }
            if (FoundReport == 0 && ReportKeyboard != 0 && CurrentInterface != 0xFFu) {
                FoundReport = 1;
                ReportInterface = CurrentInterface;
                ReportEndpoint = Endpoint;
                ReportMaxPacketSize = PacketSize;
                ReportInterval = Interval;
            }
            if (FoundFallback == 0 && CurrentInterface != 0xFFu) {
                FoundFallback = 1;
                FallbackInterface = CurrentInterface;
                FallbackEndpoint = Endpoint;
                FallbackMaxPacketSize = PacketSize;
                FallbackInterval = Interval;
            }
            if (FoundAnyInterruptIn == 0) {
                FoundAnyInterruptIn = 1;
                AnyInterface = CurrentInterface != 0xFFu ? CurrentInterface : 0;
                AnyEndpoint = Endpoint;
                AnyMaxPacketSize = PacketSize;
                AnyInterval = Interval;
            }
        }
        Offset = (UINT16)(Offset + Length);
    }
    if (FoundReport != 0) {
        if (InterfaceOut != 0) {
            *InterfaceOut = ReportInterface;
        }
        if (EndpointAddressOut != 0) {
            *EndpointAddressOut = ReportEndpoint;
        }
        if (MaxPacketSizeOut != 0) {
            *MaxPacketSizeOut = ReportMaxPacketSize;
        }
        if (IntervalOut != 0) {
            *IntervalOut = ReportInterval;
        }
        return 0;
    }
    if (FoundFallback != 0) {
        if (InterfaceOut != 0) {
            *InterfaceOut = FallbackInterface;
        }
        if (EndpointAddressOut != 0) {
            *EndpointAddressOut = FallbackEndpoint;
        }
        if (MaxPacketSizeOut != 0) {
            *MaxPacketSizeOut = FallbackMaxPacketSize;
        }
        if (IntervalOut != 0) {
            *IntervalOut = FallbackInterval;
        }
        return 0;
    }
    if (FoundAnyInterruptIn != 0) {
        if (InterfaceOut != 0) {
            *InterfaceOut = AnyInterface;
        }
        if (EndpointAddressOut != 0) {
            *EndpointAddressOut = AnyEndpoint;
        }
        if (MaxPacketSizeOut != 0) {
            *MaxPacketSizeOut = AnyMaxPacketSize;
        }
        if (IntervalOut != 0) {
            *IntervalOut = AnyInterval;
        }
        return 0;
    }
    return -1;
}

#else

int UsbHostParseBootKeyboardEndpoint(const UINT8 *Configuration, UINT16 Total,
                                     UINT8 *InterfaceOut, UINT8 *EndpointAddressOut,
                                     UINT16 *MaxPacketSizeOut, UINT8 *IntervalOut) {
    (void)Configuration;
    (void)Total;
    (void)InterfaceOut;
    (void)EndpointAddressOut;
    (void)MaxPacketSizeOut;
    (void)IntervalOut;
    return -1;
}

#endif
