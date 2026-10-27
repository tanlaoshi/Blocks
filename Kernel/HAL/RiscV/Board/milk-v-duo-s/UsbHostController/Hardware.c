/*
 * Hardware.c — Duo TOP/PHY/VBUS 板级上电（bd-duo-usb）
 */
#include "UsbHostController/Private.h"

volatile UINT32 *DuoTopRegister(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

/*
 * 板级：Type-A 口选通 + VBUS + TOP USB 复位 + PHY host。
 * 口选序对齐厂商 usb-host.sh（gpio510=PORT_SEL↑ → gpio453=VBUS↑）；
 * PHY/复位对齐 U-Boot board_usb_init + mainline phy-cv1800 HOST。
 */
void DuoUsbHardwareInitialize(void) {
    UINT32 Value;

    /*
     * Duo S：Type-A 与 Type-C 互斥。默认走 Type-C；HOST 须 USB_PORT_SEL=1
     *（XGPIOA[30] / Linux gpio 510，pad AUX0）。再开 VBUS_EN=XGPIOB[5]。
     */
    Value = *DuoTopRegister(DUO_FMUX_AUX0);
    Value = (Value & ~7u) | DUO_AUX0_XGPIOA30;
    *DuoTopRegister(DUO_FMUX_AUX0) = Value;
    Value = *DuoTopRegister(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DDR);
    *DuoTopRegister(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DDR) =
        Value | DUO_PORT_SEL_BIT;
    Value = *DuoTopRegister(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DR);
    *DuoTopRegister(DUO_GPIOA_BASE + DUO_GPIO_SWPORT_DR) =
        Value | DUO_PORT_SEL_BIT;
    Dwc2DelayMilliseconds(500);

    Value = *DuoTopRegister(DUO_FMUX_USB_VBUS_EN);
    Value = (Value & ~7u) | DUO_USB_VBUS_EN_XGPIOB5;
    *DuoTopRegister(DUO_FMUX_USB_VBUS_EN) = Value;
    Value = *DuoTopRegister(DUO_GPIOB_BASE + DUO_GPIO_SWPORT_DDR);
    *DuoTopRegister(DUO_GPIOB_BASE + DUO_GPIO_SWPORT_DDR) =
        Value | DUO_VBUS_EN_BIT;
    Value = *DuoTopRegister(DUO_GPIOB_BASE + DUO_GPIO_SWPORT_DR);
    *DuoTopRegister(DUO_GPIOB_BASE + DUO_GPIO_SWPORT_DR) =
        Value | DUO_VBUS_EN_BIT;
    Dwc2DelayMilliseconds(500);

    /* TOP soft-reset USB（U-Boot board_usb_init） */
    Value = *DuoTopRegister(DUO_REG_TOP_SOFT_RST);
    *DuoTopRegister(DUO_REG_TOP_SOFT_RST) =
        Value & ~DUO_BIT_TOP_SOFT_RST_USB;
    Dwc2Spin(50000u);
    Value = *DuoTopRegister(DUO_REG_TOP_SOFT_RST);
    *DuoTopRegister(DUO_REG_TOP_SOFT_RST) =
        Value | DUO_BIT_TOP_SOFT_RST_USB;
    Dwc2Spin(50000u);

    /*
     * PHY：EXTVBUS + VBUS_POWER + ID override host（清 ID_VALUE）。
     * U-Boot gadget 置 ID_VALUE=device；host 须清 bit7（mainline MODE_HOST）。
     */
    Value = *DuoTopRegister(DUO_REG_TOP_USB_PHY_CTRL);
    Value |= DUO_BIT_USB_PHY_EXTVBUS | DUO_BIT_USB_PHY_VBUS_PWR |
             DUO_BIT_USB_PHY_ID_OVR;
    Value &= ~DUO_BIT_USB_PHY_ID_VALUE;
    *DuoTopRegister(DUO_REG_TOP_USB_PHY_CTRL) = Value;

    *DuoTopRegister(DUO_REG_TOP_USB_ECO) =
        *DuoTopRegister(DUO_REG_TOP_USB_ECO) | DUO_BIT_USB_ECO_RX_FLUSH;

    HalSerialBootMark("Boot: Duo USB PORT_SEL+VBUS+PHY host\n");
}

/* skip-MMU：VA==PA；仍 civac 供 DWC2 DMA 见缓冲 */
