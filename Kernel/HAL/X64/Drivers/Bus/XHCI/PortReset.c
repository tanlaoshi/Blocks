/*
 * PortReset.c — ResetPortEx / ResetPort（自 Port.c 纯搬 · PR-NORM-11）
 *
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
 *
 * Force=0：真机已 PED+CCS 则跳过 PR（同 pass 内二次 PR 易打坏口）。
 * Force=1：DisableSlot / soft Address USBTE 后须复位到 Default（否则 cc=0x04）。
 */
#include "XHCI/Private.h"

int ResetPortEx(UINT32 Port1, int Force) {
    UINT64 Ps = gOperationalBase + PortRegister(Port1);
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
        Ok = RealPc ? WaitSetMilliseconds(Ps, PORTSC_PP, 50) : WaitSet(Ps, PORTSC_PP, 30000);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PP", Ok && (Val & PORTSC_PP), "PP=1", Val, 8);
        if (!Ok) {
            EnumerateWhy("Boot: Why=PP timeout\n");
            return 0;
        }
    }

    if (!(Val & PORTSC_CCS)) {
        if (RealPc) {
            for (i = 0; i < 50; i++) {
                StallMilliseconds(10);
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
            StallMilliseconds(1);
        }
        /* 等 PR/WPR 自清，避免第二记叠在第一记上 */
        for (t = 0; t < 100 && (ReadMmio32(Ps) & ResetBit); t++) {
            StallMilliseconds(1);
        }
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.knock", !(Val & PORTSC_PED) && !!(Val & PORTSC_CCS),
                "!PED+CCS", Val, 8);
        if (Val & PORTSC_PED) {
            EnumerateWhy("Boot: Why=force still PED\n");
            return 0;
        }
        if (!(Val & PORTSC_CCS)) {
            EnumerateWhy("Boot: Why=lost CCS\n");
            return 0;
        }
        SpeedHint = SpdSave; /* Polling 时 Speed 不可靠 */
    }

    /* PED=0 时清 sticky 变更安全；便于等 PRC 边沿 */
    if (RealPc && !(Val & PORTSC_PED) && (Val & PORTSC_CHANGE)) {
        WriteMmio32(Ps, PortscNeutral(Val) | (Val & PORTSC_CHANGE) | PORTSC_PP);
        Fence();
        StallMilliseconds(5);
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
        Ok = WaitSetMilliseconds(Ps, PORTSC_PRC | PORTSC_WRC, 500);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PRC", Ok, "PRC|WRC", Val, 8);
        if (!Ok) {
            EnumerateWhy("Boot: Why=reset timeout\n");
            return 0;
        }
        if (!(Val & PORTSC_PED)) {
            Ok = WaitSetMilliseconds(Ps, PORTSC_PED, Force ? 2000 : 1000);
            Val = ReadMmio32(Ps);
        }
        DiagChk("ResetPort.PED", (Val & PORTSC_PED) && (Val & PORTSC_CCS),
                "PED+CCS", Val, 8);
        if (!(Val & PORTSC_PED) || !(Val & PORTSC_CCS)) {
            if (!(Val & PORTSC_CCS)) {
                EnumerateWhy("Boot: Why=lost CCS\n");
            } else {
                EnumerateWhy("Boot: Why=not PED\n");
            }
            return 0;
        }
        DiagChk("ResetPort.leavePRC", 1, "sticky PRC", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        StallMilliseconds(50);
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
