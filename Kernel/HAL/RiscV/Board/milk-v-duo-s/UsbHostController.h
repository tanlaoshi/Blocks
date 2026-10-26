/*
 * UsbHostController.h — Milk-V Duo S USB 主机（Synopsys DWC2 OTG）
 *
 * 门面名对齐 X64 `UsbHostController` / Pi 板包。寄存器宏 DWC2_* 对表 U-Boot dwc2.h。
 * 控制器 @ 0x04340000；PHY/ID/VBUS：TOP 0x03000048；Type-A VBUS_EN = XGPIOB[5]。
 */
#ifndef BOARD_USB_HOST_CONTROLLER_H
#define BOARD_USB_HOST_CONTROLLER_H

#include "BootTypes.h"

#define DWC2_BASE           0x04340000ULL

/* TOP / PHY / VBUS（U-Boot board/cvitek/cv181x/cv181x_reg.h） */
#define DUO_TOP_BASE              0x03000000ULL
#define DUO_PINMUX_BASE           (DUO_TOP_BASE + 0x1000u)
#define DUO_REG_TOP_SOFT_RST      (DUO_TOP_BASE + 0x3000u)
#define DUO_BIT_TOP_SOFT_RST_USB  (1u << 11)
#define DUO_REG_TOP_USB_PHY_CTRL  (DUO_TOP_BASE + 0x48u)
#define DUO_BIT_USB_PHY_EXTVBUS   (1u << 0)
#define DUO_BIT_USB_PHY_VBUS_PWR  (1u << 1) /* mainline PHY_VBUS_POWER */
#define DUO_BIT_USB_PHY_ID_OVR    (1u << 6)
#define DUO_BIT_USB_PHY_ID_VALUE  (1u << 7) /* 1=device，0=host（mainline） */
#define DUO_REG_TOP_USB_ECO       (DUO_TOP_BASE + 0xB4u)
#define DUO_BIT_USB_ECO_RX_FLUSH  0x80u
#define DUO_FMUX_USB_VBUS_EN      (DUO_PINMUX_BASE + 0x100u)
#define DUO_USB_VBUS_EN_XGPIOB5   3u /* USB_VBUS_EN__XGPIOB_5 */
#define DUO_FMUX_AUX0             (DUO_PINMUX_BASE + 0x78u) /* USB_PORT_SEL pad */
#define DUO_AUX0_XGPIOA30         3u /* AUX0__XGPIOA_30 */
#define DUO_GPIOA_BASE            0x03020000ULL
#define DUO_GPIOB_BASE            0x03021000ULL
#define DUO_GPIO_SWPORT_DR        0x00u
#define DUO_GPIO_SWPORT_DDR       0x04u
#define DUO_PORT_SEL_BIT          (1u << 30) /* XGPIOA[30]；usb-host.sh gpio 510 */
#define DUO_VBUS_EN_BIT           (1u << 5)  /* XGPIOB[5]；usb-host.sh gpio 453 */

#define DWC2_GOTGCTL        0x000u
#define DWC2_GAHBCFG        0x008u
#define DWC2_GUSBCFG        0x00Cu
#define DWC2_GRSTCTL        0x010u
#define DWC2_GINTSTS        0x014u
#define DWC2_GSNPSID        0x040u
#define DWC2_GHWCFG2        0x048u
#define DWC2_HCFG           0x400u
#define DWC2_HCFG_FSLSPCLKSEL_30_60_MHZ 0u
#define DWC2_HCFG_FSLSPCLKSEL_MASK      (3u << 0)
#define DWC2_HFIR           0x404u
#define DWC2_HFNUM          0x408u
#define DWC2_HPRT0          0x440u
#define DWC2_PCGCCTL        0xE00u

#define DWC2_HC_CHANNEL     0u
#define DWC2_HC_REG(ch, off) (0x500u + (UINT32)(ch) * 0x20u + (off))
#define DWC2_HCCHAR(ch)     DWC2_HC_REG(ch, 0x00u)
#define DWC2_HCSPLT(ch)     DWC2_HC_REG(ch, 0x04u)
#define DWC2_HCINT(ch)      DWC2_HC_REG(ch, 0x08u)
#define DWC2_HCINTMSK(ch)   DWC2_HC_REG(ch, 0x0Cu)
#define DWC2_HCTSIZ(ch)     DWC2_HC_REG(ch, 0x10u)
#define DWC2_HCDMA(ch)      DWC2_HC_REG(ch, 0x14u)

#define DWC2_GRSTCTL_CSFTRST     (1u << 0)
#define DWC2_GRSTCTL_RXFFLSH     (1u << 4)
#define DWC2_GRSTCTL_TXFFLSH     (1u << 5)
#define DWC2_GRSTCTL_TXFNUM_SHIFT 6
/* 4.20a+：软复位完成看 DONE（bit29），非等 CSFTRST 自清（Linux/U-Boot dwc2_core_reset） */
#define DWC2_GRSTCTL_CSFTRST_DONE (1u << 29)
#define DWC2_GRSTCTL_AHBIDLE     (1u << 31)
#define DWC2_CORE_REV_4_20A      0x4f54420au
#define DWC2_CORE_REV_MASK       0x0000ffffu

#define DWC2_GUSBCFG_PHYIF       (1u << 3)  /* 1 = UTMI 16-bit（Linux cv1800） */
#define DWC2_GUSBCFG_FORCEHOST   (1u << 29)
#define DWC2_GUSBCFG_FORCEDEV    (1u << 30)

#define DWC2_GAHBCFG_HBSTLEN_SHIFT 1
#define DWC2_GAHBCFG_HBSTLEN_INCR16 5u
#define DWC2_GAHBCFG_DMAEN       (1u << 5)
#define DWC2_GAHBCFG_GLBLINTRMSK (1u << 0)

#define DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT 14
#define DWC2_GHWCFG2_NUM_HOST_CHAN_MASK  (0xFu << 14)

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
#define DWC2_HCINT_DATATGLERR (1u << 10)

#define DWC2_HCSPLT_PRTADDR_SHIFT 0
#define DWC2_HCSPLT_HUBADDR_SHIFT 7
#define DWC2_HCSPLT_XACTPOS_SHIFT 14
#define DWC2_HCSPLT_XACTPOS_ALL   (3u << 14)
#define DWC2_HCSPLT_COMPSPLT      (1u << 16)
#define DWC2_HCSPLT_SPLTENA       (1u << 31)

#define DWC2_HCTSIZ_XFERSIZE_SHIFT 0
#define DWC2_HCTSIZ_PKTCNT_SHIFT   19
#define DWC2_HCTSIZ_PID_SHIFT      29
#define DWC2_HC_PID_DATA0   0u
#define DWC2_HC_PID_DATA2   1u
#define DWC2_HC_PID_DATA1   2u
#define DWC2_HC_PID_SETUP   3u

/* 探针 / 主机 +（hub|根口）HID；成功 0。 */
int BoardUsbHostControllerProbe(void);
void BoardUsbHostControllerMouseHandoff(UINT32 CursorX, UINT32 CursorY);

#endif
