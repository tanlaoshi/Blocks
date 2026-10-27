/*
 * Utility.c — DWC2 寄存器/延时/FIFO（bd-duo-usb）
 */
#include "UsbHostController/Private.h"

volatile UINT32 *Dwc2Register(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DWC2_BASE + Offset);
}

void Dwc2Spin(UINT32 N) {
    volatile UINT32 i;
    for (i = 0; i < N; i++) {
        __asm__ volatile("nop");
    }
}

int Dwc2WaitBits(UINT32 Offset, UINT32 Mask, UINT32 Want, UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        if ((*Dwc2Register(Offset) & Mask) == Want) {
            return 0;
        }
        Dwc2Spin(100);
    }
    return -1;
}

void Dwc2Hex32(UINT32 V) {
    char Buffer[9];
    HalSerialFormatHex(Buffer, V, 8);
    HalSerialBootMark(Buffer);
}

void Dwc2Hex16(UINT16 V) {
    char Buffer[5];
    HalSerialFormatHex(Buffer, V, 4);
    HalSerialBootMark(Buffer);
}

void Dwc2DelayMilliseconds(UINT32 Ms) {
    UINT32 m;
    for (m = 0; m < Ms; m++) {
        Dwc2Spin(80000u);
    }
}

void Dwc2HostPortWrite(UINT32 Val) {
    Val &= ~DWC2_HPRT0_W1C_MASK;
    *Dwc2Register(DWC2_HPRT0) = Val;
}
void Dwc2FlushTransmitFifo(UINT32 Number) {
    *Dwc2Register(DWC2_GRSTCTL) =
        DWC2_GRSTCTL_TXFFLSH | (Number << DWC2_GRSTCTL_TXFNUM_SHIFT);
    (void)Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_TXFFLSH, 0, 100000u);
    Dwc2Spin(1000);
}

void Dwc2FlushReceiveFifo(void) {
    *Dwc2Register(DWC2_GRSTCTL) = DWC2_GRSTCTL_RXFFLSH;
    (void)Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_RXFFLSH, 0, 100000u);
    Dwc2Spin(1000);
}

void Dwc2HaltChannels(void) {
    UINT32 N;
    UINT32 i;
    UINT32 Char;

    N = (*Dwc2Register(DWC2_GHWCFG2) & DWC2_GHWCFG2_NUM_HOST_CHAN_MASK) >>
        DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT;
    N += 1u;
    if (N > 16u) {
        N = 16u;
    }
    for (i = 0; i < N; i++) {
        Char = *Dwc2Register(DWC2_HCCHAR(i));
        Char &= ~(DWC2_HCCHAR_CHEN | DWC2_HCCHAR_EPDIR);
        Char |= DWC2_HCCHAR_CHDIS;
        *Dwc2Register(DWC2_HCCHAR(i)) = Char;
    }
    for (i = 0; i < N; i++) {
        Char = *Dwc2Register(DWC2_HCCHAR(i));
        Char &= ~DWC2_HCCHAR_EPDIR;
        Char |= DWC2_HCCHAR_CHEN | DWC2_HCCHAR_CHDIS;
        *Dwc2Register(DWC2_HCCHAR(i)) = Char;
        (void)Dwc2WaitBits(DWC2_HCCHAR(i), DWC2_HCCHAR_CHEN, 0, 50000u);
    }
}

int Dwc2RootPortReset(void) {
    UINT32 Hprt;
    UINT32 i;

    Hprt = *Dwc2Register(DWC2_HPRT0);
    Hprt |= DWC2_HPRT0_PRTRST;
    Dwc2HostPortWrite(Hprt);
    Dwc2DelayMilliseconds(60);
    Hprt = *Dwc2Register(DWC2_HPRT0);
    Hprt &= ~DWC2_HPRT0_PRTRST;
    Dwc2HostPortWrite(Hprt);

    for (i = 0; i < 100u; i++) {
        Hprt = *Dwc2Register(DWC2_HPRT0);
        if ((Hprt & DWC2_HPRT0_PRTENA) != 0) {
            return 0;
        }
        Dwc2DelayMilliseconds(10);
    }
    return -1;
}

