/*
 * UsbHost/EmbeddedUsbRepeater.c — Pixel eUSB repeater @0x3e（同 10cb HSI2C）
 *
 * 证据：dmesg `eusb-repeater 11-003e: Enter Disabled mode, reg = 40`；
 * sepolicy `10cb0000.hsi2c/i2c-11/11-0025` → 与 TCPC 同控制器。
 * TI TUSB2E221：REV_ID@0xB0；GLOBAL_CONFIG@0xB2 bit6=DISABLE_P1 → Android
 * `reg=40`。对表 eusb_repeater_ctrl：只清 DISABLE_P1。
 * 真机 `c8`+`pd3`+`ls0`：Host 下拉已到 dig，线仍 SE0 → 疑 GPIO1 进
 * Carkit UART（datasheet 8.4.3）。LOPWR@0x50（Table 9-14）：
 *   bit1=0 选寄存器、bit0=0 关 UART（禁误置 bit1——1＝走 GPIO1）；
 *   清 bit6 HOST_FRAME_LP / bit4 IDLE_LP（默认 OTP 开；μSOF 低功耗可致
 *   PED/chirp 绿而 HS SET_ADDRESS USBTE，§8.4.10）。
 * 验收 `c`＝CONFIG_PORT1@0x60（Android debugfs 盯 P1；勿只盯 PORT0@0x20）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define EMBEDDED_USB_REPEATER_DEVICE_ADDRESS 0x3Eu
#define EMBEDDED_USB_REPEATER_LOW_POWER_UART_PORT1_REGISTER 0x50u
#define EMBEDDED_USB_REPEATER_CONFIGURATION_PORT0_REGISTER 0x20u
#define EMBEDDED_USB_REPEATER_CONFIGURATION_PORT1_REGISTER 0x60u
#define EMBEDDED_USB_REPEATER_REVISION_REGISTER 0xB0u
#define EMBEDDED_USB_REPEATER_GLOBAL_CONFIGURATION_REGISTER 0xB2u
#define EMBEDDED_USB_REPEATER_DISABLE_PORT1 (1u << 6)
#define EMBEDDED_USB_REPEATER_UART_MODE_ENABLE (1u << 0)
/* Table 9-14 bit1：0＝寄存器选源；1＝GPIO1 选源（勿当「by reg」置 1） */
#define EMBEDDED_USB_REPEATER_UART_SELECT_GPIO_PIN (1u << 1)
#define EMBEDDED_USB_REPEATER_IDLE_LOW_POWER_ENABLE (1u << 4)
#define EMBEDDED_USB_REPEATER_HOST_FRAME_LOW_POWER_ENABLE (1u << 6)

static UINT8 sRepeaterRevision;
static UINT8 sRepeaterGlobalConfiguration;
static UINT8 sRepeaterConfigurationPort0;
static UINT8 sRepeaterConfigurationPort1;
static UINT8 sRepeaterLowPowerUartPort1;
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

UINT8 UsbHostRepeaterPort1ConfigurationByte(void) {
    return sRepeaterConfigurationPort1;
}

UINT8 UsbHostRepeaterLowPowerUartPort1Byte(void) {
    return sRepeaterLowPowerUartPort1;
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
    (void)UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
        EMBEDDED_USB_REPEATER_DEVICE_ADDRESS,
        EMBEDDED_USB_REPEATER_CONFIGURATION_PORT1_REGISTER,
        &sRepeaterConfigurationPort1, 1u);
    (void)UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
        EMBEDDED_USB_REPEATER_DEVICE_ADDRESS,
        EMBEDDED_USB_REPEATER_LOW_POWER_UART_PORT1_REGISTER,
        &sRepeaterLowPowerUartPort1, 1u);
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
    UsbHostFormatHexadecimal32(Hex, sRepeaterConfigurationPort1);
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
    sRepeaterConfigurationPort1 = 0;
    sRepeaterLowPowerUartPort1 = 0;

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

    /* 对表 eusb_repeater_ctrl(true)：只清 DISABLE_P1 */
    GlobalConfiguration &= (UINT8)~EMBEDDED_USB_REPEATER_DISABLE_PORT1;
    if (WriteRepeaterRegister(
            EMBEDDED_USB_REPEATER_GLOBAL_CONFIGURATION_REGISTER,
            GlobalConfiguration) != 0) {
        sRepeaterLastResultCode = 0x31u;
        PaintRepeaterStatusLine();
        return -1;
    }
    /* 对表 eusb_repeater_power_on：Exit Disabled 后 mdelay(3)（须真 µs） */
    {
        UINT64 Frequency;
        UINT64 Start;
        UINT64 Delta;

        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Frequency));
        if (Frequency == 0u) {
            UsbHostStallYield(300000u);
        } else {
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Start));
            Delta = (Frequency * 3000ull) / 1000000ull;
            for (;;) {
                UINT64 Now;
                __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Now));
                if ((Now - Start) >= Delta) {
                    break;
                }
            }
        }
    }

    /*
     * TI Table 9-14 / §8.4.3 / §8.4.10：
     * bit1=0+bit0=0 → 寄存器关 UART（勿 OR bit1；1 反而是 GPIO1）；
     * 清 HOST_FRAME_LP+IDLE_LP，避免帧内休眠吞 HS 包。
     */
    {
        UINT8 LowPowerUart = 0;
        if (ReadRepeaterRegister(
                EMBEDDED_USB_REPEATER_LOW_POWER_UART_PORT1_REGISTER,
                &LowPowerUart) == 0) {
            LowPowerUart &= (UINT8)~(EMBEDDED_USB_REPEATER_UART_SELECT_GPIO_PIN |
                                     EMBEDDED_USB_REPEATER_UART_MODE_ENABLE |
                                     EMBEDDED_USB_REPEATER_IDLE_LOW_POWER_ENABLE |
                                     EMBEDDED_USB_REPEATER_HOST_FRAME_LOW_POWER_ENABLE);
            if (WriteRepeaterRegister(
                    EMBEDDED_USB_REPEATER_LOW_POWER_UART_PORT1_REGISTER,
                    LowPowerUart) != 0) {
                sRepeaterLastResultCode = 0x32u;
                UsbHostRepeaterRefreshStatusBytes();
                PaintRepeaterStatusLine();
                return -1;
            }
        }
    }

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

UINT8 UsbHostRepeaterPort1ConfigurationByte(void) {
    return 0;
}

UINT8 UsbHostRepeaterLowPowerUartPort1Byte(void) {
    return 0;
}

int UsbHostRepeaterEnableHost(void) {
    return -1;
}

#endif
