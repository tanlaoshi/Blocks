/*
 * UsbHost/DualRoleDevicePhysical.c — shiba BLKCON@0x11100000（init_link）
 *
 * 真机：`11100000.phy` MEM0＝USBCON/BLKCON；对表 exynos_usbcon_init_link。
 * 曾误用 EXYNOS850（UTMI@0x50）→ 真机 dr1 u0；正确 UTMI_CTRL@0x10。
 * eUSB CON@0x11110000 仍由 EmbeddedUsbPhysical 做 initiate。
 * LDO（vdd075/12/33）由 AliveClockPowerManager 开；本文件只做 BLKCON。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define DUAL_ROLE_DEVICE_PHYSICAL_BASE 0x11100000ULL

/* gs201/zuma：pmu_system_controller@0x18060000（gs101 才是 0x17460000） */
#define POWER_MANAGEMENT_UNIT_PHYSICAL_BASE 0x18060000ULL
#define POWER_MANAGEMENT_UNIT_USB_PHYSICAL_OFFSET 0x3EB0u
#define POWER_MANAGEMENT_UNIT_USB_PHYSICAL_OFFSET_DISPLAY_PORT 0x3EB4u
/* 旧 Exynos5 USBDRD_PHY_CONTROL；作粘性对照 */
#define POWER_MANAGEMENT_UNIT_USB_PHYSICAL_OFFSET_LEGACY 0x0704u
#define POWER_MANAGEMENT_UNIT_PHYSICAL_ENABLE (1u << 0)

/* 对表 exynos-usb-blkcon-sfr.h */
#define USB_CONNECTION_LINK_CONTROL_REGISTER 0x04u
#define USB_CONNECTION_LINK_CLOCK_RESET_REGISTER 0x0Cu
#define USB_CONNECTION_UTMI_CONTROL_REGISTER 0x10u

#define LINK_CONTROL_BUS_FILTER_BYPASS_MASK (0xFu << 4)
#define LINK_CONTROL_BUS_FILTER_BYPASS_HOST (0xFu << 4)
#define LINK_CONTROL_FORCE_QUEUE_ACTIVE (1u << 8)
#define LINK_CONTROL_DISABLE_BVALID_QUEUE_ACTIVE (1u << 9)
#define LINK_CONTROL_DISABLE_VBUSVALID_QUEUE_ACTIVE (1u << 10)
#define LINK_CONTROL_DISABLE_IDENTIFIER_QUEUE_ACTIVE (1u << 11)
#define LINK_CONTROL_DISABLE_LINK_GATE_QUEUE_ACTIVE (1u << 12)
#define LINK_CONTROL_FORCE_PIPE_ENABLE (1u << 16)
#define LINK_CONTROL_FORCE_PHYSICAL_STATUS (1u << 17)
#define LINK_CONTROL_FORCE_RECEIVE_ELECTRICAL_IDLE (1u << 18)
#define LINK_CONTROL_SELECT_START_OF_FRAME_MASK (0x3u << 20)
/* zuma-usb.dtsi 无 sel_sof → Android 默认 0；勿写 1 */
#define LINK_CONTROL_SELECT_START_OF_FRAME_ZERO (0x0u << 20)

#define LINK_CLOCK_RESET_SOFTWARE_RESET (1u << 0)
#define LINK_CLOCK_RESET_PIPE_CLOCK_SELECT (1u << 1)

#define UTMI_CONTROL_FORCE_B_VALID (1u << 0)
#define UTMI_CONTROL_FORCE_VBUS_VALID (1u << 1)

/* 对表 exynos_usbcon_disable_pipe3_phy：未开 SS 时只校 eUSB */
#define USB_CONNECTION_HIGH_SPEED_MISCELLANEOUS_REGISTER 0x114u
#define HIGH_SPEED_MISCELLANEOUS_SELECT_RESISTANCE_TUNE_MUX_MASK (3u << 0)
#define HIGH_SPEED_MISCELLANEOUS_SELECT_RESISTANCE_TUNE_MUX_PHYSICAL1_ONLY \
    (2u << 0)
#define HIGH_SPEED_MISCELLANEOUS_SET_REQUEST_IN2 (1u << 4)
#define HIGH_SPEED_MISCELLANEOUS_SET_ACKNOWLEDGE_IN2 (1u << 5)
#define HIGH_SPEED_MISCELLANEOUS_SET_REQUEST_IN1 (1u << 6)
#define HIGH_SPEED_MISCELLANEOUS_SET_ACKNOWLEDGE_IN1 (1u << 7)

static UINT8 sDualRoleLastResultCode;
static UINT32 sDualRoleClockReset;
static UINT32 sDualRoleUtmi;
static UINT32 sDualRolePowerManagement;

UINT8 UsbHostDualRoleDevicePhysicalLastResultCode(void) {
    return sDualRoleLastResultCode;
}

