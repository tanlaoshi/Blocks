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

/* GRSTCTL */
#define DWC2_GRSTCTL_CSFTRST     (1u << 0)
#define DWC2_GRSTCTL_AHBIDLE     (1u << 31)

/* GUSBCFG */
#define DWC2_GUSBCFG_FORCEHOST   (1u << 29)
#define DWC2_GUSBCFG_FORCEDEV    (1u << 30)

/* GAHBCFG */
#define DWC2_GAHBCFG_DMAEN       (1u << 5)
#define DWC2_GAHBCFG_GLBLINTRMSK (1u << 0)

/* HPRT0 */
#define DWC2_HPRT0_PRTCONNSTS    (1u << 0)
#define DWC2_HPRT0_PRTCONNDET    (1u << 1)
#define DWC2_HPRT0_PRTENA        (1u << 2)
#define DWC2_HPRT0_PRTENCHNG     (1u << 3)
#define DWC2_HPRT0_PRTOVRCURRACT (1u << 4)
#define DWC2_HPRT0_PRTRST        (1u << 8)
#define DWC2_HPRT0_PRTPWR        (1u << 12)
#define DWC2_HPRT0_PRTSPD_SHIFT  17
#define DWC2_HPRT0_PRTSPD_MASK   (3u << 17)

/* 探针 / 主机模式；成功 0。尚未枚举 HID。 */
int BoardDwc2Probe(void);

#endif
