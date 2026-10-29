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

static int gWifiBlockSizeReady = 0;
static UINT32 gWifiBackplaneBlockBytes = WIFI_BACKPLANE_BLOCK_BYTES;

/*
 * 一块窗口内的写入。对表 Linux sdio_io_rw_ext_helper：
 * 先尽量用块模式发满（单条 CMD53 上限 511 块），余数再走字节模式。
 * 这与我们之前「每包 64 字节字节模式」完全不同——那形状卡不认。
 */
static int WifiBackplaneWriteChunk(UINT32 WindowOffset, UINT8 *Cursor, UINT32 ByteCount) {
    if (WIFI_BACKPLANE_BLOCK_MODE != 0 && ByteCount > gWifiBackplaneBlockBytes) {
        UINT32 Blocks = ByteCount / gWifiBackplaneBlockBytes;
        UINT32 BulkBytes;

        if (Blocks > 511u) {
            Blocks = 511u;
        }
        BulkBytes = Blocks * gWifiBackplaneBlockBytes;
        if (WifiSdioBlockWrite(SDIO_FUNCTION_1, WindowOffset, Cursor, BulkBytes,
                               gWifiBackplaneBlockBytes) != 0) {
            return -1;
        }
        WindowOffset += BulkBytes;
        Cursor += BulkBytes;
        ByteCount -= BulkBytes;
    }
    while (ByteCount > 0u) {
        UINT32 Piece = (ByteCount > 64u) ? 64u : ByteCount;

        if (WifiSdioCommand53Write(SDIO_FUNCTION_1, WindowOffset, Cursor, Piece, 1) != 0) {
            return -1;
        }
        WindowOffset += Piece;
        Cursor += Piece;
        ByteCount -= Piece;
    }
    return 0;
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
    /* 块长必须跟卡自己声明的最大块长一致，否则卡等不满一块、数据阶段永不完成 */
    if (Write && !gWifiBlockSizeReady) {
        WifiSdioResetWaitReport();
    }
    if (WIFI_BACKPLANE_BLOCK_MODE == 0) {
        gWifiBackplaneBlockBytes = WIFI_BACKPLANE_BLOCK_BYTES;
    } else if (Write && !gWifiBlockSizeReady) {
        int ReadBack;

        /*
         * 对表 Linux sdio_set_block_size：块长取卡声明的值（上限 512），并写回 FBR。
         * 之前的 0x100 读的是 CSA 指针不是块长，所以恒 0；真正的块长在 0x110/0x111。
         */
        /* 512 会被卡以 R5 OUT_OF_RANGE 拒绝，说明 F1 块长上限是 64 */
        (void)WifiSdioSetFunctionBlockSize(64u);
        ReadBack = WifiSdioReadFunctionBlockSize();
        WifiBootMarkHexadecimal32("Boot: Pi WIFI fbr readback=", (UINT32)ReadBack);
        HalSerialBootMarkForce("\n");
        gWifiBackplaneBlockBytes = (ReadBack == 512) ? 512u : 64u;
        gWifiBlockSizeReady = 1;
        WifiBootMarkHexadecimal32("Boot: Pi WIFI fbr blk=", gWifiBackplaneBlockBytes);
        HalSerialBootMarkForce("\n");
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
        /* 块模式下 Chunk 由窗口大小决定（最多 32 KB）；只有字节模式才需要压到 9 位计数以内 */
        if (WIFI_BACKPLANE_BLOCK_MODE == 0 && Chunk > 480u) {
            Chunk = 480u;
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
            Error = WifiBackplaneWriteChunk(WindowOffset, Cursor, Chunk);
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
            WifiBootMarkHexadecimal32("Boot: Pi WIFI fw r5=", gSdhciResponse[0]);
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

/*
 * 验证「背板写真的落地」这个前提：往 bankidx 写 1 读回，再写 3 读回。
 * 窗口寄存器是 F1 空间（CMD52 直写），落地不代表背板写也落地；
 * 若这里读回 0，说明所有背板写都是空操作，此前「寄存器通」的结论就要推翻。
 */
int WifiBackplaneWriteReadbackTest(void) {
    UINT32 First = 0;
    UINT32 Second = 0;

    if (WifiBackplaneWrite32(WIFI_SOCRAM_BANK_INDEX, 1u) != 0 ||
        WifiBackplaneRead32(WIFI_SOCRAM_BANK_INDEX, &First) != 0 ||
        WifiBackplaneWrite32(WIFI_SOCRAM_BANK_INDEX, 3u) != 0 ||
        WifiBackplaneRead32(WIFI_SOCRAM_BANK_INDEX, &Second) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI bankidx roundtrip fail\n");
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI bankidx write1 read=", First);
    WifiBootMarkHexadecimal32(" write3 read=", Second);
    HalSerialBootMarkForce("\n");
    return 0;
}
