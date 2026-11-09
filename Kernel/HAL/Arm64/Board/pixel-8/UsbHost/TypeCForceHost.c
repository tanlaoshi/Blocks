/*
 * UsbHost/TypeCForceHost.c — max77759 DFP + USBSW@0x93=0x09 + buck-boost VBUS
 * 对表 max_tcpci_set_vbus：@0x69 reg0xb9=0x0a（真机 ls0：TCPC 0x77 不够）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define TYPE_C_PORT_CONTROLLER_DEVICE_ADDRESS 0x25u
#define TYPE_C_BUCK_BOOST_DEVICE_ADDRESS 0x69u

#define TCPC_VENDOR_IDENTIFIER_REGISTER       0x00u
#define TCPC_PRODUCT_IDENTIFIER_REGISTER      0x02u
#define TCPC_POWER_CONTROL_REGISTER           0x1Cu
#define TCPC_ROLE_CONTROL_REGISTER            0x1Au
#define TCPC_CC_STATUS_REGISTER               0x1Du
#define TCPC_POWER_STATUS_REGISTER            0x1Eu
#define TCPC_COMMAND_REGISTER                 0x23u
#define TCPC_VENDOR_USB_SWITCH_CONTROL        0x93u

#define TCPC_ROLE_CONTROL_CC_RESISTOR_PULL_UP 0x01u
#define TCPC_ROLE_CONTROL_CC_OPEN             0x03u
#define TCPC_ROLE_CONTROL_BOTH_PULL_UP                                      \
    ((TCPC_ROLE_CONTROL_CC_RESISTOR_PULL_UP << 2) |                         \
     TCPC_ROLE_CONTROL_CC_RESISTOR_PULL_UP)
#define TCPC_ROLE_CONTROL_BOTH_OPEN                                         \
    ((TCPC_ROLE_CONTROL_CC_OPEN << 2) | TCPC_ROLE_CONTROL_CC_OPEN)

#define TCPC_COMMAND_DISABLE_SINK_VBUS        0x44u
#define TCPC_COMMAND_SOURCE_VBUS_DEFAULT      0x77u
#define TCPC_COMMAND_LOOK_FOR_CONNECTION      0x99u
#define TCPC_VENDOR_USB_SWITCH_ENABLE_DATA    0x09u

/* 对表 tcpci_maxim max_tcpci_set_vbus（Pixel WAR） */
#define TYPE_C_BUCK_BOOST_OPERATION_REGISTER 0xB9u
#define TYPE_C_BUCK_BOOST_SOURCE_VALUE 0x0Au

static UINT8 sTypeCLastResultCode;
static UINT8 sTypeCVendorIdentifierLow;
static UINT8 sTypeCCcStatus;
static UINT8 sTypeCPowerStatus;
static UINT8 sTypeCRoleControl;
static UINT8 sTypeCUsbSwitch;
static UINT8 sTypeCBuckBoost;

UINT8 UsbHostTypeCLastResultCode(void) {
    return sTypeCLastResultCode;
}

UINT8 UsbHostTypeCCcStatusByte(void) {
    return sTypeCCcStatus;
}

UINT8 UsbHostTypeCPowerStatusByte(void) {
    return sTypeCPowerStatus;
}

UINT8 UsbHostTypeCRoleControlByte(void) {
    return sTypeCRoleControl;
}

UINT8 UsbHostTypeCUsbSwitchByte(void) {
    return sTypeCUsbSwitch;
}

UINT8 UsbHostTypeCBuckBoostByte(void) {
    return sTypeCBuckBoost;
}

static int WritePortControllerRegister(UINT8 RegisterAddress, UINT8 Value) {
    UINT8 Bytes[2];

    Bytes[0] = RegisterAddress;
    Bytes[1] = Value;
    return UsbHostHighSpeedInterIntegratedCircuitWrite(
        TYPE_C_PORT_CONTROLLER_DEVICE_ADDRESS, Bytes, 2u);
}

static int WriteBuckBoostSource(void) {
    UINT8 Bytes[2];

    Bytes[0] = TYPE_C_BUCK_BOOST_OPERATION_REGISTER;
    Bytes[1] = TYPE_C_BUCK_BOOST_SOURCE_VALUE;
    return UsbHostHighSpeedInterIntegratedCircuitWrite(
        TYPE_C_BUCK_BOOST_DEVICE_ADDRESS, Bytes, 2u);
}

static int ReadPortControllerRegister(UINT8 RegisterAddress, UINT8 *Value) {
    return UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
        TYPE_C_PORT_CONTROLLER_DEVICE_ADDRESS, RegisterAddress, Value, 1u);
}

