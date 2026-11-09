/*
 * Device.c — Address / Slot / Endpoint0 环选择（usb-map / PR-S-xhcidevice-1）
 *
 * AddressDeviceOnPort：EnableSlot → Address Device；完成码见 gCmdCode（日志 cc=）。
 * hub 后 SS：BSR=1 再 BSR=0（§4.6.5）；根口仍一步。USBTE≠「再同招超时」。
 * 各设备独立 EP0 环（Hub/MSC/Mouse…）：共享环会毁掉 hub dequeue → 子口永不完成。
 *
 * 上游：ClaimPort / HubEnumerate / HID / Serial。
 * usb-hid-keep：gXhciPortEnumerateFreeze=1 → 本函数直接 return 0（禁再 EnableSlot/Address，保鼠环）。
 */
#include "XHCI/Private.h"

UINT16 SpeedMaxPacketSize(UINT8 Speed) {
    if (Speed == 4) {
        return 512;
    }
    if (Speed == 3) {
        return 64;
    }
    return 8;
}

/*
 * 键 / hub / 独立鼠 各用独立 EP0 环。
 * 真机证据：Address 子设备时 RingInitialize(共享 gEndpoint0Ring) 会毁掉 hub EP0 dequeue，
 * hub Slot 仍 Hub=1 且鼠 epst=Running，但 TT 中断 IN 永不完成 → PHOTO m=0。
 */
void Endpoint0RingForSlot(UINT32 SlotId, XHCI_TRB **RingOut, RING_STATE **StOut) {
    if (SlotId != 0 && SlotId == gMscProbeHubSlot) {
        /* 第二 hub MSC probe：Address 时已用 msc EP0 环 */
        *RingOut = gMscScanEndpoint0Ring;
        *StOut = &gMscScanEndpoint0;
    } else if (SlotId != 0 && SlotId == gHubSlotId) {
        *RingOut = gHubEndpoint0Ring;
        *StOut = &gHubEndpoint0;
    } else if (SlotId != 0 && SlotId == gMscScanSlot) {
        *RingOut = gMscScanEndpoint0Ring;
        *StOut = &gMscScanEndpoint0;
    } else if (SlotId != 0 && SlotId == gFtdiSlot) {
        if (gFtdi.EndpointZeroRing) {
            gFtdi.EndpointZeroRing(RingOut, StOut);
        }
    } else if (SlotId != 0 && SlotId == gCdcSlot) {
        if (gCdc.EndpointZeroRing) {
            gCdc.EndpointZeroRing(RingOut, StOut);
        }
    } else if (SlotId != 0 && SlotId == gBluetoothSlot) {
        BluetoothEndpoint0Ring(RingOut, StOut);
    } else if (SlotId != 0 && SlotId <= DCBAA_SLOTS && gSlotEndpoint0UsesKeyboardRing[SlotId]) {
        /* 曾以键盘路径 Address：claim 为鼠后仍跟 gEndpoint0 硬件 dequeue */
        *RingOut = gEndpoint0Ring;
        *StOut = &gEndpoint0;
    } else if (SlotId != 0 && SlotId == gMouseSlotId && SlotId != gSlotId) {
        *RingOut = gMouseEndpoint0Ring;
        *StOut = &gMouseEndpoint0;
    } else {
        *RingOut = gEndpoint0Ring;
        *StOut = &gEndpoint0;
    }
}

void Endpoint0RingForSlotOut(UINT32 *SlotOut, XHCI_TRB **RingOut, RING_STATE **StOut) {
    if (SlotOut == &gHubSlotId) {
        *RingOut = gHubEndpoint0Ring;
        *StOut = &gHubEndpoint0;
    } else if (SlotOut == &gMouseSlotId) {
        *RingOut = gMouseEndpoint0Ring;
        *StOut = &gMouseEndpoint0;
    } else if (SlotOut == &gMscScanSlot) {
        *RingOut = gMscScanEndpoint0Ring;
        *StOut = &gMscScanEndpoint0;
    } else if (SlotOut == &gFtdiSlot) {
        if (gFtdi.EndpointZeroRing) {
            gFtdi.EndpointZeroRing(RingOut, StOut);
        }
    } else if (SlotOut == &gCdcSlot) {
        if (gCdc.EndpointZeroRing) {
            gCdc.EndpointZeroRing(RingOut, StOut);
        }
    } else if (SlotOut == &gBluetoothSlot) {
        BluetoothEndpoint0Ring(RingOut, StOut);
    } else {
        *RingOut = gEndpoint0Ring;
        *StOut = &gEndpoint0;
    }
}

