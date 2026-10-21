/*
 * Port.c — PR-H-xhci-split-3：端口复位 / 上电 / PORTSC
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 */
#include "XHCI/Private.h"

UINT32 PortReg(UINT32 Port1) {
    return 0x400 + (Port1 - 1) * 0x10;
}

UINT8 PortSpeed(UINT32 Portsc) {
    return (UINT8)((Portsc >> PORTSC_SPEED_SHIFT) & 0xF);
}

UINT32 PortscNeutral(UINT32 State) {
    return (State & PORTSC_RO) | (State & PORTSC_RWS);
}

/*
 * 清 PORTSC 变更位（W1C）— 仅 QEMU/virt 路径使用。
 * 真机照片两轮：Neutral 清法与 SeaBIOS(PED|PP|CHANGE) 清法都会在
 * PED 已置位后把口打回 0x6E1/0xAE1（Polling）；故真机 ResetPort 不清变更。
 */
void PortscClearChange(UINT64 Ps) {
    UINT32 Val = ReadMmio32(Ps);
    WriteMmio32(Ps, PORTSC_PED | PORTSC_PP | (Val & PORTSC_CHANGE));
    Fence();
}

/*
 * 已连接口上电。
 * 刀1/刀2（笔记本 CCS=0，不动 NUC/工控已成功路径）：
 *   - 已有 CCS：行为与旧相同（真机仅 Stall 50ms）。
 *   - 全 0：PP all → 打 maxports → 多轮等待复扫 → 仍 0 则打前几口 PORTSC。
 */
void PowerConnectedPorts(void) {
    UINT32 p;
    UINT32 Surveyed = 0;
    int RealPc = !HalCpuIsHypervisor();
    int Round;
    UINT32 DumpLimit;

    for (p = 1; p <= gMaxPorts && p <= 32; p++) {
        UINT64 Ps = gOperationalBase + PortReg(p);
        UINT32 Val = ReadMmio32(Ps);
        if (Val & PORTSC_CCS) {
            Surveyed++;
            if (!(Val & PORTSC_PP)) {
                WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
            }
        }
    }
    if (Surveyed > 0) {
        /* NUC/工控：口上已有设备，保持原短等待 */
        if (RealPc) {
            StallMs(50);
        } else {
            volatile int D;
            for (D = 0; D < 50000; D++) {
            }
        }
        return;
    }

    BootLog("Boot: XHCI CCS=0, PP all\n");
    BootLogHex("Boot: XHCI maxports=", gMaxPorts, 2);
    for (p = 1; p <= gMaxPorts && p <= 32; p++) {
        UINT64 Ps = gOperationalBase + PortReg(p);
        UINT32 Val = ReadMmio32(Ps);
        if (!(Val & PORTSC_PP)) {
            WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        }
    }

    if (!RealPc) {
        volatile int D;
        for (D = 0; D < 50000; D++) {
        }
        return;
    }

    /* 仅无 CCS：加长等待（USB3 口挂 USB2 鼠常见） */
    for (Round = 0; Round < 8; Round++) {
        StallMs(100);
        Surveyed = 0;
        for (p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT64 Ps = gOperationalBase + PortReg(p);
            UINT32 Val = ReadMmio32(Ps);
            if (!(Val & PORTSC_PP)) {
                WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
                Val = ReadMmio32(Ps);
            }
            if (Val & PORTSC_CCS) {
                Surveyed++;
            }
        }
        if (Surveyed > 0) {
            BootLogHex("Boot: XHCI CCS after wait=", Surveyed, 2);
            return;
        }
    }

    /* 普查已扫完全部口仍无 CCS；打齐最多 8 口 PORTSC（Intel 常 USB2/3 分口编号） */
    DumpLimit = gMaxPorts;
    if (DumpLimit > 8) {
        DumpLimit = 8;
    }
    for (p = 1; p <= DumpLimit; p++) {
        UINT32 Val = ReadMmio32(gOperationalBase + PortReg(p));
        char Pref[32];
        int n = 0;
        const char *S = "Boot: XHCI PORTSC";
        while (*S && n < 24) {
            Pref[n++] = *S++;
        }
        if (p >= 10) {
            Pref[n++] = (char)('0' + (p / 10));
            Pref[n++] = (char)('0' + (p % 10));
        } else {
            Pref[n++] = (char)('0' + p);
        }
        Pref[n++] = '=';
        Pref[n] = 0;
        BootLogHex(Pref, Val, 8);
    }
}
/*
 * 标准化端口复位（xHCI）：
 * USB2：PP → CCS → PR → 等 PRC → PED+CCS。USB3：WPR。
 * Force=0 且已 PED+CCS：跳过。成功后 leavePRC。
 * Force=1：须 PR/WPR 回 Default（软 Address USBTE cc=0x04）。
 *
 * NUC 证据链（勿再踩）：
 *   0x00200E03 Why=force dis PED → 写 PED=0 忽略；
 *   0x002006E1 Why=not PED → PED 上 W1C CHANGE → 卡 Polling；
 *   0x002006E1 Why=reset timeout → 单次 PR 进 Polling 后 500ms 内 PED 不回。
 * 索赔注释成功路径是 Force !PED。故 Force+PED：先 PR（不清 CHANGE）把
 * PED 打掉（常变 0x6E1），再在 !PED 上清 sticky、第二次 PR/WPR——走 !PED 成功路径。
 * Speed 以跌 PED 前为准（Polling 时 Speed 字段不可靠）。
 */
