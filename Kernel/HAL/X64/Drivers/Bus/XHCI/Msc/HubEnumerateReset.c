/*
 * HubEnumerateReset.c — hub 子口复位 / 掉电重上电 / 鼠口掉线（自 HubEnumerate.c 纯搬 · PR-NORM-11）
 *
 * HubResetChildForAddress：子口 Hot/Warm 到 Default，供 Address。
 * HubPowerCyclePort：与「再 Hot/Warm+Address」不同招（USB 端口电源控制）。
 * NoteMouseHubDropped：扫盘弄掉 hub 鼠口时清槽。
 */
#include "XHCI/Private.h"

/*
 * 子口复位到 Default，供 Address。成功则 *StOut / *SpeedOut 有效。
 * WarmOnly：仅链路 SS.Inactive 时由调用方置 1；勿在 USBTE 后当「再撞一次」。
 */
int HubResetChildForAddress(UINT8 Port, UINT32 *StOut, UINT8 *SpeedOut,
                                   int WarmOnly) {
    UINT32 St = 0;
    UINT8 Speed;
    int t;
    int Warm;
    int SawResetDone = 0;
    int MaxWait = HalCpuIsHypervisor() ? 50000 : 100;

    if (HubGetPortStatus(Port, &St) < 0) {
        return 0;
    }

    if (gHubSpeed >= 4) {
        UINT32 Pls = HUB_PORT_LINK_STATE(St);

        /*
         * 规格：Enabled 且链路非 U3 → Hot Reset；SS.Inactive 等才 Warm。
         * 48ca72f 对已 U0 口一律 BH：部分 hub 不置 C_BH，循环超时后仍见旧 PED
         * → 伪「warm ok」→ Address cc=0x04。
         */
        Warm = WarmOnly || (Pls == HUB_PLS_SS_INACTIVE);
        if (HubSetPortFeature(Port, Warm ? HUB_FEAT_BH_PORT_RESET
                                      : HUB_FEAT_PORT_RESET) < 0) {
            BootLogHex("Boot: MSC claim hub reset fail port=", Port, 2);
            return 0;
        }
        for (t = 0; t < MaxWait; t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                return 0;
            }
            if (St & HUB_C_PORT_RESET) {
                (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_RESET);
                SawResetDone = 1;
                break;
            }
            if (St & HUB_C_BH_PORT_RESET) {
                (void)HubClearPortFeature(Port, HUB_FEAT_C_BH_PORT_RESET);
                SawResetDone = 1;
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMilliseconds(5);
            }
        }
        if (!SawResetDone) {
            BootLogHex("Boot: MSC claim hub reset no C_ port=", Port, 2);
            BootLogHex("Boot: MSC claim hub st=", St, 4);
            return 0;
        }
        for (t = 0; t < MaxWait; t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                return 0;
            }
            if (St & HUB_C_PORT_CONNECTION) {
                (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_CONNECTION);
            }
            if (St & HUB_C_PORT_RESET) {
                (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_RESET);
            }
            if (St & HUB_C_BH_PORT_RESET) {
                (void)HubClearPortFeature(Port, HUB_FEAT_C_BH_PORT_RESET);
            }
            if ((St & HUB_PORT_ENABLE) && HubPortSpeed(St) == 4 &&
                HUB_PORT_LINK_STATE(St) == HUB_PLS_U0) {
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMilliseconds(5);
            }
        }
        Speed = HubPortSpeed(St);
        if (!(St & HUB_PORT_ENABLE) || Speed != 4 ||
            HUB_PORT_LINK_STATE(St) != HUB_PLS_U0) {
            BootLogHex("Boot: MSC claim hub not SS/U0 port=", Port, 2);
            BootLogHex("Boot: MSC claim hub st=", St, 4);
            BootLogHex("Boot: MSC claim hub speed=", Speed, 1);
            return 0;
        }
        BootLogHex(Warm ? "Boot: MSC claim hub warm ok st="
                        : "Boot: MSC claim hub hot ok st=",
                   St, 4);
    } else {
        if (HubSetPortFeature(Port, HUB_FEAT_PORT_RESET) < 0) {
            return 0;
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 80); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                return 0;
            }
            if (St & HUB_C_PORT_RESET) {
                (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_RESET);
                SawResetDone = 1;
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMilliseconds(5);
            }
        }
        if (!SawResetDone) {
            BootLogHex("Boot: MSC claim hub reset no C_ port=", Port, 2);
            return 0;
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 80); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                return 0;
            }
            if (St & HUB_C_PORT_CONNECTION) {
                (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_CONNECTION);
            }
            if (St & HUB_PORT_ENABLE) {
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMilliseconds(5);
            }
        }
        if (!(St & HUB_PORT_ENABLE)) {
            BootLogHex("Boot: MSC claim hub not en port=", Port, 2);
            return 0;
        }
        Speed = HubPortSpeed(St);
    }

    *StOut = St;
    *SpeedOut = Speed;
    return 1;
}

/*
 * PR-H-msc-4：hub 子口找 MSC（Bulk）；跳过已占用键/鼠子口。
 * Address → FinishClaim（SetConfig+Bulk，无 SCSI）。
 * 真机：先统一上电再扫，空口略等（U 盘上电慢 → 误判 hub empty）。
 * PR-BOOT-fast-2：Pass/空口等待收紧；仍保留一轮 rescan + 软退。
 */
/* 扫盘复位了鼠标口但槽号还在时，清掉槽，桌面初始化才会补绑。 */
void NoteMouseHubDropped(void) {
    UINT32 St = 0;

    if (gMouseSlotId == 0 || gMouseTtPort == 0) {
        return;
    }
    if (gMouseHubSlot != 0 && gMouseHubSlot != (UINT8)gHubSlotId) {
        return;
    }
    if (HubGetPortStatus(gMouseTtPort, &St) == 0 &&
        (St & HUB_PORT_CONNECTION) && (St & HUB_PORT_ENABLE)) {
        return;
    }
    BootLog("Boot: MSC hub dropped mouse\n");
    DisableSlot(gMouseSlotId);
}

/* 掉电再上电：与「再 Hot/Warm+Address」不同招（USB 端口电源控制）。 */
int HubPowerCyclePort(UINT8 Port) {
    int t;
    UINT32 St = 0;

    if (HubClearPortFeature(Port, HUB_FEAT_PORT_POWER) < 0) {
        return 0;
    }
    if (!HalCpuIsHypervisor()) {
        StallMilliseconds(100); /* 下电可见；勿当「多等一会碰运气」缩超时 */
    }
    if (HubSetPortFeature(Port, HUB_FEAT_PORT_POWER) < 0) {
        return 0;
    }
    if (!HalCpuIsHypervisor()) {
        StallMilliseconds(100);
    }
    for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 40); t++) {
        if (HubGetPortStatus(Port, &St) < 0) {
            return 0;
        }
        if (St & HUB_C_PORT_CONNECTION) {
            (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_CONNECTION);
        }
        if (St & HUB_PORT_CONNECTION) {
            BootLogHex("Boot: MSC claim hub power-cycle ok port=", Port, 2);
            return 1;
        }
        if (!HalCpuIsHypervisor()) {
            StallMilliseconds(5);
        }
    }
    BootLogHex("Boot: MSC claim hub power-cycle no conn port=", Port, 2);
    return 0;
}
