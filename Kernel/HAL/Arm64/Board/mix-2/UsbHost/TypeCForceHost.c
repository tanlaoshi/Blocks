/*
 * UsbHost/TypeCForceHost.c — PMI8998 Type-C DFP + OTG（Host）
 *
 * 证据：`ls80 tc0/a2 cc80` = DFP+EXIT_SNK 已成，但无 CC attach / VBUS。
 * 对表 qcom_usb_vbus-regulator + smb-reg：
 *   0x1368 DFP_EN bit1、EXIT_SNK_BASED_ON_CC bit7；
 *   0x1153 OTG_CFG 须清 OTG_EN_SRC bit1（否则 CMD_OTG 被硬件盖掉）；
 *   0x1140 CMD_OTG OTG_EN bit0。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define PMI8998_SLAVE_IDENTIFIER                 2u
#define TYPE_C_SOFTWARE_CONTROL_ADDRESS          0x1368u
#define TYPE_C_STATUS_2_ADDRESS                  0x130Cu
#define TYPE_C_STATUS_4_ADDRESS                  0x130Eu
#define TYPE_C_POWER_ROLE_COMMAND_MASK           0x07u
#define TYPE_C_HOST_PORT_ENABLE_COMMAND          (1u << 1) /* DFP_EN */
#define TYPE_C_EXIT_SINK_BASED_ON_CC             (1u << 7)
#define COMMAND_OTG_ADDRESS                      0x1140u
#define COMMAND_OTG_ENABLE                       (1u << 0)
#define OTG_CONFIGURATION_ADDRESS                0x1153u
#define OTG_ENABLE_SOURCE_CONFIGURATION_BIT      (1u << 1)
#define MISC_TEST_IO_DIGITAL4_SELECT_ADDRESS     0x16E9u
#define MISC_TEST_IO_DIGITAL4_SELECT_RESTORE     0xA5u

static UINT8 sTypeCControlByte;
static UINT8 sTypeCStatus2Byte;
static UINT8 sTypeCStatus4Byte;
static UINT8 sOtgConfigurationByte;
static int sTypeCLastResult = -1;

int UsbHostTypeCLastResult(void) {
    return sTypeCLastResult;
}

UINT8 UsbHostTypeCControlByte(void) {
    return sTypeCControlByte;
}

UINT8 UsbHostTypeCStatus2Byte(void) {
    UINT8 Status;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, TYPE_C_STATUS_2_ADDRESS,
                            &Status) == 0) {
        sTypeCStatus2Byte = Status;
    }
    return sTypeCStatus2Byte;
}

UINT8 UsbHostTypeCStatus4Byte(void) {
    UINT8 Status;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, TYPE_C_STATUS_4_ADDRESS,
                            &Status) == 0) {
        sTypeCStatus4Byte = Status;
    }
    return sTypeCStatus4Byte;
}

UINT8 UsbHostOtgConfigurationByte(void) {
    UINT8 Configuration;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, OTG_CONFIGURATION_ADDRESS,
                            &Configuration) == 0) {
        sOtgConfigurationByte = Configuration;
    }
    return sOtgConfigurationByte;
}

static int WriteTypeCHostControl(void) {
    UINT8 Control;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_SOFTWARE_CONTROL_ADDRESS, &Control) != 0) {
        return -2;
    }

    /* DFP + 按 CC 退出 sink（坞呈 Rd 时离开充电 sink） */
    Control = (UINT8)((Control & (UINT8)(~TYPE_C_POWER_ROLE_COMMAND_MASK)) |
                      TYPE_C_HOST_PORT_ENABLE_COMMAND |
                      TYPE_C_EXIT_SINK_BASED_ON_CC);
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                             TYPE_C_SOFTWARE_CONTROL_ADDRESS, Control) != 0) {
        return -3;
    }
    return 0;
}

static int EnableOtgVbusSoftware(void) {
    UINT8 Configuration;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, OTG_CONFIGURATION_ADDRESS,
                            &Configuration) != 0) {
        return -6;
    }
    /* 对表 regulator probe：清 HW 源，改由 CMD_OTG 软件开 VBUS */
    Configuration =
        (UINT8)(Configuration & (UINT8)(~OTG_ENABLE_SOURCE_CONFIGURATION_BIT));
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, OTG_CONFIGURATION_ADDRESS,
                             Configuration) != 0) {
        return -7;
    }
    sOtgConfigurationByte = Configuration;

    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS,
                             COMMAND_OTG_ENABLE) != 0) {
        return -5;
    }
    return 0;
}

int UsbHostTypeCForceHost(void) {
    UINT8 After;
    int StepResult;

    sTypeCLastResult = -1;
    sTypeCControlByte = 0;
    sTypeCStatus2Byte = 0;
    sTypeCStatus4Byte = 0;
    sOtgConfigurationByte = 0;
    if (UsbHostSpmiArbiterProbe() != 0) {
        sTypeCLastResult = -1;
        return sTypeCLastResult;
    }

    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                               MISC_TEST_IO_DIGITAL4_SELECT_ADDRESS,
                               MISC_TEST_IO_DIGITAL4_SELECT_RESTORE);

    StepResult = WriteTypeCHostControl();
    if (StepResult != 0) {
        sTypeCLastResult = StepResult;
        return sTypeCLastResult;
    }

    StepResult = EnableOtgVbusSoftware();
    if (StepResult != 0) {
        sTypeCLastResult = StepResult;
        return sTypeCLastResult;
    }

    UsbHostStallYield(500000u);
    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_SOFTWARE_CONTROL_ADDRESS, &After) != 0) {
        sTypeCLastResult = -2;
        return sTypeCLastResult;
    }
    sTypeCControlByte = After;
    (void)UsbHostTypeCStatus2Byte();
    (void)UsbHostTypeCStatus4Byte();
    (void)UsbHostOtgConfigurationByte();

    if ((After & TYPE_C_HOST_PORT_ENABLE_COMMAND) == 0) {
        sTypeCLastResult = -4;
        return sTypeCLastResult;
    }

    sTypeCLastResult = 0;
    return 0;
}

#else

int UsbHostTypeCLastResult(void) {
    return -1;
}

UINT8 UsbHostTypeCControlByte(void) {
    return 0;
}

UINT8 UsbHostTypeCStatus2Byte(void) {
    return 0;
}

UINT8 UsbHostTypeCStatus4Byte(void) {
    return 0;
}

UINT8 UsbHostOtgConfigurationByte(void) {
    return 0;
}

int UsbHostTypeCForceHost(void) {
    return -1;
}

#endif
