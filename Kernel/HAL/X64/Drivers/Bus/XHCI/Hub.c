/*
 * Hub.c — PR-S-xhcihub-1：Hub 控制与根口认领
 *
 * 从 Hub.c 原样搬家；不改语义。Hub 口帮手去掉 static，声明在 Internal.h。
 */
#include "XHCI/Private.h"
#include "Hub/Private.h"

static int HubCtrlSetup(const USB_SETUP *Setup, void *Data) {
    return HubCtrl(Setup->bmRequestType, Setup->bRequest, Setup->wValue,
                   Setup->wIndex, Setup->wLength, Data);
}

int HubCtrl(UINT8 BmReq, UINT8 Req, UINT16 Value, UINT16 Index,
                   UINT16 Len, void *Data) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = BmReq,
        .bRequest = Req,
        .wValue = Value,
        .wIndex = Index,
        .wLength = Len
    };
    gXferSlot = gHubSlotId;
    return ControlXfer(&Setup, Data);
}

int FinishHubSetup(UINT8 *OutNumPorts) {
    UINT8 HubDesc[16];
    UINT8 Nports = 4;
    UINT8 ConfigVal = 1;

    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    ConfigVal = gCtrlBuf[5] ? gCtrlBuf[5] : 1;
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    ZeroMemory(HubDesc, sizeof(HubDesc));
    gHubTtt = 0;
    {
        USB_SETUP Setup;

        DescriptorSetup(&Setup, sizeof(HubDesc));
        if (HubCtrlSetup(&Setup, HubDesc) == 0 &&
            HubDesc[2] != 0) {
            Nports = HubDesc[2];
            if (Nports > 15) {
                Nports = 15;
            }
            /* USB2 Hub Desc：wHubCharacteristics bit5-6 = TT Think Time */
            gHubTtt = (UINT8)((HubDesc[3] >> 5) & 3u);
        }
    }
    if (OutNumPorts) {
        *OutNumPorts = Nports;
    }
    gHubNumPorts = Nports;
    BootLogV("Boot: XHCI hub ports ok\n");
    return 1;
}

int HubGetPortStatus(UINT8 Port, UINT32 *OutSt) {
    UINT8 Buf[4];
    USB_SETUP Setup;

    PortStatusSetup(&Setup, Port);
    if (HubCtrlSetup(&Setup, Buf) < 0) {
        return -1;
    }
    *OutSt = (UINT32)Buf[0] | ((UINT32)Buf[1] << 8) |
             ((UINT32)Buf[2] << 16) | ((UINT32)Buf[3] << 24);
    return 0;
}

int HubSetPortFeat(UINT8 Port, UINT16 Feat) {
    USB_SETUP Setup;

    PortFeatureSetup(&Setup, Port, Feat, 1);
    return HubCtrlSetup(&Setup, 0);
}

int HubClearPortFeat(UINT8 Port, UINT16 Feat) {
    USB_SETUP Setup;

    PortFeatureSetup(&Setup, Port, Feat, 0);
    return HubCtrlSetup(&Setup, 0);
}

/*
 * hub 口速度 → xHCI Port Speed（0=尚不可 Address）。
 * USB2：bits 10..9 = FS/LS/HS → 1/2/3。
 * USB3（USB 3.2 §10.14.2.6）：bits 12..10 = 0 FS/LS、1 HS、2+ SS；
 * bit9=Power，不是速度。st=0x0203 的 Speed 字段是 0，绝不能报成 SS(4)。
 */
UINT8 HubPortSpeed(UINT32 St) {
    UINT32 Bits;
    UINT32 Spd3;

    if (gHubSpeed >= 4) {
        Spd3 = (St >> 10) & 7u;
        if (Spd3 >= 2u) {
            return 4; /* SuperSpeed(+) */
        }
        if (Spd3 == 1u) {
            return 3; /* HS：SS hub 父 slot 无 TT，调用方应跳过 Address */
        }
        return 0; /* 未协商好 / FS/LS：禁止当 SS Address */
    }
    Bits = (St >> 9) & 3u;
    if (Bits == 0) {
        return 1;
    }
    if (Bits == 1) {
        return 2;
    }
    if (Bits == 2) {
        return 3;
    }
    return 1;
}

int IsHubDeviceDesc(void) {
    /* GET_DESCRIPTOR device 已在 gCtrlBuf */
    if (gCtrlBuf[4] == 0x09) {
        return 1;
    }
    return 0;
}

/* 配置描述符中是否有 Hub Interface（bDeviceClass=0 的常见 hub） */
int ConfigHasHubIface(UINT8 *Cfg, UINT16 Total) {
    UINT16 Off = 0;

    while (Off + 9 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9 && Cfg[Off + 5] == 0x09) {
            return 1;
        }
        Off = (UINT16)(Off + Len);
    }
    return 0;
}

/*
 * 认领根口 hub：SetConfig + hub desc；USB2 再 Evaluate Hub/MTT。
 * ExistingSlot：InitMouseOnPort 已 Address 的 hub，保留 slot 勿 Disable+重 Address
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
    /* Device Desc 多已在 gCtrlBuf；没有则补读再判 MTT */
    if (gCtrlBuf[4] != 0x09) {
        gXferSlot = ExistingSlot ? ExistingSlot : 0;
        if (ExistingSlot != 0) {
            gEp0Mps = SpeedMps(Speed);
            (void)GetDeviceDesc();
        }
    }
    HubNoteMttFromDevDesc(Speed);

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
        gXferSlot = ExistingSlot;
        gEp0Mps = SpeedMps(Speed);
        /* 该 slot 先前按 mouse/msc 环 Address；迁到 hub 专用环 */
        RecoverEp0(gHubSlotId);
        if (!FinishHubSetup(&Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumWhy("Boot: Why=hub cfg\n");
            return 0;
        }
        /* USB2/SS 均须 Hub+端口数；SS 无 TT 也要 ConfigEP，否则子口 Address 挂死 */
        if (!EvaluateHubSlot(gHubSlotId, RootPort, Speed, Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumWhy("Boot: Why=hub eval\n");
            return 0;
        }
        BootLogHex("Boot: XHCI hub spd=", Speed, 1);
        if (Usb2Hub) {
            BootLog("Boot: XHCI ep0=split\n");
        }
        return 1;
    }

    gEp0Mps = SpeedMps(Speed);
    if (!AddressDeviceOnPort(RootPort, Speed, &gHubSlotId, gHubDevCtx,
                             0, 0, 0, Usb2Hub ? 1 : 0, Usb2Hub ? 4 : 0)) {
        gHubSlotId = 0;
        EnumWhy("Boot: Why=hub addr\n");
        return 0;
    }
    /* Address 后才有 Device Desc → 再定 MTT，随后 Evaluate 写入 */
    if (GetDeviceDesc() == 0) {
        HubNoteMttFromDevDesc(Speed);
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
        EnumWhy("Boot: Why=hub eval\n");
        return 0;
    }
    return 1;
}

/* 根口已 Address：device class=9 或配置含 hub iface → 枚举子口找键盘 */
int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed) {
    BootLog("Boot: XHCI hub on root\n");
    /* 重新 Address 为 Hub 设备（带 Hub 位）；此时 gSlotId 是误 Address 的非 hub */
    DisableSlot(gSlotId);
    gSlotId = 0;
    if (!ClaimHubOnRootPort(RootPort, Speed, 0)) {
        return 0;
    }
    if (EnumHubChildrenForKeyboard()) {
        return 1;
    }
    return 0;
}

