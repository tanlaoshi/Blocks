/*
 * Mac.c — SMSC MAC / PHY 初始化（bd-pi-eth）
 */
#include "Ethernet/Private.h"

static int MacValid(const UINT8 *M) {
    return (M[0] | M[1] | M[2] | M[3] | M[4] | M[5]) != 0 &&
           (M[0] & 1u) == 0;
}

int SmscMacInitialize(void) {
    UINT32 Lo;
    UINT32 Hi;
    UINT32 T;
    UINT32 V;

    if (SmscWriteRegister(HW_CFG, HW_CFG_LRST) != 0) {
        return -1;
    }
    for (T = 0; T < 100u; T++) {
        if (SmscReadRegister(HW_CFG, &V) != 0) {
            return -1;
        }
        if ((V & HW_CFG_LRST) == 0) {
            break;
        }
        SmscDelayMilliseconds(10);
    }
    if (SmscWriteRegister(PM_CTRL, PM_CTL_PHY_RST) != 0) {
        return -1;
    }
    for (T = 0; T < 100u; T++) {
        if (SmscReadRegister(PM_CTRL, &V) != 0) {
            return -1;
        }
        if ((V & PM_CTL_PHY_RST) == 0) {
            break;
        }
        SmscDelayMilliseconds(10);
    }

    if (SmscEepromRead(EEPROM_MAC_OFF, gSmscMac, 6) != 0 || !MacValid(gSmscMac)) {
        /* U-Boot 无 usbethaddr 时失败；给可辨本地址供课堂 */
        gSmscMac[0] = 0xB8;
        gSmscMac[1] = 0x27;
        gSmscMac[2] = 0xEB;
        gSmscMac[3] = 0x00;
        gSmscMac[4] = 0x00;
        gSmscMac[5] = 0x01;
        HalSerialBootMark("Boot: SMSC MAC fallback\n");
    }

    Lo = (UINT32)gSmscMac[0] | ((UINT32)gSmscMac[1] << 8) | ((UINT32)gSmscMac[2] << 16) |
         ((UINT32)gSmscMac[3] << 24);
    Hi = (UINT32)gSmscMac[4] | ((UINT32)gSmscMac[5] << 8);
    if (SmscWriteRegister(ADDRL, Lo) != 0 || SmscWriteRegister(ADDRH, Hi) != 0) {
        return -1;
    }

    if (SmscWriteRegister(BURST_CAP, 0) != 0) {
        return -1;
    }
    if (SmscWriteRegister(BULK_IN_DLY, DEFAULT_BULK_IN_DELAY) != 0) {
        return -1;
    }
    if (SmscReadRegister(HW_CFG, &V) != 0) {
        return -1;
    }
    V &= ~HW_CFG_RXDOFF;
    if (SmscWriteRegister(HW_CFG, V) != 0) {
        return -1;
    }
    if (SmscWriteRegister(INT_STS, 0xFFFFFFFFu) != 0) {
        return -1;
    }
    if (SmscReadRegister(ID_REV, &V) != 0) {
        return -1;
    }
    HalSerialBootMark("Boot: SMSC ID_REV=");
    {
        char Hex[9];
        HalSerialFormatHex(Hex, V, 8);
        HalSerialBootMark(Hex);
        HalSerialBootMark("\n");
    }
    if (SmscWriteRegister(LED_GPIO_CFG, LED_GPIO_CFG_LEDS) != 0) {
        return -1;
    }
    if (SmscWriteRegister(FLOW, 0) != 0 ||
        SmscWriteRegister(AFC_CFG, AFC_CFG_DEFAULT) != 0) {
        return -1;
    }
    if (SmscReadRegister(MAC_CR, &gSmscMacControl) != 0) {
        return -1;
    }
    if (SmscWriteRegister(VLAN1, 0x8100u) != 0) {
        return -1;
    }
    if (SmscWriteRegister(COE_CR, 0) != 0) {
        return -1;
    }
    gSmscMacControl &= ~(MAC_CR_PRMS | MAC_CR_MCPAS | MAC_CR_HPFILT);
    if (SmscWriteRegister(MAC_CR, gSmscMacControl) != 0) {
        return -1;
    }

    (void)SmscMiiRead(PHY_INT_SRC, 0);
    if (SmscMiiWrite(PHY_INT_MASK, PHY_INT_MASK_DEFAULT) != 0) {
        return -1;
    }
    if (SmscReadRegister(INT_EP_CTL, &V) != 0) {
        return -1;
    }
    V |= INT_EP_CTL_PHY_INT;
    if (SmscWriteRegister(INT_EP_CTL, V) != 0) {
        return -1;
    }

    gSmscMacControl |= MAC_CR_TXEN;
    if (SmscWriteRegister(MAC_CR, gSmscMacControl) != 0 ||
        SmscWriteRegister(TX_CFG, TX_CFG_ON) != 0) {
        return -1;
    }
    gSmscMacControl |= MAC_CR_RXEN;
    if (SmscWriteRegister(MAC_CR, gSmscMacControl) != 0) {
        return -1;
    }
    return 0;
}
