/*
 * UsbHost/EmbeddedUsbRepeater.c — Pixel eUSB repeater @0x3e（同 10cb HSI2C）
 *
 * 证据：dmesg `eusb-repeater 11-003e: Enter Disabled mode, reg = 40`；
 * sepolicy `10cb0000.hsi2c/i2c-11/11-0025` → 与 TCPC 同控制器。
 * TI TUSB2E221：REV_ID@0xB0=0x03；GLOBAL_CONFIG@0xB2 bit6=DISABLE_P1 → Android
 * `reg=40`；清 DISABLE 后仍 DEFAULT，须 SoC eUSB 发 Host Mode Enable（CONFIG_PORT
 * HOST_DEVICE≠0）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define EMBEDDED_USB_REPEATER_DEVICE_ADDRESS 0x3Eu
#define EMBEDDED_USB_REPEATER_CONFIGURATION_PORT0_REGISTER 0x20u
#define EMBEDDED_USB_REPEATER_REVISION_REGISTER 0xB0u
#define EMBEDDED_USB_REPEATER_GLOBAL_CONFIGURATION_REGISTER 0xB2u
#define EMBEDDED_USB_REPEATER_DISABLE_PORT0 (1u << 5)
#define EMBEDDED_USB_REPEATER_DISABLE_PORT1 (1u << 6)

static UINT8 sRepeaterRevision;
static UINT8 sRepeaterGlobalConfiguration;
static UINT8 sRepeaterConfigurationPort0;
static UINT8 sRepeaterLastResultCode;

UINT8 UsbHostRepeaterLastResultCode(void) {
    return sRepeaterLastResultCode;
}

UINT8 UsbHostRepeaterRevisionByte(void) {
    return sRepeaterRevision;
}

UINT8 UsbHostRepeaterGlobalConfigurationByte(void) {
    return sRepeaterGlobalConfiguration;
}

UINT8 UsbHostRepeaterPort0ConfigurationByte(void) {
    return sRepeaterConfigurationPort0;
}

void UsbHostRepeaterRefreshStatusBytes(void) {
    (void)UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
        EMBEDDED_USB_REPEATER_DEVICE_ADDRESS,
        EMBEDDED_USB_REPEATER_GLOBAL_CONFIGURATION_REGISTER,
        &sRepeaterGlobalConfiguration, 1u);
    (void)UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
        EMBEDDED_USB_REPEATER_DEVICE_ADDRESS,
        EMBEDDED_USB_REPEATER_CONFIGURATION_PORT0_REGISTER,
        &sRepeaterConfigurationPort0, 1u);
}

static int WriteRepeaterRegister(UINT8 RegisterAddress, UINT8 Value) {
    UINT8 Bytes[2];

    Bytes[0] = RegisterAddress;
    Bytes[1] = Value;
    return UsbHostHighSpeedInterIntegratedCircuitWrite(
        EMBEDDED_USB_REPEATER_DEVICE_ADDRESS, Bytes, 2u);
}

static int ReadRepeaterRegister(UINT8 RegisterAddress, UINT8 *Value) {
    return UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
        EMBEDDED_USB_REPEATER_DEVICE_ADDRESS, RegisterAddress, Value, 1u);
}

static void PaintRepeaterStatusLine(void) {
    char Line[48];
    char Hex[12];

    UsbHostCopyText(Line, "eu", sizeof(Line));
    UsbHostFormatHexadecimal32(Hex, sRepeaterLastResultCode);
    UsbHostAppendText(Line, Hex + 8, sizeof(Line));
    UsbHostAppendText(Line, " id", sizeof(Line));
    UsbHostFormatHexadecimal32(Hex, sRepeaterRevision);
    UsbHostAppendText(Line, Hex + 8, sizeof(Line));
    UsbHostAppendText(Line, " g", sizeof(Line));
    UsbHostFormatHexadecimal32(Hex, sRepeaterGlobalConfiguration);
    UsbHostAppendText(Line, Hex + 8, sizeof(Line));
    UsbHostAppendText(Line, " c", sizeof(Line));
    UsbHostFormatHexadecimal32(Hex, sRepeaterConfigurationPort0);
    UsbHostAppendText(Line, Hex + 8, sizeof(Line));
    /* 最终 HubStatusPaint 下半屏重画；此处只串口 */
    HalSerialWriteShell(Line);
    HalSerialWriteShell("\r\n");
}

int UsbHostRepeaterEnableHost(void) {
    UINT8 Revision = 0;
    UINT8 GlobalConfiguration = 0;
    int Result;

    sRepeaterLastResultCode = 0;
    sRepeaterRevision = 0;
    sRepeaterGlobalConfiguration = 0;
    sRepeaterConfigurationPort0 = 0;

    HalSerialWriteShell("eu: probe\r\n");

    Result = ReadRepeaterRegister(EMBEDDED_USB_REPEATER_REVISION_REGISTER,
                                  &Revision);
    if (Result != 0 || Revision == 0u || Revision == 0xFFu) {
        sRepeaterLastResultCode =
            (Result != 0) ? (UINT8)(0x10u + (UINT8)(-Result)) : 0x13u;
        sRepeaterRevision = Revision;
        PaintRepeaterStatusLine();
        return -1;
    }
    sRepeaterRevision = Revision;

    Result = ReadRepeaterRegister(
        EMBEDDED_USB_REPEATER_GLOBAL_CONFIGURATION_REGISTER,
        &GlobalConfiguration);
    if (Result != 0) {
        sRepeaterLastResultCode = (UINT8)(0x20u + (UINT8)(-Result));
        PaintRepeaterStatusLine();
        return -1;
    }

    GlobalConfiguration &=
        (UINT8) ~(EMBEDDED_USB_REPEATER_DISABLE_PORT0 |
                  EMBEDDED_USB_REPEATER_DISABLE_PORT1);
    if (WriteRepeaterRegister(
            EMBEDDED_USB_REPEATER_GLOBAL_CONFIGURATION_REGISTER,
            GlobalConfiguration) != 0) {
        sRepeaterLastResultCode = 0x31u;
        PaintRepeaterStatusLine();
        return -1;
    }
    UsbHostStallYield(20000u);

    UsbHostRepeaterRefreshStatusBytes();
    sRepeaterLastResultCode = 1u;
    PaintRepeaterStatusLine();
    return 0;
}

#else

void UsbHostRepeaterRefreshStatusBytes(void) {
}

UINT8 UsbHostRepeaterLastResultCode(void) {
    return 0;
}

UINT8 UsbHostRepeaterRevisionByte(void) {
    return 0;
}

UINT8 UsbHostRepeaterGlobalConfigurationByte(void) {
    return 0;
}

UINT8 UsbHostRepeaterPort0ConfigurationByte(void) {
    return 0;
}

int UsbHostRepeaterEnableHost(void) {
    return -1;
}

#endif
