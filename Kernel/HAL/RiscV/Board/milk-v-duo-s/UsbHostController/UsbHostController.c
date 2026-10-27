/*
 * UsbHostController.c — Milk-V Duo S USB 主机门面（bd-duo-usb；IP=DWC2）
 *
 * 控制传输 + hub 枚举 + SPLIT HID；根口也可直连 boot 键。
 * 板差：BASE/PHY/VBUS/软复位；DMA 总线地址恒等。
 */
#include "UsbHostController/Private.h"

UINT8 gDmaBuffer[DWC2_DMA_BUF_SIZE] __attribute__((aligned(64)));
UINT8 gSetupBuffer[8] __attribute__((aligned(64)));
UINT8 gStatusBuffer[64] __attribute__((aligned(64)));
UINT32 gRootSpeed;
UINT16 gEndpoint0MaxPacketSize = 64;
UINT8 gHubAddress;
UINT8 gTransferHub;
UINT8 gTransferPort;
int gTransferDoSplit;
int gTransferLowSpeed;
UINT16 gTransferEndpoint0MaxPacketSize = 64;

int gMouseOk;
UINT8 gMouseAddress;
UINT8 gMouseEndpoint;
UINT16 gMouseMaxPacketSize;
UINT8 gMouseToggle;
UINT8 gMouseHub;
UINT8 gMousePort;
int gMouseSplit;
int gMouseLowSpeed;
UINT32 gMouseX;
UINT32 gMouseY;
int gMouseAbsoluteInitialized;
UINT32 gMouseQueueX[MOUSE_QUEUE_LENGTH];
UINT32 gMouseQueueY[MOUSE_QUEUE_LENGTH];
UINT8 gMouseQueueButtons[MOUSE_QUEUE_LENGTH];
INT8 gMouseQueueWheel[MOUSE_QUEUE_LENGTH];
UINT8 gMouseQueueHead;
UINT8 gMouseQueueTail;

int gKeyboardOk;
UINT8 gKeyboardAddress;
UINT8 gKeyboardEndpoint;
UINT8 gKeyboardInterfaceIndex;
UINT16 gKeyboardEndpoint0MaxPacketSize;
UINT16 gKeyboardMaxPacketSize;
UINT8 gKeyboardToggle;
UINT8 gKeyboardHub;
UINT8 gKeyboardPort;
int gKeyboardSplit;
int gKeyboardLowSpeed;
HAL_KEYBOARD_REPORT gKeyboardQueue[KEYBOARD_QUEUE_LENGTH];
UINT8 gKeyboardQueueHead;
UINT8 gKeyboardQueueTail;

UINT8 gNextAddress = 2;
UINT8 gPortDone;

