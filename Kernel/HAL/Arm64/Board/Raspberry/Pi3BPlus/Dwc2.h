/*
 * Dwc2.h — BCM283x Synopsys DWC2 OTG（Pi3B+ USB）
 *
 * 规格：BCM2835 ARM Peripherals §14 USB；控制器 @ VC 0x7E980000 →
 * ARM 外设窗 0x3F980000（与 U-Boot/Linux dwc2 同源）。
 * 寄存器布局：Synopsys DWC2 / U-Boot drivers/usb/host/dwc2.h。
 */
#ifndef BOARD_PI3_DWC2_H
#define BOARD_PI3_DWC2_H

#include "BootTypes.h"

#define DWC2_BASE           0x3F980000ULL

#define DWC2_GOTGCTL        0x000u
#define DWC2_GAHBCFG        0x008u
#define DWC2_GUSBCFG        0x00Cu
#define DWC2_GRSTCTL        0x010u
#define DWC2_GINTSTS        0x014u
#define DWC2_GSNPSID        0x040u
#define DWC2_GHWCFG2        0x048u
#define DWC2_HCFG           0x400u
#define DWC2_HFIR           0x404u
#define DWC2_HFNUM          0x408u
#define DWC2_HPRT0          0x440u
#define DWC2_PCGCCTL        0xE00u

/* Host channel 0 @ 0x500（U-Boot 同：单通道） */
#define DWC2_HC_CHANNEL     0u
#define DWC2_HC_REG(ch, off) (0x500u + (UINT32)(ch) * 0x20u + (off))
#define DWC2_HCCHAR(ch)     DWC2_HC_REG(ch, 0x00u)
#define DWC2_HCSPLT(ch)     DWC2_HC_REG(ch, 0x04u)
#define DWC2_HCINT(ch)      DWC2_HC_REG(ch, 0x08u)
#define DWC2_HCINTMSK(ch)   DWC2_HC_REG(ch, 0x0Cu)
#define DWC2_HCTSIZ(ch)     DWC2_HC_REG(ch, 0x10u)
#define DWC2_HCDMA(ch)      DWC2_HC_REG(ch, 0x14u)

/* GRSTCTL */
#define DWC2_GRSTCTL_CSFTRST     (1u << 0)
#define DWC2_GRSTCTL_RXFFLSH     (1u << 4)
#define DWC2_GRSTCTL_TXFFLSH     (1u << 5)
#define DWC2_GRSTCTL_TXFNUM_SHIFT 6
#define DWC2_GRSTCTL_AHBIDLE     (1u << 31)

/* GUSBCFG */
#define DWC2_GUSBCFG_FORCEHOST   (1u << 29)
#define DWC2_GUSBCFG_FORCEDEV    (1u << 30)

/* GAHBCFG */
#define DWC2_GAHBCFG_DMAEN       (1u << 5)
#define DWC2_GAHBCFG_GLBLINTRMSK (1u << 0)

/* GHWCFG2 */
#define DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT 14
#define DWC2_GHWCFG2_NUM_HOST_CHAN_MASK  (0xFu << 14)

/* HPRT0 */
#define DWC2_HPRT0_PRTCONNSTS    (1u << 0)
#define DWC2_HPRT0_PRTCONNDET    (1u << 1)
#define DWC2_HPRT0_PRTENA        (1u << 2)
#define DWC2_HPRT0_PRTENCHNG     (1u << 3)
#define DWC2_HPRT0_PRTOVRCURRACT (1u << 4)
#define DWC2_HPRT0_PRTOVRCURRCHNG (1u << 5)
#define DWC2_HPRT0_PRTRST        (1u << 8)
#define DWC2_HPRT0_PRTPWR        (1u << 12)
#define DWC2_HPRT0_PRTSPD_SHIFT  17
#define DWC2_HPRT0_PRTSPD_MASK   (3u << 17)
#define DWC2_HPRT0_PRTSPD_HS     (0u << 17)
#define DWC2_HPRT0_PRTSPD_FS     (1u << 17)
#define DWC2_HPRT0_PRTSPD_LS     (2u << 17)
#define DWC2_HPRT0_W1C_MASK \
    (DWC2_HPRT0_PRTENA | DWC2_HPRT0_PRTCONNDET | DWC2_HPRT0_PRTENCHNG | \
     DWC2_HPRT0_PRTOVRCURRCHNG)

/* HCCHAR */
#define DWC2_HCCHAR_MPS_SHIFT       0
#define DWC2_HCCHAR_EPNUM_SHIFT     11
#define DWC2_HCCHAR_EPDIR           (1u << 15)
#define DWC2_HCCHAR_LSPDDEV         (1u << 17)
#define DWC2_HCCHAR_EPTYPE_SHIFT    18
#define DWC2_HCCHAR_EPTYPE_CTRL     0u
#define DWC2_HCCHAR_EPTYPE_ISOC     1u
#define DWC2_HCCHAR_EPTYPE_BULK     2u
#define DWC2_HCCHAR_EPTYPE_INTR     3u
#define DWC2_HCCHAR_MULTICNT_SHIFT  20
#define DWC2_HCCHAR_DEVADDR_SHIFT   22
#define DWC2_HCCHAR_ODDFRM          (1u << 29)
#define DWC2_HCCHAR_CHDIS           (1u << 30)
#define DWC2_HCCHAR_CHEN            (1u << 31)

/* HCINT */
#define DWC2_HCINT_XFERCOMP   (1u << 0)
#define DWC2_HCINT_CHHLTD     (1u << 1)
#define DWC2_HCINT_AHBERR     (1u << 2)
#define DWC2_HCINT_STALL      (1u << 3)
#define DWC2_HCINT_NAK        (1u << 4)
#define DWC2_HCINT_ACK        (1u << 5)
#define DWC2_HCINT_NYET       (1u << 6)
#define DWC2_HCINT_XACTERR    (1u << 7)
#define DWC2_HCINT_BBLERR     (1u << 8)
#define DWC2_HCINT_FRMOVRUN   (1u << 9)

/* HCSPLT（U-Boot dwc2.h / Synopsys：非周期 SPLIT 须 XACTPOS=ALL） */
#define DWC2_HCSPLT_PRTADDR_SHIFT 0
#define DWC2_HCSPLT_HUBADDR_SHIFT 7
#define DWC2_HCSPLT_XACTPOS_SHIFT 14
#define DWC2_HCSPLT_XACTPOS_ALL   (3u << 14)
#define DWC2_HCSPLT_COMPSPLT      (1u << 16)
#define DWC2_HCSPLT_SPLTENA       (1u << 31)

/* HCTSIZ */
#define DWC2_HCTSIZ_XFERSIZE_SHIFT 0
#define DWC2_HCTSIZ_PKTCNT_SHIFT   19
#define DWC2_HCTSIZ_PID_SHIFT      29
#define DWC2_HC_PID_DATA0   0u
#define DWC2_HC_PID_DATA2   1u
#define DWC2_HC_PID_DATA1   2u
#define DWC2_HC_PID_SETUP   3u

/* 探针 / 主机 + hub + SPLIT HID mouse；成功 0。 */
int BoardDwc2Probe(void);
/* Reveal/换分辨率：驱动 Abs 与 Gui 光标对齐（同 X64 MouseHandoffDesktop） */
void BoardDwc2MouseHandoff(UINT32 CursorX, UINT32 CursorY);

#endif
