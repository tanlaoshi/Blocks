/*
 * UsbHostController.c — Pi3B USB 主机门面（bd-pi-usb；IP=DWC2）
 *
 * 控制传输 + hub 枚举 + SPLIT HID boot 鼠/键 → DriverInputAttach。
 * 传输形：U-Boot dwc2；HCSPLT.XACTPOS=ALL（Synopsys 非周期 SPLIT）；
 * hub 类请求对齐 HAL/X64 Hub/Request.c；键解析对齐 X64 HidParse（拒假键）。
 */
#include "UsbHostController/Private.h"

UINT8 gDmaBuffer[DWC2_DMA_BUF_SIZE] __attribute__((aligned(64)));
UINT8 gSetupBuffer[8] __attribute__((aligned(64)));
UINT8 gStatusBuffer[64] __attribute__((aligned(64)));
UINT32 gRootSpeed; /* HPRT PRTSPD_* */
UINT16 gEndpoint0MaxPacketSize = 64;
UINT8 gHubAddress;
/* 当前控制/中断目标：FS/LS 在 HS 根口后须 SPLIT */
UINT8 gTransferHub;
UINT8 gTransferPort;
int gTransferDoSplit;
int gTransferLowSpeed;
UINT16 gTransferEndpoint0MaxPacketSize = 64;

/* HID boot 鼠/键（Poll 先鼠后键） */
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
/*
 * 软复位 DWC2 后全员掉线再上电：鼠常先 CONNECT，键（尤其 LS）可能慢几拍。
 * 多轮扫口；已绑成功的口不再 reset（免弄死鼠）。
 */
UINT8 gPortDone; /* bit P：本口已成功 Enum 过 */
int gEthernetAttached;

int BoardUsbHostControllerProbe(void) {
    UINT32 Id;
    UINT32 Hprt;
    UINT32 Gusbcfg;
    UINT32 Spd;

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
    *Dwc2Register(DWC2_GRSTCTL) = DWC2_GRSTCTL_CSFTRST;
    if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_CSFTRST, 0, 100000u) != 0) {
        HalSerialBootMark("Boot: DWC2 soft-reset hang\n");
        return -1;
    }
    Dwc2Spin(10000);

    Gusbcfg = *Dwc2Register(DWC2_GUSBCFG);
    Gusbcfg &= ~DWC2_GUSBCFG_FORCEDEV;
    Gusbcfg |= DWC2_GUSBCFG_FORCEHOST;
    *Dwc2Register(DWC2_GUSBCFG) = Gusbcfg;
    Dwc2Spin(10000);

    *Dwc2Register(DWC2_PCGCCTL) = 0;
    *Dwc2Register(DWC2_GAHBCFG) = DWC2_GAHBCFG_DMAEN | DWC2_GAHBCFG_GLBLINTRMSK;
    /* HS PHY：FSLSPClkSel=30/60MHz（U-Boot init_fslspclksel）；勿留复位后不定值 */
    *Dwc2Register(DWC2_HCFG) = DWC2_HCFG_FSLSPCLKSEL_30_60_MHZ;

    Dwc2FlushTransmitFifo(0x10u);
    Dwc2FlushReceiveFifo();
    Dwc2HaltChannels();

    Hprt = *Dwc2Register(DWC2_HPRT0);
    Hprt |= DWC2_HPRT0_PRTPWR;
    Dwc2HostPortWrite(Hprt);
    Dwc2DelayMilliseconds(100);

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
