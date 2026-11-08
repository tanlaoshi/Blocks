/*
 * UsbHost/TypeCForceHost.c — PMI8998 Type-C DFP（Host）
 *
 * 证据：插坞 `cc93 c224` = attach + DFP_RD_RA_VCONN → 须软件开 VCONN。
 * 对表 smb-reg：DFP + EXIT_SNK；VCONN_EN_VALUE；OTG 见 TypeCOtgVbus。
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
#define TYPE_C_STATUS_1_ADDRESS                  0x130Bu
#define TYPE_C_STATUS_2_ADDRESS                  0x130Cu
#define TYPE_C_STATUS_4_ADDRESS                  0x130Eu
#define TYPE_C_POWER_ROLE_COMMAND_MASK           0x07u
#define TYPE_C_HOST_PORT_ENABLE_COMMAND          (1u << 1)
#define TYPE_C_EXIT_SINK_BASED_ON_CC             (1u << 7)
#define TYPE_C_ENABLE_TRY_SOURCE_MODE            (1u << 3)
#define TYPE_C_ENABLE_TRY_SINK_MODE              (1u << 2)
#define TYPE_C_VCONN_ENABLE_ORIENTATION_BIT      (1u << 6)
#define TYPE_C_VCONN_ENABLE_SOURCE_BIT           (1u << 4)
#define TYPE_C_VCONN_ENABLE_VALUE_BIT            (1u << 3)
#define TYPE_C_STATUS_ATTACHED_BIT               (1u << 0)
#define TYPE_C_STATUS_ORIENTATION_BIT            (1u << 1)
#define TYPE_C_STATUS_DFP_RD_RA_VCONN_BIT        (1u << 2)
#define MISC_TEST_IO_DIGITAL4_SELECT_ADDRESS     0x16E9u
#define MISC_TEST_IO_DIGITAL4_SELECT_RESTORE     0xA5u
#define COMMAND_OTG_ADDRESS                      0x1140u
#define COMMAND_OTG_ENABLE                       (1u << 0)

static UINT8 sTypeCControlByte;
static UINT8 sTypeCStatus1Byte;
static UINT8 sTypeCStatus2Byte;
static UINT8 sTypeCStatus4Byte;
static UINT8 sAttachPhyKickDone;
static int sTypeCLastResult = -1;

int UsbHostTypeCLastResult(void) {
    return sTypeCLastResult;
}

UINT8 UsbHostTypeCControlByte(void) {
    return sTypeCControlByte;
}

UINT8 UsbHostTypeCStatus1Byte(void) {
    UINT8 Status;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, TYPE_C_STATUS_1_ADDRESS,
                            &Status) == 0) {
        sTypeCStatus1Byte = Status;
    }
    return sTypeCStatus1Byte;
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

static int WriteTypeCHostControl(void) {
    UINT8 Control;
    UINT8 Configuration;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_CONFIGURATION_3_ADDRESS,
                            &Configuration) == 0) {
        Configuration =
            (UINT8)(Configuration & (UINT8)(~TYPE_C_ENABLE_TRY_SINK_MODE));
        (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                                   TYPE_C_CONFIGURATION_3_ADDRESS,
                                   Configuration);
    }
    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_CONFIGURATION_2_ADDRESS,
                            &Configuration) == 0) {
        Configuration =
            (UINT8)(Configuration | TYPE_C_ENABLE_TRY_SOURCE_MODE);
        (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                                   TYPE_C_CONFIGURATION_2_ADDRESS,
                                   Configuration);
    }

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_SOFTWARE_CONTROL_ADDRESS, &Control) != 0) {
        return -2;
    }
    Control = (UINT8)((Control & (UINT8)(~TYPE_C_POWER_ROLE_COMMAND_MASK)) |
                      TYPE_C_HOST_PORT_ENABLE_COMMAND |
                      TYPE_C_EXIT_SINK_BASED_ON_CC);
    /* 默认交给硬件控 VCONN；attach 见 Rd/Ra 后再改软件开 */
    Control = (UINT8)((Control & (UINT8)(~TYPE_C_VCONN_ENABLE_VALUE_BIT)) |
                      TYPE_C_VCONN_ENABLE_SOURCE_BIT);
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                             TYPE_C_SOFTWARE_CONTROL_ADDRESS, Control) != 0) {
        return -3;
    }
    return 0;
}

