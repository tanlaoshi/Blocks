/*
 * Msc.c — xHCI MSC 核心（usb-map / PR-S-xhcimsc-1）
 *
 * Scan：只读 PORTSC 清点（禁 Address/Disable——真机易命令环 sick）。
 * Claim→ClaimPorts；Bulk/BOT→MscBulk/BulkOnly；事件环仍 Xhci.c。
 * 门面：UsbMsc.c（开机 Auto / HotPoll）。全局 BSS 仍 Xhci.c。
 */
#include "XHCI/Private.h"
#include "DriverBoot.h"
#include "Msc/UsbMscHost.h"

/*
 * PR-H-msc-2/4：Bulk 环 Init；claim 后 Ready=1（仍无 SCSI）。
 */
int BringUp(void) {
    if (!gMscBulkRingsInited) {
        InitRing(gBulkInRing, &gBulkIn, RING_SIZE);
        InitRing(gBulkOutRing, &gBulkOut, RING_SIZE);
        FlushDma(gBulkInRing, sizeof(gBulkInRing));
        FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
        gMscBulkRingsInited = 1;
    }
    return gMscClaimed ? 0 : -1;
}

static int Ready(void) {
    return gMscClaimed ? 1 : 0;
}

static UINT32 BlockCount(void) {
    return gMscCapacityOk ? gMscBlockCount : 0;
}

static UINT32 BlockSize(void) {
    return gMscCapacityOk ? gMscBlockSize : 0;
}

/*
 * PR-H-msc-3（热修）：只读根口 PORTSC 清点候选，**禁止 Address/Disable**。
 * NUC：未 Force PR 的 PED 口（如 0x11）Address 命令超时 → 命令环 sick →
 * MSI irq-stall → 鼠标假死。class/VID 留给 msc claim（可 Force PR）。
 */
static int ScanPorts(void) {
    UINT32 P;
    int Found = 0;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        BootLog("Boot: MSC scan no hc\n");
        return -1;
    }

    BootLog("Boot: MSC scan begin (portsc only)\n");

    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(P));
        UINT8 Speed;

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (gSlotId != 0 && P == gPort1) {
            BootLogHex("Boot: MSC scan skip kbd port=", P, 2);
            continue;
        }
        if (gMouseSlotId != 0 && P == gMousePort) {
            BootLogHex("Boot: MSC scan skip mouse port=", P, 2);
            continue;
        }
        if (gHubSlotId != 0 && P == gHubRootPort) {
            UINT8 Hp;
            UINT8 MaxP = gHubNumPorts;

            /* 根口是 HID hub：看子口 CONNECTION（键鼠口除外），供桌面热插 Scan */
            for (Hp = 1; MaxP != 0 && Hp <= MaxP; Hp++) {
                UINT32 St = 0;

                if ((gKbdRoute & 0xFu) == (UINT32)Hp && gSlotId != 0) {
                    continue;
                }
                if ((gMouseRoute & 0xFu) == (UINT32)Hp && gMouseSlotId != 0) {
                    continue;
                }
                if (gMouseSlotId != 0 && gMouseTtPort == Hp &&
                    (gMouseHubSlot == 0 || gMouseHubSlot == (UINT8)gHubSlotId)) {
                    continue;
                }
                if (gMscClaimed && gMscHubSlot == (UINT8)gHubSlotId &&
                    gMscTtPort == Hp) {
                    continue;
                }
                if (HubGetPortStatus(Hp, &St) < 0) {
                    continue;
                }
                if (St & HUB_PORT_CONNECTION) {
                    BootLogHex("Boot: MSC scan hub child=", Hp, 2);
                    Found++;
                }
            }
            continue;
        }
        if (gMscClaimed && P == gMscPort) {
            BootLogHex("Boot: MSC scan skip claimed port=", P, 2);
            continue;
        }

        Speed = PortSpeed(Ps);
        BootLogHex("Boot: MSC scan port=", P, 2);
        BootLogHex("Boot: MSC scan speed=", Speed, 1);
        BootLogHex("Boot: MSC scan ped=", (Ps & PORTSC_PED) ? 1u : 0u, 1);
        BootLogHex("Boot: MSC scan portsc=", Ps, 8);
        /*
         * usb-hid-keep：根口 FS/LS 不当 MSC 候选（NUC 口8 蓝牙 0xE0=FS；
         * HotPoll 每 5s Force 会弄死邻口鼠）。U 盘走 HS/SS。
         */
        if (!HalCpuIsHypervisor() && Speed > 0 && Speed < 3) {
            BootLog("Boot: MSC scan skip FS/LS root (not MSC)\n");
            gPortNeedForcePr &= ~(1u << P);
            continue;
        }
        if (!(Ps & PORTSC_PED)) {
            BootLog("Boot: MSC scan note: claim will Force PR\n");
            gPortNeedForcePr |= (1u << P);
        }
        Found++;
    }

    BootLogHex("Boot: MSC scan done n=", (UINT32)Found, 2);
    return Found;
}