static void AppendByteHex(char *Line, UINTN Capacity, UINT8 Value) {
    char Hex[12];

    UsbHostFormatHexadecimal32(Hex, Value);
    UsbHostAppendText(Line, Hex + 8, Capacity);
}

static void PaintTypeCStatusLine(void) {
    char Line[48];

    UsbHostCopyText(Line, "tc", sizeof(Line));
    AppendByteHex(Line, sizeof(Line), sTypeCLastResultCode);
    UsbHostAppendText(Line, " r", sizeof(Line));
    AppendByteHex(Line, sizeof(Line), sTypeCRoleControl);
    UsbHostAppendText(Line, " cc", sizeof(Line));
    AppendByteHex(Line, sizeof(Line), sTypeCCcStatus);
    UsbHostAppendText(Line, " ps", sizeof(Line));
    AppendByteHex(Line, sizeof(Line), sTypeCPowerStatus);
    UsbHostAppendText(Line, " sw", sizeof(Line));
    AppendByteHex(Line, sizeof(Line), sTypeCUsbSwitch);
    UsbHostAppendText(Line, " vb", sizeof(Line));
    AppendByteHex(Line, sizeof(Line), sTypeCBuckBoost);
    BoardBringUpSplashPaintLine(1392, Line);
    HalSerialWriteShell(Line);
    HalSerialWriteShell("\r\n");
}

static void PaintInterIntegratedDiagnostic(UINT8 ResultCode) {
    char Line[56];
    char Hex[12];

    sTypeCLastResultCode = ResultCode;
    UsbHostCopyText(Line, "i2 c", sizeof(Line));
    UsbHostFormatHexadecimal32(
        Hex, UsbHostHighSpeedInterIntegratedCircuitDiagnosticControl());
    UsbHostAppendText(Line, Hex + 8, sizeof(Line));
    UsbHostAppendText(Line, " u", sizeof(Line));
    UsbHostFormatHexadecimal32(
        Hex, UsbHostHighSpeedInterIntegratedCircuitDiagnosticUniversalSerial());
    UsbHostAppendText(Line, Hex + 8, sizeof(Line));
    UsbHostAppendText(Line, " i", sizeof(Line));
    UsbHostFormatHexadecimal32(
        Hex, UsbHostHighSpeedInterIntegratedCircuitDiagnosticInterrupt());
    UsbHostAppendText(Line, Hex + 6, sizeof(Line));
    UsbHostAppendText(Line, " t", sizeof(Line));
    UsbHostFormatHexadecimal32(
        Hex, UsbHostHighSpeedInterIntegratedCircuitDiagnosticTransfer());
    UsbHostAppendText(Line, Hex + 6, sizeof(Line));
        BoardBringUpSplashPaintLine(1392, Line);
}

void UsbHostTypeCRefreshStatusBytes(void) {
    (void)ReadPortControllerRegister(TCPC_ROLE_CONTROL_REGISTER,
                                     &sTypeCRoleControl);
    (void)ReadPortControllerRegister(TCPC_CC_STATUS_REGISTER, &sTypeCCcStatus);
    (void)ReadPortControllerRegister(TCPC_POWER_STATUS_REGISTER,
                                     &sTypeCPowerStatus);
    (void)ReadPortControllerRegister(TCPC_VENDOR_USB_SWITCH_CONTROL,
                                     &sTypeCUsbSwitch);
}