/*
 * 插坞后轮询：cc attach → 软件 VCONN（Rd/Ra 时）+ 保 CMD_OTG + 一次 PHY 重踢。
 */
void UsbHostTypeCMaintainAfterAttach(void) {
    UINT8 Status4;
    UINT8 Status2;
    UINT8 Control;

    Status4 = UsbHostTypeCStatus4Byte();
    Status2 = UsbHostTypeCStatus2Byte();
    if ((Status4 & TYPE_C_STATUS_ATTACHED_BIT) == 0) {
        sAttachPhyKickDone = 0;
        return;
    }

    if ((Status2 & TYPE_C_STATUS_DFP_RD_RA_VCONN_BIT) != 0) {
        if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                                TYPE_C_SOFTWARE_CONTROL_ADDRESS,
                                &Control) == 0) {
            Control =
                (UINT8)(Control & (UINT8)(~TYPE_C_VCONN_ENABLE_SOURCE_BIT));
            Control = (UINT8)(Control | TYPE_C_VCONN_ENABLE_VALUE_BIT);
            if ((Status4 & TYPE_C_STATUS_ORIENTATION_BIT) != 0) {
                Control =
                    (UINT8)(Control | TYPE_C_VCONN_ENABLE_ORIENTATION_BIT);
            } else {
                Control = (UINT8)(Control &
                                  (UINT8)(~TYPE_C_VCONN_ENABLE_ORIENTATION_BIT));
            }
            (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                                       TYPE_C_SOFTWARE_CONTROL_ADDRESS,
                                       Control);
            sTypeCControlByte = Control;
        }
    }

    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS,
                               COMMAND_OTG_ENABLE);
    (void)UsbHostTypeCAnalogSwitchForceUsb();

    if (sAttachPhyKickDone == 0) {
        sAttachPhyKickDone = 1;
        (void)UsbHostQusbPhyAttachKick();
        (void)UsbHostQscratchConfigureForHost();
    }
}

int UsbHostTypeCForceHost(void) {
    UINT8 After;
    int StepResult;

    sTypeCLastResult = -1;
    sTypeCControlByte = 0;
    sTypeCStatus1Byte = 0;
    sTypeCStatus2Byte = 0;
    sTypeCStatus4Byte = 0;
    sAttachPhyKickDone = 0;
    if (UsbHostSpmiArbiterProbe() != 0) {
        sTypeCLastResult = -1;
        return sTypeCLastResult;
    }

    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                               MISC_TEST_IO_DIGITAL4_SELECT_ADDRESS,
                               MISC_TEST_IO_DIGITAL4_SELECT_RESTORE);

    /* WCD USBC analog → USB idle，否则 D+/D- 到不了 QUSB */
    (void)UsbHostTypeCAnalogSwitchForceUsb();

    StepResult = WriteTypeCHostControl();
    if (StepResult != 0) {
        sTypeCLastResult = StepResult;
        return sTypeCLastResult;
    }

    StepResult = UsbHostOtgVbusEnable();
    if (StepResult != 0) {
        sTypeCLastResult = StepResult;
        return sTypeCLastResult;
    }

    UsbHostStallYield(200000u);
    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            TYPE_C_SOFTWARE_CONTROL_ADDRESS, &After) != 0) {
        sTypeCLastResult = -2;
        return sTypeCLastResult;
    }
    sTypeCControlByte = After;
    (void)UsbHostTypeCStatus1Byte();
    (void)UsbHostTypeCStatus2Byte();
    (void)UsbHostTypeCStatus4Byte();
    (void)UsbHostOtgConfigurationByte();
    (void)UsbHostOtgStatusByte();
    (void)UsbHostCommandOtgByte();

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

UINT8 UsbHostTypeCStatus1Byte(void) {
    return 0;
}

UINT8 UsbHostTypeCStatus2Byte(void) {
    return 0;
}

UINT8 UsbHostTypeCStatus4Byte(void) {
    return 0;
}

int UsbHostTypeCForceHost(void) {
    return -1;
}

void UsbHostTypeCMaintainAfterAttach(void) {
}

#endif
