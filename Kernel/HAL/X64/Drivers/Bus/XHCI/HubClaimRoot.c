/*
 * HubClaimRoot.c — ClaimHubOnRootPort（自 Hub.c 纯搬 · PR-NORM-11）
 *
 * ExistingSlot：MousePortInitialize 已 Address 的 hub，保留 slot 勿 Disable+重 Address。
 */
#include "XHCI/Private.h"
#include "Hub/Private.h"


/*
 * 认领根口 hub：SetConfig + hub desc；USB2 再 Evaluate Hub/MTT。
 * ExistingSlot：MousePortInitialize 已 Address 的 hub，保留 slot 勿 Disable+重 Address
 * （重 Address 带 Hub 位常 cc=0x11；USB3 hub 亦不可设 Hub 位）。
 * 不碰 gSlotId（键盘已绑定时可安全认领另一根口上的 hub）。
 */
int ClaimHubOnRootPort(UINT32 RootPort, UINT8 Speed, UINT32 ExistingSlot) {
    UINT8 Nports = 4;
    int Usb2Hub = (Speed < 4);

    if (gHubSlotId != 0) {
        if (ExistingSlot != 0 && ExistingSlot != gHubSlotId) {
            DisableSlot(ExistingSlot);
        }
        return 1;
    }
    BootLogV("Boot: XHCI claim hub\n");
    gHubRootPort = RootPort;
    gHubSpeed = Speed;
    /* Device Desc 多已在 gControlBuffer；没有则补读再判 MTT */
    if (gControlBuffer[4] != 0x09) {
        gTransferSlot = ExistingSlot ? ExistingSlot : 0;
        if (ExistingSlot != 0) {
            gEndpoint0MaxPacketSize = SpeedMaxPacketSize(Speed);
            (void)GetDeviceDesc();
        }
    }
    HubNoteMttFromDeviceDescriptor(Speed);

    if (ExistingSlot != 0) {
        BootLogV("Boot: XHCI hub adopt slot\n");
        /* MSC/鼠 Address 的 DevCtx 迁到 gHubDevCtx，Evaluate 才能读到真 Output */
        {
            UINT64 Phys = (gDcbaaLive && ExistingSlot <= gDcbaaMaxSlot)
                              ? gDcbaaLive[ExistingSlot]
                              : 0;
            UINT8 *Src = 0;

            if (Phys == PointerToPhysical(gMscScanDevCtx)) {
                Src = gMscScanDevCtx;
            } else if (Phys == PointerToPhysical(gMouseDevCtx)) {
                Src = gMouseDevCtx;
            } else if (Phys == PointerToPhysical(gDevCtx)) {
                Src = gDevCtx;
            }
            if (Src != 0 && Src != gHubDevCtx) {
                FlushDma(Src, 2048);
                CopyMemory(gHubDevCtx, Src, 2048);
                FlushDma(gHubDevCtx, 2048);
                DcbaaSet(ExistingSlot, PointerToPhysical(gHubDevCtx));
                DcbaaFlush();
            }
        }
        gHubSlotId = ExistingSlot;
        gTransferSlot = ExistingSlot;
        gEndpoint0MaxPacketSize = SpeedMaxPacketSize(Speed);
        /* 该 slot 先前按 mouse/msc 环 Address；迁到 hub 专用环 */
        RecoverEndpoint0(gHubSlotId);
        if (!FinishHubSetup(&Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumerateWhy("Boot: Why=hub cfg\n");
            return 0;
        }
        /* USB2/SS 均须 Hub+端口数；SS 无 TT 也要 ConfigEP，否则子口 Address 挂死 */
        if (!EvaluateHubSlot(gHubSlotId, RootPort, Speed, Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumerateWhy("Boot: Why=hub eval\n");
            return 0;
        }
        if (!HubSetDepth(0)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumerateWhy("Boot: Why=hub depth\n");
            return 0;
        }
        BootLogHexV("Boot: XHCI hub spd=", Speed, 1);
        if (Usb2Hub) {
            BootLog("Boot: XHCI ep0=split\n");
        }
        return 1;
    }

    gEndpoint0MaxPacketSize = SpeedMaxPacketSize(Speed);
    if (!AddressDeviceOnPort(RootPort, Speed, &gHubSlotId, gHubDevCtx,
                             0, 0, 0, Usb2Hub ? 1 : 0, Usb2Hub ? 4 : 0)) {
        gHubSlotId = 0;
        EnumerateWhy("Boot: Why=hub addr\n");
        return 0;
    }
    /* Address 后才有 Device Desc → 再定 MTT，随后 Evaluate 写入 */
    if (GetDeviceDesc() == 0) {
        HubNoteMttFromDeviceDescriptor(Speed);
    }
    if (!FinishHubSetup(&Nports)) {
        DisableSlot(gHubSlotId);
        gHubSlotId = 0;
        return 0;
    }
    if (!EvaluateHubSlot(gHubSlotId, RootPort, Speed, Nports)) {
        /* SS 无 Hub+ports 时子口 Address 会挂命令环；失败则整颗 hub 放弃 */
        DisableSlot(gHubSlotId);
        gHubSlotId = 0;
        EnumerateWhy("Boot: Why=hub eval\n");
        return 0;
    }
    if (!HubSetDepth(0)) {
        DisableSlot(gHubSlotId);
        gHubSlotId = 0;
        EnumerateWhy("Boot: Why=hub depth\n");
        return 0;
    }
    return 1;
}

/* 根口已 Address：device class=9 或配置含 hub iface → 枚举子口找键盘 */
