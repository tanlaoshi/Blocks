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
static int gWifiTransferShapeLogged = 0;

/*
 * 一块窗口内的写入。对表 Linux mmc1 ftrace（灌片写）：
 * 众数 CMD53 = block_size=64 × blocks=511（bit27=1），余数也走块模式（1×64），
 * 不要对整块余数改用字节模式 CMD53（bit27=0）——那是与 Linux 不一致的形状。
 */
static int WifiBackplaneWriteChunk(UINT32 WindowOffset, UINT8 *Cursor, UINT32 ByteCount) {
    if (WIFI_BACKPLANE_BLOCK_MODE != 0 && ByteCount >= gWifiBackplaneBlockBytes) {
        UINT32 Blocks = ByteCount / gWifiBackplaneBlockBytes;
        UINT32 BulkBytes;

        if (Blocks > WIFI_BACKPLANE_BLOCKS_PER_COMMAND) {
            Blocks = WIFI_BACKPLANE_BLOCKS_PER_COMMAND;
        }
        BulkBytes = Blocks * gWifiBackplaneBlockBytes;
        if (gWifiTransferShapeLogged == 0) {
            gWifiTransferShapeLogged = 1;
            WifiBootMarkHexadecimal32("Boot: Pi WIFI xfer blk=", gWifiBackplaneBlockBytes);
            WifiBootMarkHexadecimal32(" blocks=", Blocks);
            WifiBootMarkHexadecimal32(" bytes=", BulkBytes);
            HalSerialBootMarkForce("\n");
        }
        if (WifiSdioBlockWrite(SDIO_FUNCTION_1, WindowOffset, Cursor, BulkBytes,
                               gWifiBackplaneBlockBytes) != 0) {
            return -1;
        }
        WindowOffset += BulkBytes;
        Cursor += BulkBytes;
        ByteCount -= BulkBytes;
    }
    /* 非整块：字节模式（寄存器/尾）；整块余数上面已用块模式吃掉 */
    while (ByteCount > 0u) {
        UINT32 Piece = ByteCount;

        if (Piece > 512u) {
            Piece = 512u;
        }
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
        UINT32 MaxBulk;
        int Error;

        if (Chunk > Remaining) {
            Chunk = Remaining;
        }
        /*
         * 块模式：单笔上限对表 Linux 灌片写 511×64，勿一次吃满 0x8000 再留下
         * 64 B 走字节模式尾巴。
         */
        if (WIFI_BACKPLANE_BLOCK_MODE != 0 && Write) {
            MaxBulk = gWifiBackplaneBlockBytes * WIFI_BACKPLANE_BLOCKS_PER_COMMAND;
            if (Chunk > MaxBulk) {
                Chunk = MaxBulk;
            }
            if (Chunk >= gWifiBackplaneBlockBytes) {
                Chunk -= (Chunk % gWifiBackplaneBlockBytes);
            }
        } else if (WIFI_BACKPLANE_BLOCK_MODE == 0 && Chunk > 480u) {
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
