/*
 * Dwc2.c — Pi3B+ DWC2 探针与强制 Host（rpi-7 第一刀）
 *
 * 只做：读 GSNPSID → core soft-reset → force host → HPRT 上电。
 * 不做：通道 DMA、hub 枚举、SPLIT、HID（下一刀）。
 */
#include "Dwc2.h"
#include "Board.h"
#include "Hal.h"
#include "SerialLog.h"

static volatile UINT32 *Dwc2Reg(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(DWC2_BASE + Off);
}

static void Dwc2Spin(UINT32 N) {
    volatile UINT32 i;
    for (i = 0; i < N; i++) {
        __asm__ volatile("nop");
    }
}

static int Dwc2WaitBits(UINT32 Off, UINT32 Mask, UINT32 Want, UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        if ((*Dwc2Reg(Off) & Mask) == Want) {
            return 0;
        }
        Dwc2Spin(100);
    }
    return -1;
}

static void Hex32(UINT32 V) {
    char Buf[9];
    HalSerialFormatHex(Buf, V, 8);
    HalSerialBootMark(Buf);
}

int BoardDwc2Probe(void) {
    UINT32 Id;
    UINT32 Hprt;
    UINT32 Gusbcfg;

    Id = *Dwc2Reg(DWC2_GSNPSID);
    HalSerialBootMark("Boot: DWC2 GSNPSID=");
    Hex32(Id);
    HalSerialBootMark("\n");

    /* Synopsys ID 高 16 位常见 0x4F54（"OT"） */
    if ((Id >> 16) != 0x4F54u) {
        HalSerialBootMark("Boot: DWC2 id unexpected\n");
        return -1;
    }

    if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_AHBIDLE, DWC2_GRSTCTL_AHBIDLE,
                     100000u) != 0) {
        HalSerialBootMark("Boot: DWC2 AHB busy\n");
        return -1;
    }
    *Dwc2Reg(DWC2_GRSTCTL) = DWC2_GRSTCTL_CSFTRST;
    if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_CSFTRST, 0, 100000u) != 0) {
        HalSerialBootMark("Boot: DWC2 soft-reset hang\n");
        return -1;
    }
    Dwc2Spin(10000);

    Gusbcfg = *Dwc2Reg(DWC2_GUSBCFG);
    Gusbcfg &= ~DWC2_GUSBCFG_FORCEDEV;
    Gusbcfg |= DWC2_GUSBCFG_FORCEHOST;
    *Dwc2Reg(DWC2_GUSBCFG) = Gusbcfg;
    Dwc2Spin(10000);

    *Dwc2Reg(DWC2_GAHBCFG) = DWC2_GAHBCFG_DMAEN | DWC2_GAHBCFG_GLBLINTRMSK;
    *Dwc2Reg(DWC2_HCFG) = 0; /* 30 MHz / FS PHY 默认；后续按 HPRT 速度再调 */

    Hprt = *Dwc2Reg(DWC2_HPRT0);
    Hprt &= ~(DWC2_HPRT0_PRTENA | DWC2_HPRT0_PRTCONNDET | DWC2_HPRT0_PRTENCHNG);
    Hprt |= DWC2_HPRT0_PRTPWR;
    *Dwc2Reg(DWC2_HPRT0) = Hprt;
    Dwc2Spin(200000);

    Hprt = *Dwc2Reg(DWC2_HPRT0);
    HalSerialBootMark("Boot: DWC2 HPRT0=");
    Hex32(Hprt);
    HalSerialBootMark("\n");

    if ((Hprt & DWC2_HPRT0_PRTCONNSTS) == 0) {
        HalSerialBootMark("Boot: DWC2 no device on root port\n");
        /* 3B+ 板上 LAN hub 应在；无连接仍算探针过，枚举另刀 */
    } else {
        HalSerialBootMark("Boot: DWC2 root port connected\n");
    }

    return 0;
}
