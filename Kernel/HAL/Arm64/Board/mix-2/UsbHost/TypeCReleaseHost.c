/*
 * UsbHost/TypeCReleaseHost.c — PR-PHONE-PWR-1：温复位进 fastboot 前卸 Host
 *
 * 证据：ForceHost 后短按进 ABL，`fastboot devices` 空；冷进能认。
 * PMI8998 SPMI 跨温复位不掉；DFP + OTG boost 仍在 → 与 PC 双 Host。
 * 对表 smb-reg / qcom_smbx 默认：清 DFP、关 CMD_OTG、UFP_EN、VCONN 交硬件。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define PMI8998_SLAVE_IDENTIFIER                 2u
#define TYPE_C_SOFTWARE_CONTROL_ADDRESS          0x1368u
#define TYPE_C_CONFIGURATION_2_ADDRESS           0x1359u
#define TYPE_C_CONFIGURATION_3_ADDRESS           0x135Au
#define TYPE_C_POWER_ROLE_COMMAND_MASK           0x07u
#define TYPE_C_HOST_PORT_ENABLE_COMMAND          (1u << 1)
#define TYPE_C_DEVICE_PORT_ENABLE_COMMAND        (1u << 2)
#define TYPE_C_EXIT_SINK_BASED_ON_CC             (1u << 7)
#define TYPE_C_ENABLE_TRY_SOURCE_MODE            (1u << 3)
#define TYPE_C_ENABLE_TRY_SINK_MODE              (1u << 2)
#define TYPE_C_VCONN_ENABLE_ORIENTATION_BIT      (1u << 6)
#define TYPE_C_VCONN_ENABLE_SOURCE_BIT           (1u << 4)
#define TYPE_C_VCONN_ENABLE_VALUE_BIT            (1u << 3)
#define COMMAND_OTG_ADDRESS                      0x1140u
#define USBIN_COMMAND_INPUT_LIMIT_ADDRESS        0x1340u
#define USBIN_SUSPEND_BIT                        (1u << 0)

static void ClearTrySourceEnableTrySink(void) {
    UINT8 Configuration;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_CONFIGURATION_2_ADDRESS,
                            &Configuration) == 0) {
        Configuration =
            (UINT8)(Configuration & (UINT8)(~TYPE_C_ENABLE_TRY_SOURCE_MODE));
        (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                                   TYPE_C_CONFIGURATION_2_ADDRESS,
                                   Configuration);
    }
    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_CONFIGURATION_3_ADDRESS,
                            &Configuration) == 0) {
        Configuration =
            (UINT8)(Configuration | TYPE_C_ENABLE_TRY_SINK_MODE);
        (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                                   TYPE_C_CONFIGURATION_3_ADDRESS,
                                   Configuration);
    }
}

static void StopOtgBoostAndClearUsbinSuspend(void) {
    UINT8 Command;

    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS, 0);
    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            USBIN_COMMAND_INPUT_LIMIT_ADDRESS, &Command) == 0) {
        Command = (UINT8)(Command & (UINT8)(~USBIN_SUSPEND_BIT));
        (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                                   USBIN_COMMAND_INPUT_LIMIT_ADDRESS, Command);
    }
}

static int WriteTypeCDevicePortControl(void) {
    UINT8 Control;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_SOFTWARE_CONTROL_ADDRESS, &Control) != 0) {
        return -1;
    }
    Control = (UINT8)(Control & (UINT8)(~TYPE_C_POWER_ROLE_COMMAND_MASK) &
                      (UINT8)(~TYPE_C_EXIT_SINK_BASED_ON_CC) &
                      (UINT8)(~TYPE_C_VCONN_ENABLE_VALUE_BIT) &
                      (UINT8)(~TYPE_C_VCONN_ENABLE_ORIENTATION_BIT) &
                      (UINT8)(~TYPE_C_HOST_PORT_ENABLE_COMMAND));
    Control = (UINT8)(Control | TYPE_C_DEVICE_PORT_ENABLE_COMMAND |
                      TYPE_C_VCONN_ENABLE_SOURCE_BIT);
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                             TYPE_C_SOFTWARE_CONTROL_ADDRESS, Control) != 0) {
        return -2;
    }
    return 0;
}

int UsbHostTypeCReleaseHostForBootloader(void) {
    if (UsbHostSpmiArbiterProbe() != 0) {
        return -1;
    }
    StopOtgBoostAndClearUsbinSuspend();
    ClearTrySourceEnableTrySink();
    if (WriteTypeCDevicePortControl() != 0) {
        return -2;
    }
    /* CC / boost 掉电稳定后再温复位，给 ABL 干净 UFP */
    UsbHostStallYield(500000u);
    return 0;
}

#else

int UsbHostTypeCReleaseHostForBootloader(void) {
    return -1;
}

#endif