int BoardUsbHostControllerProbe(void) {
    UINT32 Id;
    UINT32 Hprt;
    UINT32 Gusbcfg;
    UINT32 Spd;

    DuoUsbHardwareInitialize();

    Id = *Dwc2Register(DWC2_GSNPSID);
    HalSerialBootMark("Boot: DWC2 GSNPSID=");
    Dwc2Hex32(Id);
    HalSerialBootMark("\n");

    if ((Id >> 16) != 0x4F54u) {
        HalSerialBootMark("Boot: DWC2 id unexpected\n");
        return -1;
    }

    if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_AHBIDLE, DWC2_GRSTCTL_AHBIDLE,
                     100000u) != 0) {
        HalSerialBootMark("Boot: DWC2 AHB busy\n");
        return -1;
    }
    /*
     * Core Soft Reset：GSNPSID≥4.20a（本片 4f54420a）须等 CSFTRST_DONE，
     * 再清 CSFTRST|写 DONE（Linux dwc2_core_reset / U-Boot dwc2_core_reset）。
     * 旧路径等 CSFTRST→0 在 4.20a 上会假 hang（真机已见）。
     */
    *Dwc2Register(DWC2_GRSTCTL) = DWC2_GRSTCTL_CSFTRST;
    if ((Id & DWC2_CORE_REV_MASK) >= (DWC2_CORE_REV_4_20A & DWC2_CORE_REV_MASK)) {
        if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_CSFTRST_DONE,
                         DWC2_GRSTCTL_CSFTRST_DONE, 100000u) != 0) {
            HalSerialBootMark("Boot: DWC2 soft-reset DONE hang\n");
            return -1;
        }
        {
            UINT32 Gr = *Dwc2Register(DWC2_GRSTCTL);
            Gr &= ~DWC2_GRSTCTL_CSFTRST;
            Gr |= DWC2_GRSTCTL_CSFTRST_DONE;
            *Dwc2Register(DWC2_GRSTCTL) = Gr;
        }
    } else if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_CSFTRST, 0, 100000u) !=
               0) {
        HalSerialBootMark("Boot: DWC2 soft-reset hang\n");
        return -1;
    }
    if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_AHBIDLE, DWC2_GRSTCTL_AHBIDLE,
                     100000u) != 0) {
        HalSerialBootMark("Boot: DWC2 AHB busy after rst\n");
        return -1;
    }
    Dwc2Spin(10000);

    /* Force host + UTMI 16-bit（Linux dwc2_set_cv1800_params） */
    Gusbcfg = *Dwc2Register(DWC2_GUSBCFG);
    Gusbcfg &= ~DWC2_GUSBCFG_FORCEDEV;
    Gusbcfg |= DWC2_GUSBCFG_FORCEHOST | DWC2_GUSBCFG_PHYIF;
    *Dwc2Register(DWC2_GUSBCFG) = Gusbcfg;
    Dwc2Spin(10000);

    *Dwc2Register(DWC2_PCGCCTL) = 0;
    /*
     * DMA + HBSTLEN_INCR16。mainline 对 cv1800 标 host_dma=false；
     * 本仓 Pi 路径与多数 U-Boot dwc2 host 用 DMA——先同形；DESC bogus 再查。
     */
    *Dwc2Register(DWC2_GAHBCFG) =
        DWC2_GAHBCFG_DMAEN | DWC2_GAHBCFG_GLBLINTRMSK |
        (DWC2_GAHBCFG_HBSTLEN_INCR16 << DWC2_GAHBCFG_HBSTLEN_SHIFT);
    /* HS PHY：FSLSPClkSel=30/60MHz（U-Boot init_fslspclksel）；勿留复位后不定值 */
    *Dwc2Register(DWC2_HCFG) = DWC2_HCFG_FSLSPCLKSEL_30_60_MHZ;

    Dwc2FlushTransmitFifo(0x10u);
    Dwc2FlushReceiveFifo();
    Dwc2HaltChannels();

    Hprt = *Dwc2Register(DWC2_HPRT0);
    Hprt |= DWC2_HPRT0_PRTPWR;
    Dwc2HostPortWrite(Hprt);
    /* 口选/VBUS 后设备上电；100ms 偏紧，对齐 usb-host.sh 量级再读 CONN */
    Dwc2DelayMilliseconds(500);

    Hprt = *Dwc2Register(DWC2_HPRT0);
    HalSerialBootMark("Boot: DWC2 HPRT0 pre=");
    Dwc2Hex32(Hprt);
    HalSerialBootMark("\n");

    if ((Hprt & DWC2_HPRT0_PRTCONNSTS) == 0) {
        HalSerialBootMark("Boot: DWC2 no device on root port\n");
        return 0;
    }

    if (Dwc2RootPortReset() != 0) {
        HalSerialBootMark("Boot: DWC2 root reset timeout\n");
        Hprt = *Dwc2Register(DWC2_HPRT0);
        HalSerialBootMark("Boot: DWC2 HPRT0=");
        Dwc2Hex32(Hprt);
        HalSerialBootMark("\n");
        return -1;
    }

    Hprt = *Dwc2Register(DWC2_HPRT0);
    Spd = Hprt & DWC2_HPRT0_PRTSPD_MASK;
    gRootSpeed = Spd;
    if (Spd == DWC2_HPRT0_PRTSPD_HS) {
        gEndpoint0MaxPacketSize = 64;
    } else if (Spd == DWC2_HPRT0_PRTSPD_FS) {
        gEndpoint0MaxPacketSize = 64;
    } else {
        gEndpoint0MaxPacketSize = 8;
    }

    HalSerialBootMark("Boot: DWC2 HPRT0 post=");
    Dwc2Hex32(Hprt);
    HalSerialBootMark(Spd == DWC2_HPRT0_PRTSPD_HS ? " HS\n" :
                      Spd == DWC2_HPRT0_PRTSPD_FS ? " FS\n" :
                      Spd == DWC2_HPRT0_PRTSPD_LS ? " LS\n" : " ?\n");

    /* 根口复位后等 hub 醒：对齐 U-Boot dwc2_init_common 1s */
    Dwc2DelayMilliseconds(1000);
    if (EnumerateRootHub() != 0) {
        return -1;
    }
    return 0;
}
