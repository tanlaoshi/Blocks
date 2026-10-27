/*
 * HubScan.c — hub 多轮扫口（bd-duo-usb）
 */
#include "UsbHostController/Private.h"

void HubScanPortsOnce(UINT8 Hub, UINT8 PortCount, UINT8 Pass) {
    UINT8 P;
    UINT16 Status;
    UINT16 Change;

    HalSerialBootMark("Boot: DWC2 hub scan pass=");
    Dwc2Hex16(Pass);
    HalSerialBootMark("\n");

    for (P = 1; P <= PortCount; P++) {
        if ((gPortDone & (UINT8)(1u << P)) != 0) {
            continue;
        }
        if (gMouseOk && gKeyboardOk) {
            return;
        }
        TransferTargetHub();
        if (HubGetPortStatus(Hub, P, &Status, &Change) != 0) {
            HalSerialBootMark("Boot: DWC2 hub+");
            Dwc2Hex16(P);
            HalSerialBootMark(" status fail\n");
            continue;
        }
        HalSerialBootMark("Boot: DWC2 hub+");
        Dwc2Hex16(P);
        HalSerialBootMark(" st=");
        Dwc2Hex16(Status);
        if ((Status & HUB_STAT_CONNECT) == 0) {
            HalSerialBootMark("\n");
            continue;
        }
        /* 速度位仅在 Enable 后有效（USB2 §11.24.2.7）→ 先 reset 再判 */
        (void)HubSetPortFeature(Hub, P, HUB_FEAT_C_PORT_CONNECTION, 0);
        if (HubResetPort(Hub, P, &Status) != 0) {
            HalSerialBootMark(" rst fail\n");
            continue;
        }
        HalSerialBootMark(" rst=");
        Dwc2Hex16(Status);
        if ((Status & HUB_STAT_HIGH_SPEED) != 0) {
            HalSerialBootMark(" HS (skip HID)\n");
            gPortDone |= (UINT8)(1u << P); /* 网口等：勿每轮再 reset */
            continue;
        }
        HalSerialBootMark(Status & HUB_STAT_LOW_SPEED ? " LS\n" : " FS\n");
        Dwc2DelayMilliseconds(50); /* 口复位恢复；再 SPLIT */
        if (!gMouseOk || !gKeyboardOk) {
            if (EnumerateHidOnPort(Hub, P, Status) > 0) {
                gPortDone |= (UINT8)(1u << P);
            }
            TransferTargetHub();
        }
    }
}

void HubScanPorts(UINT8 Hub) {
    UsbSetup Setup;
    UINT8 HubDesc[16];
    UINT8 PortCount;
    UINT8 PwrGood;
    UINT8 P;
    UINT8 Pass;
    int Ret;

    TransferTargetHub();
    /* SET_CONFIGURATION(1) */
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 9;
    Setup.WValue = 1;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlMessage(Hub, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 SET_CFG fail\n");
        return;
    }

    /* GET_DESCRIPTOR hub class 0x29 */
    Setup.BmRequestType = 0xA0;
    Setup.BRequest = 6;
    Setup.WValue = 0x2900;
    Setup.WIndex = 0;
    Setup.WLength = 9;
    Ret = ControlMessage(Hub, &Setup, HubDesc, 9);
    if (Ret < 3) {
        HalSerialBootMark("Boot: DWC2 hub-class DESC fail\n");
        return;
    }
    PortCount = HubDesc[2];
    if (PortCount == 0) {
        PortCount = 4;
    }
    if (PortCount > 8) {
        PortCount = 8;
    }
    PwrGood = HubDesc[5];
    if (PwrGood == 0) {
        PwrGood = 50;
    }
    HalSerialBootMark("Boot: DWC2 hub ports=");
    Dwc2Hex16(PortCount);
    HalSerialBootMark("\n");

    for (P = 1; P <= PortCount; P++) {
        (void)HubSetPortFeature(Hub, P, HUB_FEAT_PORT_POWER, 1);
    }
    /*
     * bPwrOn2PwrGood×2ms（USB2 §11.23.2.1）+ 额外 settle：
     * 我们 CSFTRST 后键常比鼠慢亮 CONNECT（U-Boot 曾用过的键也会重枚）。
     */
    Dwc2DelayMilliseconds((UINT32)PwrGood * 2u + 200u);

    gPortDone = 0;
    for (Pass = 1; Pass <= 3; Pass++) {
        HubScanPortsOnce(Hub, PortCount, Pass);
        if (gMouseOk && gKeyboardOk) {
            break;
        }
        if (Pass < 3) {
            Dwc2DelayMilliseconds(300);
        }
    }

    if (gMouseOk || gKeyboardOk) {
        if (DriverInputAttach(&gInputBackend) == 0) {
            HalSerialBootMark("Boot: DWC2 DriverInputAttach");
            if (gMouseOk) {
                HalSerialBootMark(" mouse");
            }
            if (gKeyboardOk) {
                HalSerialBootMark(" kbd");
            }
            HalSerialBootMark("\n");
        } else {
            HalSerialBootMark("Boot: DWC2 DriverInputAttach fail\n");
        }
    }
    if (!gKeyboardOk) {
        HalSerialBootMark("Boot: DWC2 no kbd after hub passes");
        if (gMouseOk) {
            HalSerialBootMark(" (mouse ok)");
        }
        HalSerialBootMark("\n");
    }
}