int UsbHostTypeCForceHost(void) {
    UINT8 VendorLow = 0;
    UINT8 VendorHigh = 0;
    UINT8 ProductLow = 0;
    int Result;

    sTypeCLastResultCode = 0;
    sTypeCVendorIdentifierLow = 0;
    sTypeCCcStatus = 0;
    sTypeCPowerStatus = 0;
    sTypeCRoleControl = 0;
    sTypeCUsbSwitch = 0;
    sTypeCBuckBoost = 0;

    BoardBringUpSplashPaintLine(1392, "tc probe");
    HalSerialWriteShell("tc: probe\r\n");

    Result = ReadPortControllerRegister(TCPC_VENDOR_IDENTIFIER_REGISTER,
                                        &VendorLow);
    if (Result != 0 || VendorLow == 0u || VendorLow == 0xFFu) {
        if (Result != 0) {
            PaintInterIntegratedDiagnostic(
                (UINT8)(0x10u + (UINT8)(-Result)));
        } else {
            sTypeCVendorIdentifierLow = VendorLow;
            PaintInterIntegratedDiagnostic(0x13u);
        }
        PaintTypeCStatusLine();
        return -1;
    }
    (void)ReadPortControllerRegister(TCPC_VENDOR_IDENTIFIER_REGISTER + 1u,
                                     &VendorHigh);
    (void)ReadPortControllerRegister(TCPC_PRODUCT_IDENTIFIER_REGISTER,
                                     &ProductLow);
    sTypeCVendorIdentifierLow = VendorLow;
    HalSerialWriteShell("tc: vid ");
    {
        char Hex[12];
        UsbHostFormatHexadecimal32(Hex, ((UINT32)VendorHigh << 8) | VendorLow);
        HalSerialWriteShell(Hex);
        HalSerialWriteShell(" pid ");
        UsbHostFormatHexadecimal32(Hex, ProductLow);
        HalSerialWriteShell(Hex);
        HalSerialWriteShell("\r\n");
    }

    /* 对表 max_tcpci_init_regs：POWER_CTRL=0 开监测 */
    (void)WritePortControllerRegister(TCPC_POWER_CONTROL_REGISTER, 0u);

    (void)WritePortControllerRegister(TCPC_COMMAND_REGISTER,
                                      TCPC_COMMAND_DISABLE_SINK_VBUS);
    UsbHostStallYield(20000u);

    /*
     * ABL 常留下 Looking4Connection（真机 cc30，即使本核不发 Look4）。
     * 双 Open + Look4 → 找「断开」以清粘位；再呈 Rp，不再 Look4。
     */
    if (WritePortControllerRegister(TCPC_ROLE_CONTROL_REGISTER,
                                    TCPC_ROLE_CONTROL_BOTH_OPEN) != 0) {
        sTypeCLastResultCode = 0x21u;
        PaintTypeCStatusLine();
        return -1;
    }
    (void)WritePortControllerRegister(TCPC_COMMAND_REGISTER,
                                      TCPC_COMMAND_LOOK_FOR_CONNECTION);
    UsbHostStallYield(50000u);

    sTypeCRoleControl = TCPC_ROLE_CONTROL_BOTH_PULL_UP;
    if (WritePortControllerRegister(TCPC_ROLE_CONTROL_REGISTER,
                                    sTypeCRoleControl) != 0) {
        sTypeCLastResultCode = 0x22u;
        PaintTypeCStatusLine();
        return -1;
    }
    /* 呈 Rp 后再 Look4（TCPCI：按当前 ROLE 开始找连接） */
    (void)WritePortControllerRegister(TCPC_COMMAND_REGISTER,
                                      TCPC_COMMAND_LOOK_FOR_CONNECTION);
    UsbHostStallYield(50000u);

    (void)WritePortControllerRegister(TCPC_COMMAND_REGISTER,
                                      TCPC_COMMAND_SOURCE_VBUS_DEFAULT);
    /* 真 5V：buck-boost@0x69；TCPC 0x77  alone 不够（真机 ls0） */
    if (WriteBuckBoostSource() != 0) {
        sTypeCBuckBoost = 0xEEu;
        sTypeCLastResultCode = 0x33u;
        UsbHostTypeCRefreshStatusBytes();
        PaintTypeCStatusLine();
        return -1;
    }
    sTypeCBuckBoost = TYPE_C_BUCK_BOOST_SOURCE_VALUE;
    UsbHostStallYield(30000u);

    if (WritePortControllerRegister(TCPC_VENDOR_USB_SWITCH_CONTROL,
                                    TCPC_VENDOR_USB_SWITCH_ENABLE_DATA) != 0) {
        sTypeCLastResultCode = 0x31u;
        UsbHostTypeCRefreshStatusBytes();
        PaintTypeCStatusLine();
        return -1;
    }
    UsbHostStallYield(30000u);

    UsbHostTypeCRefreshStatusBytes();
    sTypeCLastResultCode = 1u;
    PaintTypeCStatusLine();
    return 0;
}

#else

void UsbHostTypeCRefreshStatusBytes(void) {
}

UINT8 UsbHostTypeCLastResultCode(void) {
    return 0;
}

UINT8 UsbHostTypeCCcStatusByte(void) {
    return 0;
}

UINT8 UsbHostTypeCPowerStatusByte(void) {
    return 0;
}

UINT8 UsbHostTypeCRoleControlByte(void) {
    return 0;
}

UINT8 UsbHostTypeCUsbSwitchByte(void) {
    return 0;
}

UINT8 UsbHostTypeCBuckBoostByte(void) {
    return 0;
}

int UsbHostTypeCForceHost(void) {
    return -1;
}

#endif