UINT32 UsbHostDualRoleDevicePhysicalClockReset(void) {
    return sDualRoleClockReset;
}

UINT32 UsbHostDualRoleDevicePhysicalUtmi(void) {
    return sDualRoleUtmi;
}

UINT32 UsbHostDualRoleDevicePhysicalPowerManagement(void) {
    return sDualRolePowerManagement;
}

static void StallMicroseconds(UINT32 Microseconds) {
    UINT64 Frequency;
    UINT64 Start;
    UINT64 Delta;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Frequency));
    if (Frequency == 0u) {
        UsbHostStallYield((UINTN)Microseconds * 100u);
        return;
    }
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Start));
    Delta = (Frequency * (UINT64)Microseconds) / 1000000ull;
    for (;;) {
        UINT64 Now;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Now));
        if ((Now - Start) >= Delta) {
            break;
        }
    }
}

/*
 * 返回编码（画 m）：
 * bit0＝0x3EB0 写1粘住；bit1＝0x3EB4 粘；bit2＝0x0704 粘；
 * bit4＝PMU 基址可读（非 0/全 F）。
 */
static UINT32 TryStickyEnable(UINT64 Address) {
    UINT32 Before;
    UINT32 After;

    Before = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, Before | POWER_MANAGEMENT_UNIT_PHYSICAL_ENABLE);
    __asm__ volatile("dsb sy" ::: "memory");
    After = UsbHostReadMmio32(Address);
    return ((After & POWER_MANAGEMENT_UNIT_PHYSICAL_ENABLE) != 0u) ? 1u : 0u;
}

static void EnablePowerManagementUnitPhysical(void) {
    UINT64 Base = POWER_MANAGEMENT_UNIT_PHYSICAL_BASE;
    UINT32 Probe;
    UINT32 Status = 0;

    Probe = UsbHostReadMmio32(Base);
    if (Probe != 0u && Probe != 0xFFFFFFFFu) {
        Status |= (1u << 4);
    }

    /* 对表 exynos_usbdrd_*_phy_isol(on=0)：写 mask bit＝解除 isol */
    if (TryStickyEnable(Base + POWER_MANAGEMENT_UNIT_USB_PHYSICAL_OFFSET) != 0u) {
        Status |= (1u << 0);
    }
    if (TryStickyEnable(Base + POWER_MANAGEMENT_UNIT_USB_PHYSICAL_OFFSET_DISPLAY_PORT) !=
        0u) {
        Status |= (1u << 1);
    }
    if (TryStickyEnable(Base + POWER_MANAGEMENT_UNIT_USB_PHYSICAL_OFFSET_LEGACY) != 0u) {
        Status |= (1u << 2);
    }

    sDualRolePowerManagement = Status;
}