int AddressDeviceOnPort(UINT32 RootPort, UINT8 Speed, UINT32 *SlotOut,
                               UINT8 *DevCtx, UINT32 RouteString,
                               UINT8 ParentHubSlot, UINT8 TtPort,
                               int HubDevice, UINT8 HubNumPorts) {
    int Ok;
    XHCI_TRB *Endpoint0Ring;
    RING_STATE *Endpoint0State;

    gTransferSlot = 0;
    if (SlotOut) {
        *SlotOut = 0;
    }
    if (gXhciPortEnumerateFreeze) {
        return 0;
    }
    DiagChk("AddressDev.port", 1, "root+spd", ((UINT64)RootPort << 8) | Speed, 4);
    if (Command(0, TRB_TYPE(TRB_ENABLE_SLOT), SlotOut) < 0 || *SlotOut == 0 ||
        *SlotOut > gDcbaaMaxSlot) {
        DiagChkStr("AddressDev", 0, "EnableSlot ok", "fail");
        BootLogHex("Boot: XHCI EnableSlot cc=", gCmdCode, 2);
        BootLogHex("Boot: XHCI EnableSlot slot=", *SlotOut, 2);
        BootLogHex("Boot: XHCI EnableSlot done=", gCmdDone, 1);
        EnumerateWhy("Boot: Why=enable slot\n");
        return 0;
    }

    gTransferSlot = *SlotOut;
    DcbaaSet(*SlotOut, PointerToPhysical(DevCtx));
    ZeroMemory(DevCtx, 2048);
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << 1);

    if (SlotOut == &gSlotId) {
        gKeyboardRoute = RouteString & 0xFFFFFu;
        gKeyboardHubSlot = ParentHubSlot;
        gKeyboardTransactionTranslatorPort = TtPort;
        if (*SlotOut <= DCBAA_SLOTS) {
            gSlotEndpoint0UsesKeyboardRing[*SlotOut] = 1;
        }
    }
    if (SlotOut == &gMouseSlotId) {
        gMouseRoute = RouteString & 0xFFFFFu;
        gMouseHubSlot = ParentHubSlot;
        gMouseTtPort = TtPort;
        if (*SlotOut <= DCBAA_SLOTS) {
            gSlotEndpoint0UsesKeyboardRing[*SlotOut] = 0;
        }
    }

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (1u << 27) | ((UINT32)Speed << 20) | (RouteString & 0xFFFFFu);
    if (HubDevice) {
        Slot[0] |= (1u << 26); /* USB2 hub only; SS hub must stay Hub=0 */
        if (gHubMtt) {
            Slot[0] |= (1u << 25); /* 仅 Multi-TT hub */
        }
    }
    Slot[1] = ((UINT32)RootPort << 16);
    if (HubDevice && HubNumPorts != 0) {
        Slot[1] |= ((UINT32)HubNumPorts << 24);
    }
    /*
     * Slot DWORD2 = TT Hub Slot / TT Port（xHCI §6.2.2）：
     * 仅 LS/FS（Speed 1/2）经 HS hub 的 TT 须填；HS（3）与 SS（≥4）必须 0，
     * 拓扑靠 Route String + Root Hub Port。
     * 旧条件 Speed<4 给 HS 子设备也填 TT → Address USBTE cc=0x04
     * （NUC：root hub 子口2 speed=3 反复 addr fail；ConfigureBulk 已是 Speed<3）。
     */
    if (ParentHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)ParentHubSlot | ((UINT32)TtPort << 8);
    }

    Endpoint0RingForSlotOut(SlotOut, &Endpoint0Ring, &Endpoint0State);
    RingInitialize(Endpoint0Ring, Endpoint0State, RING_SIZE);
    UINT32 *Endpoint0 = (UINT32 *)(void *)InEndpoint(1);
    gEndpoint0MaxPacketSize = SpeedMaxPacketSize(Speed);
    Endpoint0[1] = (3u << 1) | (4u << 3) | ((UINT32)gEndpoint0MaxPacketSize << 16);
    UINT64 Deq = PointerToPhysical(Endpoint0Ring) | 1;
    Endpoint0[2] = (UINT32)Deq;
    Endpoint0[3] = (UINT32)(Deq >> 32);
    Endpoint0[4] = 8;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(DevCtx, 2048);
    DcbaaFlush();
    FlushDma(Endpoint0Ring, RING_SIZE * sizeof(XHCI_TRB));

    /*
     * xHCI §4.6.5：hub 后 SS 子设备先 BSR=1（不发 SET_ADDRESS，只建可寻址槽），
     * 再 BSR=0 才发 SET_ADDRESS。直接 BSR=0 在部分 HC 上对 hub 子口 → USBTE cc=0x04。
     * 根口设备仍一步 BSR=0（键鼠已通路径勿动）。
     */
    if (ParentHubSlot != 0 && Speed >= 4) {
        Ok = Command(PointerToPhysical(gInCtx),
                     TRB_TYPE(TRB_ADDRESS_DEV) | TRB_BSR | TRB_SLOT(*SlotOut), 0) == 0;
        DiagChk("AddressDev.bsr1", Ok, "AddressDev BSR1 cc=1", gCmdCode, 2);
        if (!Ok) {
            BootLogHex("Boot: XHCI addr BSR1 cc=", gCmdCode, 2);
            EnumerateWhy("Boot: Why=address fail\n");
            if (gXhciCmdSick && SlotOut) {
                *SlotOut = 0;
            }
            return 0;
        }
        FlushDma(gInCtx, sizeof(gInCtx));
        FlushDma(DevCtx, 2048);
    }

    Ok = Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(*SlotOut), 0) == 0;
    DiagChk("AddressDev", Ok, "AddressDev cc=1", gCmdCode, 2);
    if (!Ok) {
        BootLogHex("Boot: XHCI addr cc=", gCmdCode, 2);
        EnumerateWhy("Boot: Why=address fail\n");
        if (gXhciCmdSick && SlotOut) {
            *SlotOut = 0;
        }
        return 0;
    }
    return 1;
}

