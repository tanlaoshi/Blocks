/*
 * UsbHost/HubStatusPaint.c — splash 根口 / eUSB / tc（行距 64，对表 scale4）
 *
 * SplashPaintLine 每行清高 ≈56px；邻行 Top 须 ≥+64，否则后画盖先画。
 * scale4 字宽 ≈36px、左边 24 → 一行约 29 字；超长必裁，故拆行。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

/* 行位见 Private.h USB_HOST_SPLASH_LINE_*（标题下 → p1…tc → KEY/k） */

static void AppendCompactHex(char *Line, UINT32 Capacity, UINT32 Value) {
    char Hex[12];
    const char *Digits;

    UsbHostFormatHexadecimal32(Hex, Value);
    Digits = Hex + 2;
    while (Digits[0] == '0' && Digits[1] != '\0') {
        Digits++;
    }
    UsbHostAppendText(Line, Digits, Capacity);
}

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable,
                           UINT32 HubAddress) {
    char Line[40];
    UINT32 PortIndex;

    /* 热插后须重读；勿只画 ForceHost/Enable 时的缓存 */
    UsbHostTypeCRefreshStatusBytes();
    UsbHostRepeaterRefreshStatusBytes();
    UsbHostPhysicalRefreshCommonControl();

    Line[0] = '\0';
    for (PortIndex = 1; PortIndex <= PortCount && PortIndex <= 4u; PortIndex++) {
        char Piece[4];
        if (PortIndex > 1u) {
            UsbHostAppendText(Line, " ", sizeof(Line));
        }
        Piece[0] = 'p';
        Piece[1] = (char)('0' + (int)PortIndex);
        Piece[2] = ':';
        Piece[3] = '\0';
        UsbHostAppendText(Line, Piece, sizeof(Line));
        AppendCompactHex(Line, sizeof(Line), LastStable[PortIndex]);
    }
    UsbHostAppendText(Line, " e", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostEnumerateStatusCode());
    UsbHostAppendText(Line, " a", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), HubAddress);
    /* u2＝USB2 口位图；st＝Protocol Slot Type（Address 排障） */
    UsbHostAppendText(Line, " u", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostXhciUsb2PortBitMask());
    UsbHostAppendText(Line, " s", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostXhciUsb2ProtocolSlotType());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_PORTS, Line);
    HalSerialWriteShell(Line);
    HalSerialWriteShell("\r\n");

    /* 行2：repeater；id/ba 已绿过，省宽给 c */
    Line[0] = '\0';
    UsbHostAppendText(Line, "eu", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostRepeaterLastResultCode());
    UsbHostAppendText(Line, " g", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostRepeaterGlobalConfigurationByte());
    /* c＝PORT1 HOST_DEVICE（对表 Android debugfs）；附 p0 对照 */
    UsbHostAppendText(Line, " c", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostRepeaterPort1ConfigurationByte());
    UsbHostAppendText(Line, " p0", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostRepeaterPort0ConfigurationByte());
    /* ua＝LOPWR_UART_P1；bit1=寄存器选源 bit0=UART 开（期望 ua≥2 且偶） */
    UsbHostAppendText(Line, " ua", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostRepeaterLowPowerUartPort1Byte());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_REPEATER, Line);
    HalSerialWriteShell(Line);
    HalSerialWriteShell("\r\n");

    /* 行3/4：ba / cm 各一行（勿再挤 cs，真机漏报 ba） */
    Line[0] = '\0';
    UsbHostAppendText(Line, "ph", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPhysicalLastResultCode());
    UsbHostAppendText(Line, " ba", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPhysicalFoundBasePage());
    UsbHostAppendText(Line, " cs", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPhysicalStrapControl());
    UsbHostAppendText(Line, " es", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPhysicalEmbeddedUsbState());
    /* ls＝dig LineState；pd＝UTMI dp/dm pulldown 位图（bit0=DP bit1=DM） */
    UsbHostAppendText(Line, " ls", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPhysicalLineState());
    UsbHostAppendText(Line, " pd", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPhysicalUtmiPullDown());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_PHYSICAL_BASE, Line);
    HalSerialWriteShell(Line);
    HalSerialWriteShell("\r\n");

    Line[0] = '\0';
    /* ck 已绿过；本行盯 DualRole + ACPM：dr / u / k / ac=LDO */
    UsbHostAppendText(Line, "cm", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostPhysicalCommonControl());
    UsbHostAppendText(Line, " dr", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostDualRoleDevicePhysicalLastResultCode());
    UsbHostAppendText(Line, " u", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostDualRoleDevicePhysicalUtmi() & 0xFFu);
    UsbHostAppendText(Line, " k", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostDualRoleDevicePhysicalClockReset() & 0xFFu);
    UsbHostAppendText(Line, " ac", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line),
                     UsbHostAliveClockPowerManagerLastResultCode());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_PHYSICAL_COMMON, Line);
    HalSerialWriteShell(Line);
    HalSerialWriteShell("\r\n");

    Line[0] = '\0';
    UsbHostAppendText(Line, "tc", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCLastResultCode());
    UsbHostAppendText(Line, " r", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCRoleControlByte());
    UsbHostAppendText(Line, " cc", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCCcStatusByte());
    UsbHostAppendText(Line, " ps", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCPowerStatusByte());
    UsbHostAppendText(Line, " sw", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCUsbSwitchByte());
    /* vb＝buck-boost@0x69 源模（期望 a）；ee＝I2C 写失败 */
    UsbHostAppendText(Line, " vb", sizeof(Line));
    AppendCompactHex(Line, sizeof(Line), UsbHostTypeCBuckBoostByte());
    BoardBringUpSplashPaintLine(USB_HOST_SPLASH_LINE_TYPE_C, Line);
    HalSerialWriteShell(Line);
    HalSerialWriteShell("\r\n");
}

#else

void UsbHostPaintHubStatus(UINT32 PortCount, const UINT32 *LastStable,
                           UINT32 HubAddress) {
    (void)PortCount;
    (void)LastStable;
    (void)HubAddress;
}

#endif
