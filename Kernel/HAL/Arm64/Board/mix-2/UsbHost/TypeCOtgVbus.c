/*
 * UsbHost/TypeCOtgVbus.c — PMI8998 OTG boost（软件开 VBUS）
 *
 * 证据：`il1 os0` = USBIN 已挂起，boost 仍未 softstart。
 * 对表 smblib + SPMI SEC_ACCESS：
 *   清 DIS_OTG_ON_TLIM；SEC_ACCESS 后立刻写 ENG（勿插读）；
 *   电流爬坡；刷 eng 回读与 OTG INT_RT_STS。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define PMI8998_SLAVE_IDENTIFIER                 2u
#define COMMAND_OTG_ADDRESS                      0x1140u
#define COMMAND_OTG_ENABLE                       (1u << 0)
#define OTG_STATUS_ADDRESS                       0x1109u
#define OTG_STATUS_BOOST_SOFTSTART_DONE          (1u << 3)
#define OTG_STATUS_STATE_MASK                    0x07u
#define OTG_STATUS_STATE_ENABLED                 0x02u
#define OTG_INTERRUPT_REALTIME_STATUS_ADDRESS    0x1110u
#define OTG_CONFIGURATION_ADDRESS                0x1153u
#define OTG_ENABLE_SOURCE_CONFIGURATION_BIT      (1u << 1)
#define OTG_DISABLE_ON_THERMAL_LIMIT_BIT         (1u << 5)
#define OTG_CURRENT_LIMIT_CONFIGURATION_ADDRESS  0x1152u
#define OTG_ENGINEERING_CONFIGURATION_ADDRESS    0x11C0u
#define OTG_ENGINEERING_HALT_ONE_IN_EIGHT_BIT    (1u << 0)
#define OTG_SECURITY_ACCESS_ADDRESS              0x11D0u
#define OTG_SECURITY_ACCESS_UNLOCK               0xA5u
#define USBIN_COMMAND_INPUT_LIMIT_ADDRESS        0x1340u
#define USBIN_SUSPEND_BIT                        (1u << 0)

static UINT8 sOtgConfigurationByte;
static UINT8 sOtgStatusByte;
static UINT8 sCommandOtgByte;
static UINT8 sUsbinCommandByte;
static UINT8 sOtgEngineeringByte;
static UINT8 sOtgInterruptStatusByte;

UINT8 UsbHostOtgConfigurationByte(void) {
    UINT8 Configuration;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, OTG_CONFIGURATION_ADDRESS,
                            &Configuration) == 0) {
        sOtgConfigurationByte = Configuration;
    }
    return sOtgConfigurationByte;
}

UINT8 UsbHostOtgStatusByte(void) {
    UINT8 Status;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, OTG_STATUS_ADDRESS,
                            &Status) == 0) {
        sOtgStatusByte = Status;
    }
    return sOtgStatusByte;
}

UINT8 UsbHostCommandOtgByte(void) {
    UINT8 Command;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS,
                            &Command) == 0) {
        sCommandOtgByte = Command;
    }
    return sCommandOtgByte;
}

UINT8 UsbHostUsbinCommandByte(void) {
    UINT8 Command;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            USBIN_COMMAND_INPUT_LIMIT_ADDRESS, &Command) == 0) {
        sUsbinCommandByte = Command;
    }
    return sUsbinCommandByte;
}

UINT8 UsbHostOtgEngineeringByte(void) {
    UINT8 Engineering;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            OTG_ENGINEERING_CONFIGURATION_ADDRESS,
                            &Engineering) == 0) {
        sOtgEngineeringByte = Engineering;
    }
    return sOtgEngineeringByte;
}

UINT8 UsbHostOtgInterruptStatusByte(void) {
    UINT8 Status;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            OTG_INTERRUPT_REALTIME_STATUS_ADDRESS,
                            &Status) == 0) {
        sOtgInterruptStatusByte = Status;
    }
    return sOtgInterruptStatusByte;
}

static int SoftstartDone(void) {
    (void)UsbHostOtgStatusByte();
    if ((sOtgStatusByte & OTG_STATUS_BOOST_SOFTSTART_DONE) != 0) {
        return 1;
    }
    if ((sOtgStatusByte & OTG_STATUS_STATE_MASK) == OTG_STATUS_STATE_ENABLED) {
        return 1;
    }
    return 0;
}

static int SuspendUsbinForBoost(void) {
    UINT8 Command;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            USBIN_COMMAND_INPUT_LIMIT_ADDRESS, &Command) != 0) {
        return -10;
    }
    Command = (UINT8)(Command | USBIN_SUSPEND_BIT);
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                             USBIN_COMMAND_INPUT_LIMIT_ADDRESS, Command) != 0) {
        return -11;
    }
    sUsbinCommandByte = Command;
    return 0;
}

static int WriteEngineeringHalt(void) {
    UINT8 Engineering;

    /* 先读；SEC_ACCESS 只对紧随其后的那一次写生效 */
    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER,
                            OTG_ENGINEERING_CONFIGURATION_ADDRESS,
                            &Engineering) != 0) {
        return -8;
    }
    Engineering =
        (UINT8)(Engineering | OTG_ENGINEERING_HALT_ONE_IN_EIGHT_BIT);
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                             OTG_SECURITY_ACCESS_ADDRESS,
                             OTG_SECURITY_ACCESS_UNLOCK) != 0) {
        return -8;
    }
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                             OTG_ENGINEERING_CONFIGURATION_ADDRESS,
                             Engineering) != 0) {
        return -9;
    }
    sOtgEngineeringByte = Engineering;
    return 0;
}