/* Enable Slot + Address Device（根口设备） */
int AddressDevice(UINT32 Port1, UINT8 Speed) {
    gTransferSlot = gSlotId;
    return AddressDeviceOnPort(Port1, Speed, &gSlotId, gDevCtx, 0, 0, 0, 0, 0);
}

void DisableSlot(UINT32 SlotId) {
    if (SlotId == 0 || SlotId > DCBAA_SLOTS) {
        return;
    }
    (void)Command(0, TRB_TYPE(TRB_DISABLE_SLOT) | TRB_SLOT(SlotId), 0);
    DcbaaSet(SlotId, 0);
    gSlotEndpoint0UsesKeyboardRing[SlotId] = 0;
    if (gSlotId == SlotId) {
        gSlotId = 0;
    }
    if (gMouseSlotId == SlotId) {
        gMouseSlotId = 0;
    }
    if (gHubSlotId == SlotId) {
        gHubSlotId = 0;
    }
    if (gMscProbeHubSlot == SlotId) {
        gMscProbeHubSlot = 0;
    }
    if (gMscScanSlot == SlotId) {
        gMscScanSlot = 0;
        gMscClaimed = 0;
        gMscPort = 0;
        gMscBulkInDci = 0;
        gMscBulkOutDci = 0;
        gMscCapacityOk = 0;
        gMscBlockCount = 0;
        gMscBlockSize = 0;
    }
    if (gFtdiSlot == SlotId) {
        gFtdiSlot = 0;
        gFtdiClaimed = 0;
        gFtdiPort = 0;
        gFtdiBulkInDci = 0;
        gFtdiBulkOutDci = 0;
    }
    if (gCdcSlot == SlotId) {
        gCdcSlot = 0;
        gCdcClaimed = 0;
        gCdcPort = 0;
        gCdcBulkInDci = 0;
        gCdcBulkOutDci = 0;
    }
    if (gTransferSlot == SlotId) {
        gTransferSlot = 0;
    }
}

/* ControlTransfer 超时后 EP0 环与 HC 失步，须 Reset+SetTrDeq 才能继续枚举 */
void RecoverEndpoint0(UINT32 SlotId) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 Deq;
    UINT32 EndpointField = (1u << 16);
    UINT32 QuietSave;

    if (SlotId == 0) {
        return;
    }
    QuietSave = gDiagQuiet;
    gDiagQuiet = 1; /* 恢复过程中的 Stop/ResetEP 勿刷 FAIL */
    (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(SlotId) | EndpointField, 0);
    ProcessEvents();
    (void)Command(0, TRB_TYPE(TRB_RESET_EP) | TRB_SLOT(SlotId) | EndpointField, 0);
    ProcessEvents();
    Endpoint0RingForSlot(SlotId, &Ring, &St);
    RingInitialize(Ring, St, RING_SIZE);
    FlushDma(Ring, RING_SIZE * sizeof(XHCI_TRB));
    Deq = PointerToPhysical(Ring) | 1;
    (void)Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(SlotId) | EndpointField, 0);
    ProcessEvents();
    gDiagQuiet = QuietSave;
}

