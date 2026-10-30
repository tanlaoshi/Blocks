/*
 * Io.c — Duo SDHCI SDIO 命令：CMD52（1 字节）与 CMD53（块/字节模式）
 *
 * 参数编码照 Linux include/linux/mmc/sdio.h 与 drivers/mmc/core/sdio_ops.c:
 *   CMD52 arg: [31]R/W [30:28]fn [27]RAW [25:9]addr [7:0]data
 *   CMD53 arg: [31]R/W [30:28]fn [27]BLK_MODE [26]OP_CODE(1=自增) [25:9]addr [8:0]cnt
 * AIC 的 FIFO（fn1 0x0F 读 / 0x10 写）必须 OP_CODE=0（固定地址）：
 * 厂商 aicsdio.c:742-751 用 sdio_writesb/readsb，sdio_io.c:519-523 传 incr_addr=0。
 */
#include "Sdhci/Private.h"

#define SDIO_CMD_RW_DIRECT 52u
#define SDIO_CMD_RW_EXTENDED 53u

/* R5 错误位（include/linux/mmc/sdio.h:55-59） */
#define SDIO_R5_COM_CRC_ERROR (1u << 15)
#define SDIO_R5_ILLEGAL_COMMAND (1u << 14)
#define SDIO_R5_ERROR (1u << 11)
#define SDIO_R5_FUNCTION_NUMBER (1u << 9)
#define SDIO_R5_OUT_OF_RANGE (1u << 8)
#define SDIO_R5_ERROR_MASK                                                                        \
    (SDIO_R5_COM_CRC_ERROR | SDIO_R5_ILLEGAL_COMMAND | SDIO_R5_ERROR | SDIO_R5_FUNCTION_NUMBER |   \
     SDIO_R5_OUT_OF_RANGE)

static int SdhciR5Check(SDHCI_HOST *Host) {
    UINT32 R5 = Host->Response[0];

    Host->LastR5 = R5;
    if ((R5 & SDIO_R5_ERROR_MASK) == 0) {
        return 0;
    }
    Host->LastStatus = R5;
    return -1;
}

/*
 * CMD52。Write=1 时 Data 写进 [7:0]；Write=0 时数据从 R5[7:0] 取回 Out。
 * RAW（写后回读）不用，固定 0（与 Linux 一致）。读命令的 data 域填 0（don't care）。
 */
int SdhciIoRwDirect(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, int Write, UINT8 Data,
                    UINT8 *Out) {
    UINT32 Argument = 0;
    UINT16 Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);

    if (Function > 7u || Address > 0x1FFFFu) {
        return -1;
    }
    if (Write) {
        Argument |= 0x80000000u;
    }
    Argument |= (Function & 7u) << 28;
    Argument |= (Address & 0x1FFFFu) << 9;
    Argument |= (UINT32)Data;
    if (SdhciCommandSend(Host, (UINT8)SDIO_CMD_RW_DIRECT, Argument, Flags, 0) != 0) {
        return -1;
    }
    if (SdhciR5Check(Host) != 0) {
        return -1;
    }
    if (!Write && Out != 0) {
        *Out = (UINT8)(Host->Response[0] & 0xFFu);
    }
    return 0;
}

int SdhciIoRead8(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, UINT8 *Out) {
    return SdhciIoRwDirect(Host, Function, Address, 0, 0, Out);
}

int SdhciIoWrite8(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, UINT8 Data) {
    return SdhciIoRwDirect(Host, Function, Address, 1, Data, 0);
}

int SdhciIoRwExtended(SDHCI_HOST *Host, UINT32 Function, UINT32 Address, int Write,
                      int Increment, UINT32 Blocks, UINT32 BlockSize, UINT8 *Buffer) {
    UINT32 Argument = 0;
    UINT16 Flags =
        (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    UINT16 TransferMode = 0;
    UINT32 Bytes;

    if (Function > 7u || Address > 0x1FFFFu || Buffer == 0) {
        return -1;
    }
    if (Blocks != 0) {
        /* 块模式：bit27=1，count 是块数（9 位） */
        if (Blocks > 511u || BlockSize == 0 || BlockSize > SDHCI_BLOCK_BYTES) {
            return -1;
        }
        Argument |= 0x08000000u | (Blocks & 0x1FFu);
        TransferMode |= (UINT16)(SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MSBS);
        Bytes = Blocks * BlockSize;
    } else {
        /* 字节模式：count 是字节数；512 编码成 0（sdio_ops.c:140-142） */
        if (BlockSize == 0 || BlockSize > SDHCI_BLOCK_BYTES) {
            return -1;
        }
        Argument |= (BlockSize == SDHCI_BLOCK_BYTES) ? 0u : (BlockSize & 0x1FFu);
        Bytes = BlockSize;
    }
    if (Write) {
        Argument |= 0x80000000u;
    }
    if (Increment) {
        Argument |= 0x04000000u;
    }
    Argument |= (Function & 7u) << 28;
    Argument |= (Address & 0x1FFFFu) << 9;
    if (!Write) {
        TransferMode |= SDHCI_TRNS_READ;
    }

    /*
     * 0x04 与 0x06 是相邻的两个 16 位寄存器，一次 32 位写：低半 = 块大小，
     * 高半 = 块数（字节模式写 0，与 Linux sdhci_set_block_info 一致）。
     */
    SdhciWrite32(Host, SDHCI_BLOCK_SIZE, BlockSize | ((Blocks & 0xFFFFu) << 16));
    if (SdhciCommandSend(Host, (UINT8)SDIO_CMD_RW_EXTENDED, Argument, Flags, TransferMode) != 0) {
        return -1;
    }
    if (Write) {
        if (SdhciPioWrite(Host, Buffer, Bytes) != 0) {
            return -1;
        }
    } else {
        if (SdhciPioRead(Host, Buffer, Bytes) != 0) {
            return -1;
        }
    }
    /* R5 的 FUNCTION_NUMBER / OUT_OF_RANGE 也可能在数据传输期间才置起来 */
    return SdhciR5Check(Host);
}
