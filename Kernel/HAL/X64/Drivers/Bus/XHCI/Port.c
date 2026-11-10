/*
 * Port.c — PR-H-xhci-split-3：PORTSC 工具 / 已连接口上电
 *
 * ResetPortEx / ResetPort → PortReset.c（PR-NORM-11）。
 */
#include "XHCI/Private.h"

UINT32 PortRegister(UINT32 Port1) {
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
        UINT64 Ps = gOperationalBase + PortRegister(p);
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
            StallMilliseconds(50);
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
        UINT64 Ps = gOperationalBase + PortRegister(p);
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
        StallMilliseconds(100);
        Surveyed = 0;
        for (p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT64 Ps = gOperationalBase + PortRegister(p);
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
        UINT32 Val = ReadMmio32(gOperationalBase + PortRegister(p));
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
