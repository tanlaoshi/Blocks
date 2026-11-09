/*
 * EnumeratePort.c — 根口试键盘 / 绑鼠标（PR-F-xhci-1）
 */
#include "XHCI/Private.h"
#include "UsbHostController.h"

/* 1 = 本口已作键盘；0 = 继续下一口 */
int EnumerateTryRootPort(UINT32 P) {
    UINT8 Speed = 0;
    UINT8 EndpointAddress = 0, Interval = 10;
    UINT16 MaxPacketSize = 8;
    UINT8 ConfigVal = 1;
    int HaveIntr = 0;
    UINT16 Total = 0;
    UINT32 Ps = ReadMmio32(gOperationalBase + PortRegister(P));

    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    BootLogHexV("Boot: XHCI try port=", P, 2);
    if (!ResetPort(P)) {
        return 0;
    }
    {
        UINT32 After = ReadMmio32(gOperationalBase + PortRegister(P));
        Speed = PortSpeed(After);
    }
    gPort1 = P;
    gSpeed = Speed;

    BootMarkV("Boot: XHCI address...\n");
    if (!AddressDevice(P, Speed)) {
        BootMarkUsb("Boot: XHCI addr fail\n");
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    BootMarkV("Boot: XHCI address ok\n");

    BootMarkV("Boot: XHCI get desc\n");
    if (GetDeviceDesc() < 0) {
        BootMarkUsb("Boot: XHCI desc fail\n");
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    /* PR-H-hub：根口 hub（device class 9）→ 子口找键盘 */
    if (IsHubDeviceDesc()) {
        BootLog("Boot: XHCI hub root\n");
        if (TryHubOnRootPort(P, Speed)) {
            return 1;
        }
        EnumerateWhy("Boot: Why=hub fail\n");
        DisableSlot(gHubSlotId);
        return 0;
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gControlBuffer) < 0) {
        EnumerateWhy("Boot: Why=cfg desc\n");
        DisableSlot(gSlotId);
        return 0;
    }
    Total = (UINT16)(gControlBuffer[2] | (gControlBuffer[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gControlBuffer)) {
        Total = (UINT16)sizeof(gControlBuffer);
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gControlBuffer) < 0) {
        EnumerateWhy("Boot: Why=cfg desc\n");
        DisableSlot(gSlotId);
        return 0;
    }
    /*
     * bDeviceClass=0 的 hub：配置里 Interface Class=9。
     * 家侧 port3「no hid ep」即此类；不进 hub 则真鼠标可能在 hub 后。
     */
    if (ConfigHasHubInterface(gControlBuffer, Total)) {
        BootLog("Boot: XHCI hub (iface class 9)\n");
        if (TryHubOnRootPort(P, Speed)) {
            return 1;
        }
        EnumerateWhy("Boot: Why=hub iface fail\n");
        DisableSlot(gHubSlotId);
        return 0;
    }
    ConfigVal = gControlBuffer[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    HaveIntr = ParseConfig(gControlBuffer, Total, Speed, &gKeyboardInterface, &EndpointAddress, &MaxPacketSize, &Interval);
    if (!HaveIntr) {
        /*
         * 标准 USB 鼠标是 3/1/2，键盘解析不计分，旧逻辑会打上 gPortNoHid。
         * 鼠标轮不再碰这些口，直插后口的鼠标就永远 mouse=00。
         * slot 已 Address，直接认领；再 Reset 会得到 addr cc=0x04。
         */
        if (ClaimAddressedSlotAsMouse(P, Speed, Total, ConfigVal)) {
            return 0;
        }
        EnumerateWhy("Boot: Why=no hid ep\n");
        gPortNoHid |= (1u << P);
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    if (RealPcRejectMouseExtraAsKeyboard(Total, Speed)) {
        /* 优先原地认领为鼠；失败才 Disable，留给 MousePortInitialize+ForcePR */
        if (ClaimAddressedSlotAsMouse(P, Speed, Total, ConfigVal)) {
            return 0;
        }
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    if (SetConfig(ConfigVal) < 0) {
        EnumerateWhy("Boot: Why=set cfg\n");
        DisableSlot(gSlotId);
        return 0;
    }
    (void)SetProtocolBoot(gKeyboardInterface);
    SetIdle(gKeyboardInterface);
    {
        UINT8 Addr = gDevCtx[12];
        if (Addr == 0 && gSlotId != 0) {
            Addr = (UINT8)gSlotId;
        }
        UsbDeviceNote(Addr, gControlBuffer, Total);
    }
    {
        UINT8 MouseEndpoint = 0, MIv = 10;
        UINT16 MouseMaxPacketSize = 8;
        int WantMouse;

        WantMouse = HalCpuIsHypervisor() &&
                    PrepCompositeMouse(Total, Speed, gKeyboardInterface, EndpointAddress, &MouseEndpoint, &MouseMaxPacketSize, &MIv);
        if (!HalCpuIsHypervisor()) {
            /* 真机 v6：仅键盘对照；复合鼠会弄死键 IN（v5 Sync ok 仍 k=0） */
            if (!ConfigureInterrupt(EndpointAddress, MaxPacketSize, Interval, Speed, 0, 0, 0)) {
                DisableSlot(gSlotId);
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            (void)InterruptByClass(3, 1, 1);
            BootLog("Boot: XHCI kbd-only then bind mouse ports\n");
            BootLog("Boot: XHCI kbd-fix=v8\n");
        } else if (WantMouse) {
            (void)SetInterface(gKeyboardInterface, 0);
            (void)SetProtocolBoot(gKeyboardInterface);
            SetIdle(gKeyboardInterface);
            if (!ConfigureInterrupt(EndpointAddress, MaxPacketSize, Interval, Speed, MouseEndpoint, MouseMaxPacketSize, MIv)) {
                DisableSlot(gSlotId);
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            (void)InterruptByClass(3, 1, 1);
            (void)InterruptByClass(3, 0, 0);
            BootLog("Boot: XHCI-HID Mouse (Composite)\n");
        } else {
            if (!ConfigureInterrupt(EndpointAddress, MaxPacketSize, Interval, Speed, 0, 0, 0)) {
                DisableSlot(gSlotId);
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            (void)InterruptByClass(3, 1, 1);
        }
    }
    gUseGetReport = 0;
    return 1;
}

void EnumerateBindMouseAfterKeyboard(void) {
    /*
     * 真机有线键鼠：优先其它口独立鼠（两 slot，利于保键盘）。
     * 刀 #117：#115 延到桌面后再 EnumerateHubChildrenForMouse 会 Reset hub 子口，
     * MSC 认盘后的二次枚举易把键鼠/中断弄死 →「进桌面不动」。改回枚举期绑。
     */
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumerateHubChildrenForMouse();
    }
    if (gMouseSlotId == 0) {
        for (UINT32 P = 1; P <= gMaxPorts; P++) {
            if (P == gPort1) {
                continue;
            }
            /* 占用门闩在 MouseClaimPortInitialize → XhciRootPortIsOwned */
            if (MousePortInitialize(P)) {
                BootLog("Boot: XHCI mouse on other port\n");
                break;
            }
        }
    }
    /*
     * 真机：禁止同 slot 复合加鼠。CX 2.4G 等 Boot 键+鼠棒无独立鼠口时，
     * 旧 fallback 会弄死键 IN（k=0）；保键优先。QEMU 仍可 composite。
     */
    if (gMouseSlotId == 0) {
        if (HalCpuIsHypervisor()) {
            BootLog("Boot: XHCI mouse fallback composite-on-kbd\n");
            (void)MouseKeyboardSlotInitialize();
        } else if (gSlotId != 0 && gIntrDci != 0) {
            BootLog("Boot: XHCI skip composite-on-kbd (keep kbd)\n");
        }
    }
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumerateHubChildrenForMouse();
    }
}
