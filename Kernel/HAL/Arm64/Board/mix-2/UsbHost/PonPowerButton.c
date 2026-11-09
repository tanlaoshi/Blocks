/*
 * UsbHost/PonPowerButton.c — PR-PHONE-PWR-0：短按 KPDPWR → fastboot
 *
 * 对表 Linux qcom-pon / qpnp-pon / pm8941-pwrkey（pm8998）：
 *   PON@0x800 SID0；RT_STS bit0=KPDPWR_N_SET（按下=1）；
 *   SOFT_RB_SPARE bootloader=0x2<<1（GEN2）；
 *   PS_HOLD_RST 温复位后拉低 msm PS_HOLD@0x10AC000。
 * 长按硬关仍由 PMIC Stage3/硬件路径，本文件不改。
 *
 * 真机约束：Handoff 停在 HubWatch，Tasks 不到；长 Stall 须走
 * UsbHostStallYieldPumpPower，禁止在 SpmiWaitDone 内泵键（嵌套 SPMI）。
 */
#include "Board.h"
#include "Hal.h"
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define PM8998_SLAVE_IDENTIFIER 0u
#define POWER_ON_BASE           0x800u
#define POWER_ON_REAL_TIME_STATUS (POWER_ON_BASE + 0x10u)
#define POWER_ON_KEY_POWER_S2_CONTROL2 (POWER_ON_BASE + 0x43u)
#define POWER_ON_POWER_SUPPLY_HOLD_RESET_CONTROL (POWER_ON_BASE + 0x5Au)
#define POWER_ON_POWER_SUPPLY_HOLD_RESET_CONTROL2 (POWER_ON_BASE + 0x5Bu)
#define POWER_ON_SOFT_RESET_SPARE (POWER_ON_BASE + 0x8Fu)
#define POWER_ON_SECURE_ACCESS (POWER_ON_BASE + 0xD0u)

#define POWER_ON_KEY_POWER_STATUS_BIT (1u << 0)
#define POWER_ON_S2_RESET_ENABLE (1u << 7)
#define POWER_ON_RESET_ENABLE (1u << 7)
#define POWER_ON_POWER_OFF_MASK 0x0Fu
#define POWER_ON_POWER_OFF_WARM_RESET 0x01u
#define POWER_ON_SECURE_UNLOCK 0xA5u
#define POWER_ON_RESTART_REASON_BOOTLOADER 0x02u
#define POWER_ON_RESTART_REASON_SHIFT 1u
#define POWER_ON_RESTART_REASON_MASK 0xFEu

#define POWER_SUPPLY_HOLD_PHYSICAL 0x010AC000ULL
#define POWER_ON_DIAGNOSTIC_SPMI_FAIL (1u << 31)

static int sPowerOnArmed;
static int sStage2ResetDisarmed;
static int sSpmiArbiterReady;
static int sRebootInProgress;
static UINT32 sPowerOnStatusDiagnostic = POWER_ON_DIAGNOSTIC_SPMI_FAIL;

static void Mix2PowerOnSecureUnlock(void) {
    (void)UsbHostSpmiWriteByte(PM8998_SLAVE_IDENTIFIER, POWER_ON_SECURE_ACCESS,
                                POWER_ON_SECURE_UNLOCK);
}

static int Mix2PowerOnRead(UINT16 Address, UINT8 *ValueOut) {
    return UsbHostSpmiReadByte(PM8998_SLAVE_IDENTIFIER, Address, ValueOut);
}

static int Mix2PowerOnWrite(UINT16 Address, UINT8 Value) {
    return UsbHostSpmiWriteByte(PM8998_SLAVE_IDENTIFIER, Address, Value);
}

static int Mix2PowerOnMaskedWrite(UINT16 Address, UINT8 Mask, UINT8 Value) {
    UINT8 Current = 0;

    if (Mix2PowerOnRead(Address, &Current) != 0) {
        return -1;
    }
    Current = (UINT8)((Current & (UINT8)~Mask) | (Value & Mask));
    return Mix2PowerOnWrite(Address, Current);
}

static void Mix2PowerOnDisarmKeyPowerStage2Reset(void) {
    UINT8 Control = 0;

    if (sStage2ResetDisarmed != 0) {
        return;
    }
    /* 短按勿走 PMIC Stage2 关机；长按硬关仍可用 Stage3/硬件 */
    Mix2PowerOnSecureUnlock();
    if (Mix2PowerOnRead(POWER_ON_KEY_POWER_S2_CONTROL2, &Control) == 0) {
        (void)Mix2PowerOnWrite(POWER_ON_KEY_POWER_S2_CONTROL2,
                               (UINT8)(Control & (UINT8)~POWER_ON_S2_RESET_ENABLE));
        sStage2ResetDisarmed = 1;
    }
}