static int ConfigureOtgSoftwareSource(void) {
    UINT8 Configuration;

    if (UsbHostSpmiReadByte(PMI8998_SLAVE_IDENTIFIER, OTG_CONFIGURATION_ADDRESS,
                            &Configuration) != 0) {
        return -6;
    }
    /* 软件控 + 勿因 TLIM 立刻关掉 boost */
    Configuration = (UINT8)(Configuration &
                            (UINT8)(~OTG_ENABLE_SOURCE_CONFIGURATION_BIT) &
                            (UINT8)(~OTG_DISABLE_ON_THERMAL_LIMIT_BIT));
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, OTG_CONFIGURATION_ADDRESS,
                             Configuration) != 0) {
        return -7;
    }
    sOtgConfigurationByte = Configuration;
    return 0;
}

static int TryOtgWithCurrentLimit(UINT8 LimitCode) {
    UINTN Guard;

    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS, 0);
    UsbHostStallYield(2000u);
    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                               OTG_CURRENT_LIMIT_CONFIGURATION_ADDRESS,
                               LimitCode);
    if (UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS,
                             COMMAND_OTG_ENABLE) != 0) {
        return -5;
    }
    for (Guard = 0; Guard < 40u; Guard++) {
        UsbHostStallYield(5000u);
        if (SoftstartDone() != 0) {
            return 0;
        }
    }
    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS, 0);
    return -12;
}

static void RefreshOtgDiagnostics(void) {
    (void)UsbHostOtgStatusByte();
    (void)UsbHostCommandOtgByte();
    (void)UsbHostUsbinCommandByte();
    (void)UsbHostOtgConfigurationByte();
    (void)UsbHostOtgEngineeringByte();
    (void)UsbHostOtgInterruptStatusByte();
}

int UsbHostOtgVbusEnable(void) {
    UINT8 LimitCode;
    int StepResult;

    sOtgConfigurationByte = 0;
    sOtgStatusByte = 0;
    sCommandOtgByte = 0;
    sUsbinCommandByte = 0;
    sOtgEngineeringByte = 0;
    sOtgInterruptStatusByte = 0;

    StepResult = SuspendUsbinForBoost();
    if (StepResult != 0) {
        return StepResult;
    }

    StepResult = ConfigureOtgSoftwareSource();
    if (StepResult != 0) {
        return StepResult;
    }

    StepResult = WriteEngineeringHalt();
    if (StepResult != 0) {
        return StepResult;
    }

    for (LimitCode = 0; LimitCode < 4u; LimitCode++) {
        if (TryOtgWithCurrentLimit(LimitCode) == 0) {
            RefreshOtgDiagnostics();
            return 0;
        }
    }

    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER,
                               OTG_CURRENT_LIMIT_CONFIGURATION_ADDRESS, 0);
    (void)UsbHostSpmiWriteByte(PMI8998_SLAVE_IDENTIFIER, COMMAND_OTG_ADDRESS,
                               COMMAND_OTG_ENABLE);
    RefreshOtgDiagnostics();
    return 0;
}

#else

UINT8 UsbHostOtgConfigurationByte(void) {
    return 0;
}

UINT8 UsbHostOtgStatusByte(void) {
    return 0;
}

UINT8 UsbHostCommandOtgByte(void) {
    return 0;
}

UINT8 UsbHostUsbinCommandByte(void) {
    return 0;
}

UINT8 UsbHostOtgEngineeringByte(void) {
    return 0;
}

UINT8 UsbHostOtgInterruptStatusByte(void) {
    return 0;
}

int UsbHostOtgVbusEnable(void) {
    return -1;
}

#endif
