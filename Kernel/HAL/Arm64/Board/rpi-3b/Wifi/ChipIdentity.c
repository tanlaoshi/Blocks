/*
 * ChipIdentity.c — F1 ChipCommon 身份 + 固件路径（pwifi-1/2）
 */
#include "Wifi/Private.h"

int gWifiChipProbed;
int gWifiFirmwarePresent;
int gWifiFirmwareLoaded;
UINT32 gWifiChipIdentity;
UINT32 gWifiChipRevision;

static int WifiClockForceActiveLowPower(void) {
    UINT8 Status = 0;
    UINT32 Attempt;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, 0) != 0) {
        return -1;
    }
    SdhciDelayMicroseconds(10);
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)(WIFI_CLOCK_NO_HARDWARE_REQ | WIFI_CLOCK_REQUEST_ALP)) !=
        0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 100u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
            return -1;
        }
        if ((Status & (WIFI_CLOCK_ALP_AVAILABLE | WIFI_CLOCK_HT_AVAILABLE)) != 0) {
            break;
        }
        SdhciDelayMicroseconds(10);
    }
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)(WIFI_CLOCK_NO_HARDWARE_REQ | WIFI_CLOCK_FORCE_ALP)) !=
        0) {
        return -1;
    }
    SdhciDelayMicroseconds(65);
    return 0;
}

int WifiChipIdentityProbe(void) {
    UINT32 Signature = 0;
    UINT32 ChipIdentity;

    gWifiChipProbed = 0;
    gWifiFirmwareLoaded = 0;
    gWifiChipIdentity = 0;
    gWifiChipRevision = 0;

    (void)WifiClockForceActiveLowPower();

    if (WifiBackplaneRead32(WIFI_CHIPCOMMON_BASE, &Signature) != 0) {
        HalSerialBootMark("Boot: Pi WIFI chip read fail\n");
        return -1;
    }
    ChipIdentity = Signature & 0xFFFFu;
    gWifiChipIdentity = ChipIdentity;
    gWifiChipRevision = (Signature >> 16) & 0xFu;

    {
        char Buffer[9];
        HalSerialBootMark("Boot: Pi WIFI chip=");
        HalSerialFormatHex(Buffer, ChipIdentity, 4);
        HalSerialBootMark(Buffer);
        HalSerialBootMark(" rev=");
        HalSerialFormatHex(Buffer, gWifiChipRevision, 1);
        HalSerialBootMark(Buffer);
        HalSerialBootMark(" sig=");
        HalSerialFormatHex(Buffer, Signature, 8);
        HalSerialBootMark(Buffer);
        HalSerialBootMark("\n");
    }

    if (ChipIdentity == WIFI_CHIP_ID_43455) {
        HalSerialBootMark("Boot: Pi WIFI name=BCM43455\n");
    } else if (ChipIdentity == WIFI_CHIP_ID_43430) {
        HalSerialBootMark("Boot: Pi WIFI name=BCM43430\n");
    } else {
        HalSerialBootMark("Boot: Pi WIFI chip unsupported\n");
        return -1;
    }
    gWifiChipProbed = 1;
    return 0;
}

const char *WifiFirmwarePathForChip(void) {
    if (gWifiChipIdentity == WIFI_CHIP_ID_43455) {
        return WIFI_FIRMWARE_PATH_43455;
    }
    if (gWifiChipIdentity == WIFI_CHIP_ID_43430) {
        /*
         * 对表 Linux brcmfmac/sdio.c firmware map（revmask & BIT(chiprev)）：
         *   rev0 → brcmfmac43430a0-sdio；rev1 → brcmfmac43430-sdio（A1）。
         * Pi3B 为 a9a6/rev1；错灌 a0 → ARM 起来但 HT 永不置位（csr=0x50）。
         */
        if (gWifiChipRevision == 0u) {
            return WIFI_FIRMWARE_PATH_43430_A0;
        }
        return WIFI_FIRMWARE_PATH_43430_A1;
    }
    return 0;
}

const char *WifiNvramPathForChip(void) {
    if (gWifiChipIdentity == WIFI_CHIP_ID_43430 || gWifiChipIdentity == WIFI_CHIP_ID_43455) {
        return WIFI_NVRAM_PATH_43430;
    }
    return 0;
}

void WifiFirmwarePathProbe(void) {
    const char *Path;
    UINT8 Probe[4];
    UINTN Size = 0;
    int Error;

    gWifiFirmwarePresent = 0;
    Path = WifiFirmwarePathForChip();
    if (Path == 0) {
        HalSerialBootMark("Boot: Pi WIFI fw path unknown chip\n");
        return;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI fw=");
    HalSerialBootMarkForce(Path);
    HalSerialBootMarkForce("\n");

    Error = HalDriverFileRead(Path, Probe, sizeof(Probe), &Size);
    if (Error != 0 || Size == 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI fw miss (place on BLOCKS)\n");
        return;
    }
    gWifiFirmwarePresent = 1;
    HalSerialBootMarkForce("Boot: Pi WIFI fw present\n");
}
