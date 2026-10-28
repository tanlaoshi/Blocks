/*
 * CoreControl.c — AI 核 disable/reset（对表 Linux brcmf_chip_ai_*）
 */
#include "Wifi/Private.h"

int WifiCoreDisable(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits) {
    UINT32 Value = 0;
    UINT32 Attempt;

    if (WifiBackplaneRead32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, &Value) != 0) {
        return -1;
    }
    if ((Value & WIFI_AI_RESET_BIT) != 0) {
        return 0;
    }
    if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET,
                             PreResetBits | WIFI_SICF_FORCE_GATED_CLOCK |
                                 WIFI_SICF_CLOCK_ENABLE) != 0) {
        return -1;
    }
    (void)WifiBackplaneRead32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET, &Value);
    if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, WIFI_AI_RESET_BIT) !=
        0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 300u; Attempt++) {
        if (WifiBackplaneRead32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, &Value) != 0) {
            return -1;
        }
        if ((Value & WIFI_AI_RESET_BIT) != 0) {
            break;
        }
        SdhciDelayMicroseconds(10);
    }
    if ((Value & WIFI_AI_RESET_BIT) == 0) {
        return -1;
    }
    if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET,
                             ResetBits | WIFI_SICF_FORCE_GATED_CLOCK |
                                 WIFI_SICF_CLOCK_ENABLE) != 0) {
        return -1;
    }
    (void)WifiBackplaneRead32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET, &Value);
    return 0;
}

int WifiCoreReset(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits,
                  UINT32 PostResetBits) {
    UINT32 Value = 0;
    UINT32 Attempt;

    if (WifiCoreDisable(WrapperBase, PreResetBits, ResetBits) != 0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 50u; Attempt++) {
        if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, 0) != 0) {
            return -1;
        }
        SdhciDelayMicroseconds(40);
        if (WifiBackplaneRead32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, &Value) != 0) {
            return -1;
        }
        if ((Value & WIFI_AI_RESET_BIT) == 0) {
            break;
        }
    }
    if ((Value & WIFI_AI_RESET_BIT) != 0) {
        return -1;
    }
    if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET,
                             PostResetBits | WIFI_SICF_CLOCK_ENABLE) != 0) {
        return -1;
    }
    (void)WifiBackplaneRead32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET, &Value);
    SdhciDelayMicroseconds(1000);
    return 0;
}

int WifiCoreResetWritesOnly(UINT32 WrapperBase, UINT32 PreResetBits, UINT32 ResetBits,
                            UINT32 PostResetBits) {
    /*
     * EnterDownload 已 disable ARM。对表 Linux ai_resetcore 出复位（跳过读回）：
     *   ioctl = reset|FGC|CLK → 清 RESET（多写几次）→ ioctl = post|CLK。
     * 勿再整段 disable（曾对表 cyw43 重断言 RESET，易把已灌状态打毛）。
     */
    UINT32 Attempt;

    (void)PreResetBits;
    if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET,
                             ResetBits | WIFI_SICF_FORCE_GATED_CLOCK |
                                 WIFI_SICF_CLOCK_ENABLE) != 0) {
        return -1;
    }
    SdhciDelayMicroseconds(10);
    for (Attempt = 0; Attempt < 50u; Attempt++) {
        if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, 0) != 0) {
            return -1;
        }
        SdhciDelayMicroseconds(40);
    }
    if (WifiBackplaneWrite32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET,
                             PostResetBits | WIFI_SICF_CLOCK_ENABLE) != 0) {
        return -1;
    }
    SdhciDelayMicroseconds(1000);
    return 0;
}

int WifiCoreIsUp(UINT32 WrapperBase) {
    UINT32 IoControl = 0;
    UINT32 ResetControl = 0;

    if (WifiBackplaneRead32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET, &IoControl) != 0) {
        return 0;
    }
    if (WifiBackplaneRead32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, &ResetControl) != 0) {
        return 0;
    }
    if ((IoControl & (WIFI_SICF_FORCE_GATED_CLOCK | WIFI_SICF_CLOCK_ENABLE)) !=
        WIFI_SICF_CLOCK_ENABLE) {
        return 0;
    }
    if ((ResetControl & WIFI_AI_RESET_BIT) != 0) {
        return 0;
    }
    return 1;
}
