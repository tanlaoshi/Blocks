/*
 * MscClaim.c — MSC FinishClaim / ConfigEP（PR-S3-xhcimscclaim-1）
 *
 * 配置描述符解析见 MscClaimParse.c。MSC 全局仍在 Xhci.c。
 */
#include "XHCI/Private.h"

/* ConfigEP：Bulk IN + Bulk OUT（EP Type 6/2）；带回 Route/TT */
static int ConfigureBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                            UINT8 EndpointIn, UINT16 MaxPacketSizeIn, UINT8 EndpointOut, UINT16 MaxPacketSizeOut) {
    UINT8 InNum = EndpointIn & 0x0F;
    UINT8 OutNum = EndpointOut & 0x0F;
    UINT32 InDci = (UINT32)InNum * 2 + 1;
    UINT32 OutDci = (UINT32)OutNum * 2 + 0;
    UINT32 CtxEntries = InDci > OutDci ? InDci : OutDci;
    UINT32 *Slot;
    UINT32 *Endpoint;
    UINT64 Deq;

    if (MaxPacketSizeIn == 0 || MaxPacketSizeIn > 1024) {
        MaxPacketSizeIn = 512;
    }
    if (MaxPacketSizeOut == 0 || MaxPacketSizeOut > 1024) {
        MaxPacketSizeOut = 512;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gMscRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gMscHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gMscHubSlot | ((UINT32)gMscTtPort << 8);
    }

    if (!gMscBulkRingsInited) {
        (void)BringUp();
    }
    RingInitialize(gBulkInRing, &gBulkIn, RING_SIZE);
    RingInitialize(gBulkOutRing, &gBulkOut, RING_SIZE);

    Endpoint = (UINT32 *)(void *)InEndpoint(InDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (6u << 3) | ((UINT32)MaxPacketSizeIn << 16); /* Bulk IN */
    Deq = PointerToPhysical(gBulkInRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSizeIn;

    Endpoint = (UINT32 *)(void *)InEndpoint(OutDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (2u << 3) | ((UINT32)MaxPacketSizeOut << 16); /* Bulk OUT */
    Deq = PointerToPhysical(gBulkOutRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSizeOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gBulkInRing, sizeof(gBulkInRing));
    FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
    FlushDma(gMscScanDevCtx, sizeof(gMscScanDevCtx));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        BootLog("Boot: MSC claim cfg ep fail\n");
        return 0;
    }

    gMscBulkInDci = InDci;
    gMscBulkOutDci = OutDci;
    gMscBulkInMaxPacketSize = MaxPacketSizeIn;
    gMscBulkOutMaxPacketSize = MaxPacketSizeOut;
    return 1;
}

/*
 * gMscScanSlot 已 Address：取配置、Parse Bulk、SetConfig、ConfigEP。
 * 成功置 gMscClaimed；失败 Disable slot。
 */
int FinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EndpointIn = 0;
    UINT8 EndpointOut = 0;
    UINT8 Interface = 0;
    UINT16 MaxPacketSizeIn = 64;
    UINT16 MaxPacketSizeOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT8 DevClass;

    if (gMscScanSlot == 0) {
        return 0;
    }
    gTransferSlot = gMscScanSlot;
    if (GetDeviceDesc() < 0) {
        BootLog("Boot: MSC claim desc fail\n");
        goto fail;
    }
    DevClass = gControlBuffer[4];
    BootLogHexV("Boot: MSC claim dclass=", DevClass, 2);
    {
        UINT16 Vid = (UINT16)(gControlBuffer[8] | (gControlBuffer[9] << 8));

        /* 勿把 FT232 当 MSC（厂商 Bulk）；留给 usb-uart */
        if (Vid == 0x0403u) {
            BootLog("Boot: MSC claim skip FTDI\n");
            gPortNeedForcePr &= ~(1u << RootPort);
            FtdiKeepForUart(gMscScanSlot, RootPort, Speed, gMscRoute,
                            gMscHubSlot, gMscTtPort);
            gMscScanSlot = 0;
            return 0;
        }
    }
    if (DevClass == 0x09) {
        BootLog("Boot: MSC claim skip hub device\n");
        goto skip_not_msc;
    }
    if (DevClass == 0x02) {
        BootLog("Boot: MSC claim skip CDC\n");
        goto skip_not_msc;
    }
    if (DevClass == 0x03 || DevClass == 0xE0) {
        BootLogHex("Boot: MSC claim skip class=", DevClass, 2);
        goto skip_not_msc;
    }

    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gMscConfigBuffer) < 0) {
        BootLog("Boot: MSC claim cfg9 fail\n");
        goto fail;
    }
    Total = (UINT16)(gMscConfigBuffer[2] | (gMscConfigBuffer[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gMscConfigBuffer)) {
        BootLogHex("Boot: MSC claim cfg trunc want=", Total, 4);
        Total = (UINT16)sizeof(gMscConfigBuffer);
    }
    ConfigVal = gMscConfigBuffer[5] ? gMscConfigBuffer[5] : 1;
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gMscConfigBuffer) < 0) {
        RecoverEndpoint0(gMscScanSlot);
        gTransferSlot = gMscScanSlot;
        if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gMscConfigBuffer) < 0) {
            BootLog("Boot: MSC claim cfg fail\n");
            goto fail;
        }
    }
    BootLogHexV("Boot: MSC claim cfg len=", Total, 4);

    if (ConfigHasHubInterface(gMscConfigBuffer, Total)) {
        /* 根口应在 ClaimPorts 已认领；子口嵌套 hub 跳过 */
        BootLogHex("Boot: MSC claim cfg hub iface port=", RootPort, 2);
        goto fail;
    }

    if (!ParseBulk(gMscConfigBuffer, Total, &Interface, &EndpointIn, &MaxPacketSizeIn, &EndpointOut, &MaxPacketSizeOut)) {
        BootLogHex("Boot: MSC claim no bulk port=", RootPort, 2);
        LogConfigInterfaces(gMscConfigBuffer, Total);
        goto fail;
    }

    if (SetConfig(ConfigVal) < 0) {
        BootLog("Boot: MSC claim setcfg fail\n");
        goto fail;
    }

    if (!ConfigureBulk(gMscScanSlot, RootPort, Speed, EndpointIn, MaxPacketSizeIn, EndpointOut, MaxPacketSizeOut)) {
        goto fail;
    }

    gMscPort = RootPort;
    gMscClaimed = 1;
    BootLogHex("Boot: MSC claim ok port=", RootPort, 2);
    BootLogHexV("Boot: MSC claim slot=", gMscScanSlot, 2);
    BootLogHexV("Boot: MSC claim iface=", Interface, 2);
    BootLogHexV("Boot: MSC claim epin=", EndpointIn, 2);
    BootLogHexV("Boot: MSC claim epout=", EndpointOut, 2);
    BootLogHexV("Boot: MSC claim route=", gMscRoute, 2);
    return 1;

skip_not_msc:
    /* 已知非 MSC：清 NeedForce，勿让 HotPoll 每 5s 再 Force PR（保鼠） */
    if (gMscScanSlot != 0) {
        gPortNeedForcePr &= ~(1u << RootPort);
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    return 0;

fail:
    if (gMscScanSlot != 0) {
        gPortNeedForcePr |= (1u << RootPort);
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    return 0;
}