/*
 * PR-H-msc-hot：DisableSlot 清 gMsc*（见 Device.c）。
 */
static int Release(void) {
    if (gMscScanSlot != 0) {
        DisableSlot(gMscScanSlot);
    } else {
        gMscClaimed = 0;
        gMscPort = 0;
        gMscRoute = 0;
        gMscHubSlot = 0;
        gMscTtPort = 0;
        gMscBulkInDci = 0;
        gMscBulkOutDci = 0;
        gMscCapacityOk = 0;
        gMscBlockCount = 0;
        gMscBlockSize = 0;
    }
    BootLog("Boot: MSC release\n");
    return 0;
}

/*
 * 根口：PORTSC.CCS。hub 子设备：HubGetPortStatus(CONNECTION)
 *（仅当 gMscHubSlot==当前 HID hub，第二 hub 场景 Shell 手动 release）。
 * 调用方 HotPoll 须节流：2Hz GetPortStatus 会抢 HID 环（空拖规律顿）。
 */
static int Present(void) {
    UINT32 Ps;
    UINT32 St;

    if (!gMscClaimed) {
        return 0;
    }
    if (!gXhciStarted || gOperationalBase == 0) {
        return 0;
    }
    if (gMscHubSlot != 0 && gMscTtPort != 0) {
        if (gMscHubSlot != (UINT8)gHubSlotId || gHubSlotId == 0) {
            /* 父 hub 非当前 gHub*：勿乱 HubCtrl；当仍在 */
            return 1;
        }
        /*
         * BOT 进行中勿 HubGetPortStatus：与 Bulk 抢事件环/EP0 → STALL/Babble。
         * 忙则假定仍在位（拔出下个空闲 tick 再确认）。
         */
        if (__sync_lock_test_and_set(&gMscBotBusy, 1u) != 0) {
            return 1;
        }
        if (HubGetPortStatus(gMscTtPort, &St) < 0) {
            __sync_lock_release(&gMscBotBusy);
            return 0;
        }
        __sync_lock_release(&gMscBotBusy);
        return (St & HUB_PORT_CONNECTION) ? 1 : 0;
    }
    if (gMscPort == 0 || gMscPort > gMaxPorts) {
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(gMscPort));
    return (Ps & PORTSC_CCS) ? 1 : 0;
}

void HostOffer(void) {
    static const MSC_HOST Host = {
        .Kind = MSC_HOST_XHCI,
        .BringUp = BringUp,
        .Ready = Ready,
        .Scan = ScanPorts,
        .Claim = ClaimPorts,
        .Capacity = Capacity,
        .BlockCount = BlockCount,
        .BlockSize = BlockSize,
        .ReadSectors = ReadSectors,
        .WriteSectors = WriteSectors,
        .Flush = Synchronize,
        .Release = Release,
        .Present = Present,
        .ControllerReady = 0,
    };

    UsbMscHostRegister(&Host);
}

DRIVER_BOOT(DRIVER_BOOT_USB_HOST, HostOffer);
