/*
 * DeviceControl.c — PR-S-xhcidevice-1：ControlTransfer / GetDescriptor / Evaluate
 *
 * 从 Device.c 原样搬家；不改语义。无 static 提升。
 */
#include "XHCI/Private.h"

int ControlTransfer(USB_SETUP_PACKET *Setup, void *Data) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 SetupParam = 0;
    UINT8 *Raw = (UINT8 *)Setup;
    for (int i = 0; i < 8; i++) {
        SetupParam |= ((UINT64)Raw[i]) << (8 * i);
    }

    UINT32 Trt = 0;
    if (Setup->wLength && Data) {
        Trt = (Setup->bmRequestType & 0x80) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    Endpoint0RingForSlot(gTransferSlot, &Ring, &St);

    /* 清完成码：超时后若仍显示上一笔 cc=1，会误报 FAIL want=cc=1|13 got=0x01 */
    gTransferDone = 0;
    gTransferCode = 0;
    /* excl-1：门铃与 WaitTransfer 同独占窗 */
    EventEnterExclusive();
    Enqueue(Ring, St, SetupParam, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | Trt);

    if (Setup->wLength && Data) {
        UINT32 Dir = (Setup->bmRequestType & 0x80) ? TRB_DIR_IN : 0;
        Enqueue(Ring, St, PointerToPhysical(Data), Setup->wLength, TRB_TYPE(TRB_DATA) | Dir);
    }

    UINT32 StatusDir = (Setup->wLength && (Setup->bmRequestType & 0x80)) ? 0 : TRB_DIR_IN;
    Enqueue(Ring, St, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | StatusDir);
    RingDoorbell(gTransferSlot, 1);
    if (WaitTransfer(150000) < 0) {
        ProcessEvents();
        ServiceHidCompletions();
        if (gTransferDone && (gTransferCode == CC_SUCCESS || gTransferCode == CC_SHORT_PACKET)) {
            EventLeaveExclusive();
            DiagChk("ControlTransfer", 1, "cc=1|13", gTransferCode, 2);
            return 0;
        }
        if (!gTransferDone) {
            if (DiagVerbose()) {
                DiagChkStr("ControlTransfer", 0, "xfer done", "timeout");
            }
        } else if (gTransferCode == CC_STALL) {
            /* Stall：键鼠 SetProtocol/Idle/错 iface 很常见；只 VERBOSE 打 */
            if (DiagVerbose()) {
                DiagChk("ControlTransfer", 0, "cc=1|13", gTransferCode, 2);
            }
        } else if (!gTransferFast && gCtrlFailLogged < 2) {
            DiagChk("ControlTransfer", 0, "cc=1|13", gTransferCode, 2);
            gCtrlFailLogged++;
        }
        EventLeaveExclusive();
        RecoverEndpoint0(gTransferSlot);
        return -1;
    }
    if (!(gTransferCode == CC_SUCCESS || gTransferCode == CC_SHORT_PACKET)) {
        EventLeaveExclusive();
        if (gTransferCode == CC_STALL) {
            if (DiagVerbose()) {
                DiagChk("ControlTransfer", 0, "cc=1|13", gTransferCode, 2);
            }
        } else if (!gTransferFast && gCtrlFailLogged < 2) {
            DiagChk("ControlTransfer", 0, "cc=1|13", gTransferCode, 2);
            gCtrlFailLogged++;
        }
        return -1;
    }
    EventLeaveExclusive();
    if (DiagVerbose()) {
        DiagChk("ControlTransfer", 1, "cc=1|13", gTransferCode, 2);
    }
    return 0;
}

/* GET_DESCRIPTOR 控制传输封装 */
int GetDescriptor(UINT16 TypeIndex, UINT16 Index, UINT16 Length, void *Buf) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x80,
        .bRequest = 0x06,
        .wValue = TypeIndex,
        .wIndex = Index,
        .wLength = Length
    };
    ZeroMemory(Buf, Length);
    FlushDma(Buf, Length);
    if (ControlTransfer(&Setup, Buf) < 0) {
        return -1;
    }
    FlushDma(Buf, Length);
    return 0;
}

/* 对齐 EDK2 XhcConfigHubContext：从 Output Slot 拷贝后 ConfigEP，写入 Hub/端口数。
 * USB2：再写 MTT/TTT（缺 TTT 时真机 EP0 经 TT 成功、中断 IN 永不完成）。
 * USB3：也须 Hub+NumPorts；旧逻辑 Speed>=4 直接 return → 子口 Address 挂死→鼠卡。 */
