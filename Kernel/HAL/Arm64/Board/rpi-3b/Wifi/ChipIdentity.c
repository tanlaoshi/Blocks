/*
 * ChipIdentity.c — F1 backplane ChipCommon 身份（pwifi-1）
 */
#include "Wifi/Private.h"

int gWifiChipProbed;
int gWifiFirmwarePresent;
UINT32 gWifiChipIdentity;
UINT32 gWifiChipRevision;

static int WifiBackplaneWindowSet(UINT32 Address) {
    UINT32 Window = Address & ~(SDIO_BACKPLANE_WINDOW_SIZE - 1u);

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_BACKPLANE_ADDRESS,
                               (UINT8)((Window >> 8) & 0xFFu)) != 0) {
        return -1;
    }
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_BACKPLANE_ADDRESS + 1u,
                               (UINT8)((Window >> 16) & 0xFFu)) != 0) {
        return -1;
    }
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_BACKPLANE_ADDRESS + 2u,
                               (UINT8)((Window >> 24) & 0xFFu)) != 0) {
        return -1;
    }
    return 0;
}

static int WifiBackplaneRead32(UINT32 Address, UINT32 *OutValue) {
    UINT8 Buffer[4];
    UINT32 Offset;

    if (OutValue == 0) {
        return -1;
    }
    if (WifiBackplaneWindowSet(Address) != 0) {
        return -1;
    }
    Offset = (Address & (SDIO_BACKPLANE_WINDOW_SIZE - 1u)) | SDIO_BACKPLANE_ACCESS_32BIT;
    if (WifiSdioCommand53Read(SDIO_FUNCTION_1, Offset, Buffer, 4u) != 0) {
        return -1;
    }
    *OutValue = (UINT32)Buffer[0] | ((UINT32)Buffer[1] << 8) | ((UINT32)Buffer[2] << 16) |
                ((UINT32)Buffer[3] << 24);
    return 0;
}

static int WifiClockForceActiveLowPower(void) {
    UINT8 Status = 0;
    UINT32 Attempt;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CLOCK_CONTROL_STATUS, 0) != 0) {
        return -1;
    }
    SdhciDelayMicroseconds(10);
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CLOCK_CONTROL_STATUS,
                               (UINT8)(SDIO_CLOCK_NO_HARDWARE_REQ | SDIO_CLOCK_REQUEST_ALP)) !=
        0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 100u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CLOCK_CONTROL_STATUS, &Status) != 0) {
            return -1;
        }
        if ((Status & (SDIO_CLOCK_ALP_AVAILABLE | SDIO_CLOCK_HT_AVAILABLE)) != 0) {
            break;
        }
        SdhciDelayMicroseconds(10);
    }
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CLOCK_CONTROL_STATUS,
                               (UINT8)(SDIO_CLOCK_NO_HARDWARE_REQ | SDIO_CLOCK_FORCE_ALP)) !=
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
    gWifiChipIdentity = 0;
    gWifiChipRevision = 0;

    (void)WifiClockForceActiveLowPower();

    if (WifiBackplaneRead32(SDIO_CHIPCOMMON_BASE, &Signature) != 0) {
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
        return WIFI_FIRMWARE_PATH_43430;
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
    HalSerialBootMark("Boot: Pi WIFI fw=");
    HalSerialBootMark(Path);
    HalSerialBootMark("\n");

    Error = HalDriverFileRead(Path, Probe, sizeof(Probe), &Size);
    if (Error != 0 || Size == 0) {
        HalSerialBootMark("Boot: Pi WIFI fw miss (place on BLOCKS; load later)\n");
        return;
    }
    gWifiFirmwarePresent = 1;
    HalSerialBootMark("Boot: Pi WIFI fw present\n");
}
