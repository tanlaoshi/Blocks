/*
 * BlockTransfer.c — 块模式 CMD53 写（pwifi-2）
 *
 * 对表 Linux brcmf_sdiod_ramrw → sdio_memcpy_toio：灌固件走块模式，
 * 字节模式写 SRAM 会在第一块之后把卡挂死（R5=ERROR、DAT0 不释放）。
 */
#include "Wifi/Private.h"

/*
 * 块模式写。对表 Linux brcmf_sdiod_ramrw → sdio_memcpy_toio：
 * CMD53 bit27=1（块模式），bit26=1（地址递增，memcpy 必设；缺则整块打同一
 * 偏移，卡侧易 DAT 永不收尾——0x12d..0x12f `no data end words=10`），
 * 参数低 9 位=块数；块大小由 FBR 0x110 决定。
 */
int WifiSdioBlockWrite(UINT32 Function, UINT32 RegisterAddress, const UINT8 *Buffer,
                       UINT32 ByteCount, UINT32 BlockBytes) {
    UINT16 Flags;
    UINT16 TransferMode;
    UINT32 Argument;
    UINT32 Blocks;
    UINT32 Attempt;

    if (Buffer == 0 || ByteCount == 0 || BlockBytes == 0 || ByteCount > 32768u ||
        (ByteCount % BlockBytes) != 0u || (ByteCount / BlockBytes) > 0x1FFu) {
        return -1;
    }
    Blocks = ByteCount / BlockBytes;
    /* bit31=写, bit27=块模式, bit26=地址递增（对表 sdio_memcpy_toio） */
    Argument = ((Function & 7u) << 28) | ((RegisterAddress & 0x1FFFFu) << 9) |
               (Blocks & 0x1FFu) | (1u << 31) | (1u << 27) | (1u << 26);
    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    /* 失败 14：Blocks=1 且 TransferMode=0 → 永不 DATA_END；始终 BLK_CNT_EN */
    TransferMode = (UINT16)SDHCI_TRNS_BLK_CNT_EN;
    if (Blocks > 1u) {
        TransferMode = (UINT16)(TransferMode | WIFI_SDHCI_TRANSFER_MULTI);
    }

    for (Attempt = 0; Attempt < 4u; Attempt++) {
        (void)WifiSdioWaitCardIdle();
        if (Attempt != 0) {
            SdhciDelayMicroseconds(500);
        }
        SdhciWrite32(SDHCI_BLOCK_SIZE, BlockBytes | (Blocks << 16));
        if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_EXTENDED, Argument, Flags,
                             TransferMode) != 0) {
            continue;
        }
        if ((gSdhciResponse[0] & WIFI_SDIO_R5_ERROR_MASK) != 0) {
            if ((gSdhciResponse[0] & WIFI_SDIO_R5_ERROR_MASK) == WIFI_SDIO_R5_ERROR_ONLY) {
                continue;
            }
            WifiSdioProgrammedIoFailCleanup();
            return -1;
        }
        return WifiSdioProgrammedIoWriteBytes(Buffer, ByteCount);
    }
    WifiSdioProgrammedIoFailCleanup();
    return -1;
}

/* FBR 0x100/0x101 = 该功能支持的最大块长（低字节先）。Linux sdio_set_block_size 先读它。 */
int WifiSdioReadFunctionBlockSize(void) {
    UINT8 Low = 0;
    UINT8 High = 0;

    if (WifiSdioCommand52Read(SDIO_FUNCTION_0, SDIO_FBR1_BLOCK_SIZE, &Low) != 0 ||
        WifiSdioCommand52Read(SDIO_FUNCTION_0, SDIO_FBR1_BLOCK_SIZE + 1u, &High) != 0) {
        return -1;
    }
    return (int)((UINT32)Low | ((UINT32)High << 8));
}

int WifiSdioReadFunctionMaxBlockSize(void) {
    UINT8 Low = 0;
    UINT8 High = 0;

    if (WifiSdioCommand52Read(SDIO_FUNCTION_0, 0x100u, &Low) != 0) {
        return -1;
    }
    if (WifiSdioCommand52Read(SDIO_FUNCTION_0, 0x101u, &High) != 0) {
        return -1;
    }
    return (int)(((UINT32)High << 8) | (UINT32)Low);
}

/* FBR 在 0 号功能地址空间：0x100*N + 0x10 是块大小（低字节先）。 */
int WifiSdioSetFunctionBlockSize(UINT32 Bytes) {
    if (WifiSdioCommand52Write(SDIO_FUNCTION_0, 0x110u, (UINT8)(Bytes & 0xFFu)) != 0) {
        return -1;
    }
    return WifiSdioCommand52Write(SDIO_FUNCTION_0, 0x111u, (UINT8)((Bytes >> 8) & 0xFFu));
}