int EvaluateHubSlot(UINT32 SlotId, UINT32 RootPort, UINT8 Speed, UINT8 NumPorts) {
    UINT32 *InSlotCtx;
    UINT32 *OutSlotCtx;
    UINT8 *OutCtx;
    UINT64 Phys;
    UINT32 i;
    UINT32 Words;
    int Usb2Hub = (Speed < 4);

    if (NumPorts == 0 || SlotId == 0) {
        return 0;
    }
    Phys = (gDcbaaLive && SlotId <= gDcbaaMaxSlot) ? gDcbaaLive[SlotId] : 0;
    OutCtx = gHubDevCtx;
    if (Phys == PointerToPhysical(gMscScanDevCtx)) {
        OutCtx = gMscScanDevCtx;
    } else if (Phys == PointerToPhysical(gMouseDevCtx)) {
        OutCtx = gMouseDevCtx;
    } else if (Phys == PointerToPhysical(gDevCtx)) {
        OutCtx = gDevCtx;
    }
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0); /* Add A0 */
    InSlotCtx = (UINT32 *)(void *)InSlot();
    FlushDma(OutCtx, 2048);
    OutSlotCtx = (UINT32 *)(void *)OutCtx;
    Words = gCtxSize / 4u;
    if (Words > 16) {
        Words = 16;
    }
    for (i = 0; i < Words; i++) {
        InSlotCtx[i] = OutSlotCtx[i];
    }
    /* Context Entries 至少 1；Hub +（USB2）可选 MTT + TTT */
    if (((InSlotCtx[0] >> 27) & 0x1Fu) < 1u) {
        InSlotCtx[0] = (InSlotCtx[0] & ~(0x1Fu << 27)) | (1u << 27);
    }
    InSlotCtx[0] |= (1u << 26);
    if (Usb2Hub && gHubMtt) {
        InSlotCtx[0] |= (1u << 25);
    } else {
        InSlotCtx[0] &= ~(1u << 25);
    }
    if (Usb2Hub) {
        InSlotCtx[0] = (InSlotCtx[0] & ~(3u << 16)) | (((UINT32)gHubTtt & 3u) << 16);
    } else {
        InSlotCtx[0] &= ~(3u << 16);
    }
    if (Speed != 0) {
        InSlotCtx[0] = (InSlotCtx[0] & ~(0xFu << 20)) | ((UINT32)Speed << 20);
    }
    InSlotCtx[1] = (InSlotCtx[1] & 0x0000FFFFu) |
                   ((UINT32)RootPort << 16) | ((UINT32)NumPorts << 24);
    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(OutCtx, 2048);
    /* EDK2 走 Configure Endpoint（非 Evaluate）更新 hub Slot */
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) != 0) {
        BootLogHex("Boot: XHCI hub cfg cc=", gCmdCode, 2);
        return 0;
    }
    if (Usb2Hub) {
        BootLogHexV("Boot: XHCI hub mtt=", gHubMtt, 1);
        BootLogHexV("Boot: XHCI hub ttt=", gHubTtt, 1);
    } else {
        BootLogHexV("Boot: XHCI hub ss ports=", NumPorts, 1);
    }
    return 1;
}

/* Device Desc 仍在 gControlBuffer：HS Multi-TT hub 的 bDeviceProtocol==2 */
void HubNoteMttFromDeviceDescriptor(UINT8 Speed) {
    gHubMtt = 0;
    if (Speed == 3 && gControlBuffer[4] == 0x09 && gControlBuffer[7] == 2) {
        gHubMtt = 1;
    }
}

int EvaluateEndpoint0(UINT32 SlotId, UINT16 MaxPacketSize) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 Deq;

    Endpoint0RingForSlot(SlotId, &Ring, &St);
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 1);
    {
        UINT32 *Endpoint0 = (UINT32 *)(void *)InEndpoint(1);
        Endpoint0[1] = (3u << 1) | (4u << 3) | ((UINT32)MaxPacketSize << 16);
        Deq = PointerToPhysical(&Ring[St->Enq]) | (UINT64)(St->Pcs & 1);
        Endpoint0[2] = (UINT32)Deq;
        Endpoint0[3] = (UINT32)(Deq >> 32);
    }
    gEndpoint0MaxPacketSize = MaxPacketSize;
    FlushDma(gInCtx, sizeof(gInCtx));
    return Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_EVALUATE_CTX) | TRB_SLOT(SlotId), 0) == 0;
}

/* 先 8 字节拿 bMaxPacketSize0，再 18 字节完整设备描述符 */
int GetDeviceDesc(void) {
    UINT8 MaxPacketSize;
    int Ok;

    Ok = GetDescriptor(USB_WVALUE_DT_DEVICE, 0, 8, gControlBuffer) == 0;
    if (DiagVerbose()) {
        DiagChk("GetDesc8", Ok, "xfer ok", Ok ? gControlBuffer[7] : gTransferCode, 2);
    }
    if (!Ok) {
        EnumerateWhy("Boot: Why=desc8\n");
        return -1;
    }
    MaxPacketSize = gControlBuffer[7];
    if (MaxPacketSize != 8 && MaxPacketSize != 16 && MaxPacketSize != 32 && MaxPacketSize != 64) {
        MaxPacketSize = (UINT8)gEndpoint0MaxPacketSize;
    }
    if (MaxPacketSize != (UINT8)gEndpoint0MaxPacketSize) {
        (void)EvaluateEndpoint0(gTransferSlot, MaxPacketSize);
    }
    Ok = GetDescriptor(USB_WVALUE_DT_DEVICE, 0, 18, gControlBuffer) == 0;
    if (DiagVerbose()) {
        DiagChk("GetDesc18", Ok, "len>=18 class", Ok ? gControlBuffer[4] : gTransferCode, 2);
    }
    if (!Ok) {
        EnumerateWhy("Boot: Why=desc18\n");
        return -1;
    }
    return 0;
}

