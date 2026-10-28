/*
 * Backplane.c — SDIO F1 背板窗读写（pwifi-2）
 */
#include "Wifi/Private.h"

UINT32 gWifiBackplaneWindow = 0xFFFFFFFFu;

int WifiBackplaneWindowSet(UINT32 Address) {
    UINT32 Window = Address & ~(SDIO_BACKPLANE_WINDOW_SIZE - 1u);

    if (Window == gWifiBackplaneWindow) {
        return 0;
    }
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
    gWifiBackplaneWindow = Window;
    return 0;
}

void WifiBackplaneWindowInvalidate(void) {
    gWifiBackplaneWindow = 0xFFFFFFFFu;
}

int WifiBackplaneRead32(UINT32 Address, UINT32 *OutValue) {
    UINT8 Buffer[4];
    UINT32 Offset;

    if (OutValue == 0) {
        return -1;
    }
    if (WifiBackplaneWindowSet(Address) != 0) {
        return -1;
    }
    Offset = (Address & (SDIO_BACKPLANE_WINDOW_SIZE - 1u)) | SDIO_BACKPLANE_ACCESS_32BIT;
    if (WifiSdioCommand53Read(SDIO_FUNCTION_1, Offset, Buffer, 4u, 1) != 0) {
        return -1;
    }
    *OutValue = (UINT32)Buffer[0] | ((UINT32)Buffer[1] << 8) | ((UINT32)Buffer[2] << 16) |
                ((UINT32)Buffer[3] << 24);
    return 0;
}

int WifiBackplaneWrite32(UINT32 Address, UINT32 Value) {
    UINT8 Buffer[4];
    UINT32 Offset;

    Buffer[0] = (UINT8)(Value & 0xFFu);
    Buffer[1] = (UINT8)((Value >> 8) & 0xFFu);
    Buffer[2] = (UINT8)((Value >> 16) & 0xFFu);
    Buffer[3] = (UINT8)((Value >> 24) & 0xFFu);
    if (WifiBackplaneWindowSet(Address) != 0) {
        return -1;
    }
    Offset = (Address & (SDIO_BACKPLANE_WINDOW_SIZE - 1u)) | SDIO_BACKPLANE_ACCESS_32BIT;
    return WifiSdioCommand53Write(SDIO_FUNCTION_1, Offset, Buffer, 4u, 1);
}

static int WifiBackplaneMemoryTransfer(UINT32 Address, UINT8 *Data, UINT32 ByteCount,
                                       int Write) {
    UINT32 Remaining = ByteCount;
    UINT32 Current = Address;
    UINT8 *Cursor = Data;
    UINT32 Done = 0;

    if (Data == 0 && ByteCount != 0) {
        return -1;
    }
    /*
     * 对表 Linux brcmf_sdiod_ramrw（Pi Arasan）：窗内偏移 | ACCESS_2_4B（0x8000）。
     * cyw43/Pico 灌片不加该位；本板不加则 ARM 起不来、HT 永卡 csr=0x50。
     * 仍禁止在 HT 前做 CMD53 回读（会楔死 DOING_READ）——只写、不验。
     */
    while (Remaining > 0) {
        UINT32 WindowOffset =
            (Current & (SDIO_BACKPLANE_WINDOW_SIZE - 1u)) | SDIO_BACKPLANE_ACCESS_32BIT;
        UINT32 Chunk = SDIO_BACKPLANE_WINDOW_SIZE - (Current & (SDIO_BACKPLANE_WINDOW_SIZE - 1u));
        int Error;

        if (Chunk > Remaining) {
            Chunk = Remaining;
        }
        if (Chunk > WIFI_BACKPLANE_BLOCK_BYTES) {
            Chunk = WIFI_BACKPLANE_BLOCK_BYTES;
        }
        if (Chunk > 4u && (Chunk & 3u) != 0) {
            Chunk &= ~3u;
        }
        if (WifiBackplaneWindowSet(Current) != 0) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI fw window fail @", Current);
            HalSerialBootMarkForce("\n");
            return -1;
        }
        if (Write) {
            Error = WifiSdioCommand53Write(SDIO_FUNCTION_1, WindowOffset, Cursor, Chunk, 1);
        } else {
            Error = WifiSdioCommand53Read(SDIO_FUNCTION_1, WindowOffset, Cursor, Chunk, 1);
        }
        if (Error != 0) {
            WifiBootMarkHexadecimal32(
                Write ? "Boot: Pi WIFI fw write fail @" : "Boot: Pi WIFI fw read fail @",
                Current);
            HalSerialBootMarkForce("\n");
            WifiBootMarkHexadecimal32("Boot: Pi WIFI fw pst=", gSdhciLastPresentState);
            HalSerialBootMarkForce("\n");
            WifiBootMarkHexadecimal32("Boot: Pi WIFI fw isr=", gSdhciLastCommandStatus);
            HalSerialBootMarkForce("\n");
            return -1;
        }
        Current += Chunk;
        Cursor += Chunk;
        Remaining -= Chunk;
        Done += Chunk;
        if (Write && (Done & 0x1FFFu) == 0u) {
            (void)WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                                         (UINT8)(WIFI_CLOCK_REQUEST_ALP | WIFI_CLOCK_FORCE_ALP));
        }
        if (Write && (Done & 0xFFFFu) == 0u) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI fw bytes=", Done);
            HalSerialBootMarkForce("\n");
        }
    }
    return 0;
}

int WifiBackplaneWriteMemory(UINT32 Address, const UINT8 *Data, UINT32 ByteCount) {
    return WifiBackplaneMemoryTransfer(Address, (UINT8 *)(UINTN)Data, ByteCount, 1);
}

int WifiBackplaneReadMemory(UINT32 Address, UINT8 *Data, UINT32 ByteCount) {
    return WifiBackplaneMemoryTransfer(Address, Data, ByteCount, 0);
}
