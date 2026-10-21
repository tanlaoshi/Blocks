/*
 * Request.c — 标准 Hub 类请求。字节与原先两处 Hub.c 相同。
 */
#include "Hub/Private.h"

void PortStatusSetup(USB_SETUP *Setup, UINT16 Port) {
    Setup->bmRequestType = 0xA3;
    Setup->bRequest = 0x00;
    Setup->wValue = 0;
    Setup->wIndex = Port;
    Setup->wLength = 4;
}

void PortFeatureSetup(USB_SETUP *Setup, UINT16 Port, UINT16 Feature, int Set) {
    Setup->bmRequestType = 0x23;
    Setup->bRequest = Set ? 0x03 : 0x01;
    Setup->wValue = Feature;
    Setup->wIndex = Port;
    Setup->wLength = 0;
}

void DescriptorSetup(USB_SETUP *Setup, UINT16 Length) {
    Setup->bmRequestType = 0xA0;
    Setup->bRequest = 0x06;
    Setup->wValue = 0x2900; /* Hub class descriptor */
    Setup->wIndex = 0;
    Setup->wLength = Length;
}

void HubDepthSetup(USB_SETUP *Setup, UINT16 Depth) {
    Setup->bmRequestType = 0x20; /* OUT | Class | Device */
    Setup->bRequest = 12;        /* SET_HUB_DEPTH USB3 §10.14.2.8 */
    Setup->wValue = Depth;
    Setup->wIndex = 0;
    Setup->wLength = 0;
}