/*
 * Force=0：真机已 PED+CCS 则跳过 PR（同 pass 内二次 PR 易打坏口）。
 * Force=1：DisableSlot / soft Address USBTE 后须复位到 Default（否则 cc=0x04）。
 */
int ResetPortEx(UINT32 Port1, int Force) {
    UINT64 Ps = gOperationalBase + PortReg(Port1);
    UINT32 Val = ReadMmio32(Ps);
    UINT32 SpeedHint = PortSpeed(Val);
    UINT32 Speed;
    UINT32 Pls;
    UINT32 SpdSave;
    UINT32 ResetBit;
    int RealPc = !HalCpuIsHypervisor();
    int UseWpr;
    int i;
    int t;
    int Ok;

    DiagChk("ResetPort.enter", 1, "PORTSC", Val, 8);

    if (RealPc && !Force && (Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        DiagChk("ResetPort.already", 1, "PED+CCS skip PR", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        return 1;
    }
    if (RealPc && Force && (Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        BootLogHexV("Boot: XHCI port PR force=", Port1, 2);
    }

    /* 端口上电（勿在已连接时清 PED） */
    if (!(Val & PORTSC_PP)) {
        WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        Ok = RealPc ? WaitSetMs(Ps, PORTSC_PP, 50) : WaitSet(Ps, PORTSC_PP, 30000);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PP", Ok && (Val & PORTSC_PP), "PP=1", Val, 8);
        if (!Ok) {
            EnumWhy("Boot: Why=PP timeout\n");
            return 0;
        }
    }

    if (!(Val & PORTSC_CCS)) {
        if (RealPc) {
            for (i = 0; i < 50; i++) {
                StallMs(10);
                Val = ReadMmio32(Ps);
                if (Val & PORTSC_CCS) {
                    break;
                }
            }
        } else {
            for (i = 0; i < 50000; i++) {
                Val = ReadMmio32(Ps);
                if (Val & PORTSC_CCS) {
                    break;
                }
            }
        }
        DiagChk("ResetPort.CCS", !!(Val & PORTSC_CCS), "CCS=1", Val, 8);
        if (!(Val & PORTSC_CCS)) {
            return 0;
        }
    }

    /*
     * 真机 Force：若仍 PED，先一记 PR/WPR（不清 CHANGE）把 PED 打掉，
     * 再落入下面与「Force !PED」相同的清 sticky → 再复位。
     */
    if (RealPc && Force && (Val & PORTSC_PED)) {
        SpdSave = PortSpeed(Val);
        Pls = (Val >> 5) & 0xFu;
        UseWpr = (SpdSave >= 4) || (SpdSave == 0) || (Pls == 6u);
        ResetBit = UseWpr ? PORTSC_WPR : PORTSC_PR;
        WriteMmio32(Ps, PortscNeutral(Val) | ResetBit | PORTSC_PP);
        Fence();
        for (t = 0; t < 500 && (ReadMmio32(Ps) & PORTSC_PED); t++) {
            StallMs(1);
        }
        /* 等 PR/WPR 自清，避免第二记叠在第一记上 */
        for (t = 0; t < 100 && (ReadMmio32(Ps) & ResetBit); t++) {
            StallMs(1);
        }
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.knock", !(Val & PORTSC_PED) && !!(Val & PORTSC_CCS),
                "!PED+CCS", Val, 8);
        if (Val & PORTSC_PED) {
            EnumWhy("Boot: Why=force still PED\n");
            return 0;
        }
        if (!(Val & PORTSC_CCS)) {
            EnumWhy("Boot: Why=lost CCS\n");
            return 0;
        }
        SpeedHint = SpdSave; /* Polling 时 Speed 不可靠 */
    }

    /* PED=0 时清 sticky 变更安全；便于等 PRC 边沿 */
    if (RealPc && !(Val & PORTSC_PED) && (Val & PORTSC_CHANGE)) {
        WriteMmio32(Ps, PortscNeutral(Val) | (Val & PORTSC_CHANGE) | PORTSC_PP);
        Fence();
        StallMs(5);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.clrSticky", !(Val & PORTSC_PRC), "PRC=0", Val, 8);
    }

    if (!(RealPc && Force && SpeedHint != 0)) {
        SpeedHint = PortSpeed(Val);
    }
    Pls = (Val >> 5) & 0xFu;
    DiagChk("ResetPort.speed", SpeedHint != 0, "spd!=0", SpeedHint, 2);

    UseWpr = (SpeedHint >= 4) || (Force && SpeedHint == 0) || (Pls == 6u);
    Val = PortscNeutral(ReadMmio32(Ps));
    if (UseWpr) {
        WriteMmio32(Ps, Val | PORTSC_WPR | PORTSC_PP);
    } else {
        WriteMmio32(Ps, Val | PORTSC_PR | PORTSC_PP);
    }
    Fence();

    if (RealPc) {
        /*
         * 照片：PED OK → afterClr → Polling。成功后 leavePRC。
         * 现已保证写 PR 前 PED=0 且 sticky 已清，可等 PRC|WRC 边沿。
         */
        Ok = WaitSetMs(Ps, PORTSC_PRC | PORTSC_WRC, 500);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PRC", Ok, "PRC|WRC", Val, 8);
        if (!Ok) {
            EnumWhy("Boot: Why=reset timeout\n");
            return 0;
        }
        if (!(Val & PORTSC_PED)) {
            Ok = WaitSetMs(Ps, PORTSC_PED, Force ? 2000 : 1000);
            Val = ReadMmio32(Ps);
        }
        DiagChk("ResetPort.PED", (Val & PORTSC_PED) && (Val & PORTSC_CCS),
                "PED+CCS", Val, 8);
        if (!(Val & PORTSC_PED) || !(Val & PORTSC_CCS)) {
            if (!(Val & PORTSC_CCS)) {
                EnumWhy("Boot: Why=lost CCS\n");
            } else {
                EnumWhy("Boot: Why=not PED\n");
            }
            return 0;
        }
        DiagChk("ResetPort.leavePRC", 1, "sticky PRC", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        StallMs(50);
        return 1;
    }

    Ok = WaitSet(Ps, PORTSC_PRC | PORTSC_WRC, 60000);
    Val = ReadMmio32(Ps);
    DiagChk("ResetPort.PRC", Ok, "PRC|WRC", Val, 8);
    if (!Ok) {
        return 0;
    }
    if (!(Val & PORTSC_PED)) {
        for (t = 0; t < 30000; t++) {
            Val = ReadMmio32(Ps);
            if (Val & PORTSC_PED) {
                break;
            }
        }
    }
    PortscClearChange(Ps);
    for (t = 0; t < 30000; t++) {
        Val = ReadMmio32(Ps);
        if ((Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
            DiagChk("ResetPort.done", 1, "PED+CCS", Val, 8);
            return 1;
        }
    }
    DiagChk("ResetPort.PED", 0, "PED+CCS", ReadMmio32(Ps), 8);
    return 0;
}

int ResetPort(UINT32 Port1) {
    return ResetPortEx(Port1, 0);
}