int UsbHostDualRoleDevicePhysicalInitialize(void) {
    UINT64 Base = DUAL_ROLE_DEVICE_PHYSICAL_BASE;
    UINT32 LinkControl;
    UINT32 ClockReset;
    UINT32 UtmiControl;

    sDualRoleLastResultCode = 0;
    sDualRoleClockReset = 0;
    sDualRoleUtmi = 0;
    sDualRolePowerManagement = 0;

    HalSerialWriteShell("dr: blkcon\r\n");
    EnablePowerManagementUnitPhysical();

    LinkControl = UsbHostReadMmio32(Base + USB_CONNECTION_LINK_CONTROL_REGISTER);
    if (LinkControl == 0u || LinkControl == 0xFFFFFFFFu) {
        sDualRoleLastResultCode = 0x10u;
        HalSerialWriteShell("dr: dead\r\n");
        return -1;
    }

    /* 对表 exynos_usbcon_init_link：改位→500µs→写→再 500µs→force_qact */
    LinkControl |= LINK_CONTROL_DISABLE_IDENTIFIER_QUEUE_ACTIVE |
                   LINK_CONTROL_DISABLE_BVALID_QUEUE_ACTIVE |
                   LINK_CONTROL_DISABLE_VBUSVALID_QUEUE_ACTIVE |
                   LINK_CONTROL_DISABLE_LINK_GATE_QUEUE_ACTIVE;
    LinkControl &= ~LINK_CONTROL_FORCE_QUEUE_ACTIVE;
    StallMicroseconds(500u);
    UsbHostWriteMmio32(Base + USB_CONNECTION_LINK_CONTROL_REGISTER, LinkControl);
    StallMicroseconds(500u);

    LinkControl |= LINK_CONTROL_FORCE_QUEUE_ACTIVE;
    LinkControl = (LinkControl & ~LINK_CONTROL_BUS_FILTER_BYPASS_MASK) |
                  LINK_CONTROL_BUS_FILTER_BYPASS_HOST;
    LinkControl = (LinkControl & ~LINK_CONTROL_SELECT_START_OF_FRAME_MASK) |
                  LINK_CONTROL_SELECT_START_OF_FRAME_ZERO;
    UsbHostWriteMmio32(Base + USB_CONNECTION_LINK_CONTROL_REGISTER, LinkControl);

    ClockReset =
        UsbHostReadMmio32(Base + USB_CONNECTION_LINK_CLOCK_RESET_REGISTER);
    ClockReset |= LINK_CLOCK_RESET_SOFTWARE_RESET;
    UsbHostWriteMmio32(Base + USB_CONNECTION_LINK_CLOCK_RESET_REGISTER,
                       ClockReset);
    StallMicroseconds(10u);
    ClockReset &= ~LINK_CLOCK_RESET_SOFTWARE_RESET;
    UsbHostWriteMmio32(Base + USB_CONNECTION_LINK_CLOCK_RESET_REGISTER,
                       ClockReset);

    UtmiControl =
        UsbHostReadMmio32(Base + USB_CONNECTION_UTMI_CONTROL_REGISTER);
    UtmiControl |= UTMI_CONTROL_FORCE_B_VALID | UTMI_CONTROL_FORCE_VBUS_VALID;
    UsbHostWriteMmio32(Base + USB_CONNECTION_UTMI_CONTROL_REGISTER, UtmiControl);

    /* 未 enable USBDP：对表 disable_pipe3（detach + Phy1-only 阻校） */
    LinkControl =
        UsbHostReadMmio32(Base + USB_CONNECTION_LINK_CONTROL_REGISTER);
    LinkControl |= LINK_CONTROL_FORCE_PIPE_ENABLE |
                   LINK_CONTROL_FORCE_RECEIVE_ELECTRICAL_IDLE;
    LinkControl &= ~LINK_CONTROL_FORCE_PHYSICAL_STATUS;
    UsbHostWriteMmio32(Base + USB_CONNECTION_LINK_CONTROL_REGISTER, LinkControl);
    ClockReset =
        UsbHostReadMmio32(Base + USB_CONNECTION_LINK_CLOCK_RESET_REGISTER);
    ClockReset &= ~LINK_CLOCK_RESET_PIPE_CLOCK_SELECT;
    UsbHostWriteMmio32(Base + USB_CONNECTION_LINK_CLOCK_RESET_REGISTER,
                       ClockReset);
    {
        UINT32 HighSpeedMiscellaneous = UsbHostReadMmio32(
            Base + USB_CONNECTION_HIGH_SPEED_MISCELLANEOUS_REGISTER);
        HighSpeedMiscellaneous =
            (HighSpeedMiscellaneous &
             ~HIGH_SPEED_MISCELLANEOUS_SELECT_RESISTANCE_TUNE_MUX_MASK) |
            HIGH_SPEED_MISCELLANEOUS_SELECT_RESISTANCE_TUNE_MUX_PHYSICAL1_ONLY;
        HighSpeedMiscellaneous |= HIGH_SPEED_MISCELLANEOUS_SET_REQUEST_IN2;
        HighSpeedMiscellaneous &=
            ~(HIGH_SPEED_MISCELLANEOUS_SET_ACKNOWLEDGE_IN2 |
              HIGH_SPEED_MISCELLANEOUS_SET_REQUEST_IN1 |
              HIGH_SPEED_MISCELLANEOUS_SET_ACKNOWLEDGE_IN1);
        UsbHostWriteMmio32(
            Base + USB_CONNECTION_HIGH_SPEED_MISCELLANEOUS_REGISTER,
            HighSpeedMiscellaneous);
    }

    sDualRoleClockReset =
        UsbHostReadMmio32(Base + USB_CONNECTION_LINK_CLOCK_RESET_REGISTER);
    sDualRoleUtmi =
        UsbHostReadMmio32(Base + USB_CONNECTION_UTMI_CONTROL_REGISTER);
    if ((sDualRoleUtmi & (UTMI_CONTROL_FORCE_B_VALID |
                          UTMI_CONTROL_FORCE_VBUS_VALID)) !=
        (UTMI_CONTROL_FORCE_B_VALID | UTMI_CONTROL_FORCE_VBUS_VALID)) {
        sDualRoleLastResultCode = 0x11u;
        HalSerialWriteShell("dr: utmi\r\n");
        return -1;
    }

    sDualRoleLastResultCode = 1u;
    return 0;
}

#else

UINT8 UsbHostDualRoleDevicePhysicalLastResultCode(void) {
    return 0;
}

UINT32 UsbHostDualRoleDevicePhysicalClockReset(void) {
    return 0;
}

UINT32 UsbHostDualRoleDevicePhysicalUtmi(void) {
    return 0;
}

UINT32 UsbHostDualRoleDevicePhysicalPowerManagement(void) {
    return 0;
}

int UsbHostDualRoleDevicePhysicalInitialize(void) {
    return -1;
}

#endif