static int Mix2PowerOnKeyIsPressed(void) {
    UINT8 Status = 0;

    if (Mix2PowerOnRead(POWER_ON_REAL_TIME_STATUS, &Status) != 0) {
        sPowerOnStatusDiagnostic = POWER_ON_DIAGNOSTIC_SPMI_FAIL;
        return 0;
    }
    sPowerOnStatusDiagnostic = Status;
    return (Status & POWER_ON_KEY_POWER_STATUS_BIT) != 0 ? 1 : 0;
}

static void Mix2PowerOnWriteRestartReasonBootloader(void) {
    UINT8 Spare = 0;
    UINT8 ReasonField =
        (UINT8)(POWER_ON_RESTART_REASON_BOOTLOADER << POWER_ON_RESTART_REASON_SHIFT);

    Mix2PowerOnSecureUnlock();
    if (Mix2PowerOnRead(POWER_ON_SOFT_RESET_SPARE, &Spare) != 0) {
        Spare = 0;
    }
    Spare = (UINT8)((Spare & (UINT8)~POWER_ON_RESTART_REASON_MASK) | ReasonField);
    (void)Mix2PowerOnWrite(POWER_ON_SOFT_RESET_SPARE, Spare);
}

static void Mix2PowerOnConfigureWarmReset(void) {
    /*
     * 对表 qpnp_pon_reset_config / pm8941_reboot_notify：
     * 先关 RST_EN（CTL2），改 CTL 类型，再开 RST_EN；中间须等数个 sleep cycle。
     */
    Mix2PowerOnSecureUnlock();
    (void)Mix2PowerOnMaskedWrite(POWER_ON_POWER_SUPPLY_HOLD_RESET_CONTROL2,
                                 POWER_ON_RESET_ENABLE, 0);
    UsbHostStallYield(200000u);
    Mix2PowerOnSecureUnlock();
    (void)Mix2PowerOnMaskedWrite(POWER_ON_POWER_SUPPLY_HOLD_RESET_CONTROL,
                                 POWER_ON_POWER_OFF_MASK, POWER_ON_POWER_OFF_WARM_RESET);
    Mix2PowerOnSecureUnlock();
    (void)Mix2PowerOnMaskedWrite(POWER_ON_POWER_SUPPLY_HOLD_RESET_CONTROL2,
                                 POWER_ON_RESET_ENABLE, POWER_ON_RESET_ENABLE);
}

static void Mix2PowerOnDropPowerSupplyHold(void) {
    volatile UINT32 *PowerSupplyHold =
        (volatile UINT32 *)(UINTN)POWER_SUPPLY_HOLD_PHYSICAL;

    *PowerSupplyHold = 0;
    __asm__ volatile("dsb sy" ::: "memory");
}

static void Mix2RebootToBootloader(void) {
    if (sRebootInProgress != 0) {
        return;
    }
    sRebootInProgress = 1;
    (void)BoardBringUpSplashPaintLine(112, "pwr fastboot");
    Mix2PowerOnWriteRestartReasonBootloader();
    Mix2PowerOnConfigureWarmReset();
    /* 纯 Stall：勿 PumpPower（会再进本函数） */
    UsbHostStallYield(2000000u);
    Mix2PowerOnDropPowerSupplyHold();
    for (;;) {
        __asm__ volatile("wfi");
    }
}

UINT32 UsbHostPowerOnStatusDiagnostic(void) {
    return sPowerOnStatusDiagnostic;
}

int BoardPowerButtonPressed(void) {
    int Pressed;

    if (sRebootInProgress != 0) {
        return 0;
    }
    if (sSpmiArbiterReady == 0) {
        if (UsbHostSpmiArbiterProbe() != 0) {
            sPowerOnStatusDiagnostic = POWER_ON_DIAGNOSTIC_SPMI_FAIL;
            return 0;
        }
        sSpmiArbiterReady = 1;
    }
    Mix2PowerOnDisarmKeyPowerStage2Reset();
    Pressed = Mix2PowerOnKeyIsPressed();
    if (sPowerOnArmed == 0) {
        sPowerOnArmed = 1;
        return 0;
    }
    /*
     * 电平触发（非仅上升沿）：HubWatch / 复位长等若只采样边沿，
     * 短按会整段落在两次采样之间 → 「短按无用」。
     */
    if (Pressed != 0) {
        Mix2RebootToBootloader();
    }
    return 0;
}

#else

UINT32 UsbHostPowerOnStatusDiagnostic(void) {
    return 0;
}

int BoardPowerButtonPressed(void) {
    return 0;
}

#endif
