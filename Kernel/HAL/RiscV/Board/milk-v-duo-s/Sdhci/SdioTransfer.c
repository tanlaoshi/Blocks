/*
 * SdioTransfer.c — SDIO 传输与现场：CMD53 收发、错误位解码、传输参数打印、总线恢复
 *
 * 职责边界：只回答"这一笔数据怎么走、走坏了怎么救"——FIFO 搬运、错误位、总线恢复；
 *           不做卡识别（SdioCis.c）、不做自检判定（SdioSelfTest.c）。
 * 从 Sdio.c 原样搬出（纯搬家，函数体逐字不动），行为零变化。
 */
#include "Sdhci/Private.h"


/*
 * 1536 B 一次搬完：SMB=1 走一次 3 块多块，否则拆 3 次字节模式 512 B
 * （Linux sdio_io_rw_ext_helper 的同一套分流，sdio_io.c:315-360）。
 * 两种都固定地址（OP_CODE=0）。
 */
int SdioFifoTransfer(SDHCI_HOST *Host, int Write, UINT8 *Buffer, int MultiBlock) {
    UINT32 Offset = 0;
    UINT32 Address = Write ? AIC_WR_FIFO_ADDR_V3 : AIC_RD_FIFO_ADDR_V3;

    if (MultiBlock) {
        return SdhciIoRwExtended(Host, 1, Address, Write, 0, SDIO_SELFTEST_BLOCKS,
                                 SDHCI_BLOCK_BYTES, Buffer);
    }
    while (Offset < SDIO_SELFTEST_BYTES) {
        if (SdhciIoRwExtended(Host, 1, Address, Write, 0, 0, SDHCI_BLOCK_BYTES,
                              Buffer + Offset) != 0) {
            return -1;
        }
        Offset += SDHCI_BLOCK_BYTES;
    }
    return 0;
}

/*
 * 传输现场：BLOCK_SIZE / BLOCK_COUNT（块模式下传完会减到 0）/ TRANSFER_MODE /
 * DATA_TIMEOUT / 原始 INT_STATUS / 错误位 / PRESENT_STATE。
 * 块模式的 BLOCK_COUNT 是"还剩几块"，传完应为 0 —— 这是判断"模块到底回了
 * 几块"最直接的证据（用户要求第 1 点）。
 */
static void SdioMarkErrBits(UINT32 Err) {
    SdhciMark(" bits:");
    if ((Err & 0x0001u) != 0) {
        SdhciMark(" ADMA");
    }
    if ((Err & 0x0002u) != 0) {
        SdhciMark(" AC12");
    }
    if ((Err & 0x0004u) != 0) {
        SdhciMark(" CMD_END");
    }
    if ((Err & 0x0008u) != 0) {
        SdhciMark(" CMD_IDX");
    }
    if ((Err & 0x0010u) != 0) {
        SdhciMark(" DAT_END");
    }
    if ((Err & 0x0020u) != 0) {
        SdhciMark(" DAT_CRC");
    }
    if ((Err & 0x0040u) != 0) {
        SdhciMark(" DAT_TIMEOUT");
    }
    if ((Err & 0x0080u) != 0) {
        SdhciMark(" CMD_TIMEOUT");
    }
    if (Err == 0) {
        SdhciMark(" none");
    }
    SdhciMark("\n");
}

/*
 * 传输现场的现场三行：BLOCK_SIZE/BLOCK_COUNT/TRANSFER_MODE/DATA_TIMEOUT、
 * **复位之前**的 INT_STATUS / 错误位 / PRESENT_STATE（Host->Last* 抓的，
 * PioAbort 会先记再复位，现场读是读不到的）、以及复位之后的当前值。
 */
void SdioMarkXfer(SDHCI_HOST *Host, const char *Tag) {
    UINT32 St = SdhciRead32(Host, SDHCI_INT_STATUS);
    UINT32 Err = (Host->LastStatus >> 16) & 0xFFFFu;

    SdhciMark(Tag);
    SdhciMarkHex(" blksz=", SdhciRead16(Host, SDHCI_BLOCK_SIZE), 4);
    SdhciMarkHex(" blkcnt=", SdhciRead16(Host, SDHCI_BLOCK_COUNT), 4);
    SdhciMarkHex(" tmode=", SdhciRead16(Host, SDHCI_TRANSFER_MODE), 4);
    SdhciMarkHex(" tmo=", SdhciRead8(Host, SDHCI_TIMEOUT_CONTROL), 2);
    SdhciMark("\n");
    SdhciMarkHex("WiFi:  pre-reset int=", Host->LastStatus, 8);
    SdhciMarkHex(" err=", Err, 4);
    SdhciMarkHex(" pst=", Host->LastPresentState, 8);
    SdioMarkErrBits(Err);
    SdhciMarkHex("WiFi:  now int=", St, 8);
    SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
    SdhciMark("\n");
}

/* 把 DAT/CMD 卡住的总线拉回来：先 RESET_DAT，不行再 RESET_ALL + 重起 400k */
int SdioRecoverBus(SDHCI_HOST *Host, UINT8 HostControl, UINT32 RestoreHertz) {
    UINT32 Pst = SdhciRead32(Host, SDHCI_PRESENT_STATE);
    UINT8 Data;

    if ((Pst & (SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT)) == 0) {
        return 0;
    }
    SdhciMarkHex("WiFi: bus stuck, pst=", Pst, 8);
    SdhciMark("\n");
    (void)SdhciSoftwareReset(Host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    Pst = SdhciRead32(Host, SDHCI_PRESENT_STATE);
    if ((Pst & (SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT)) == 0) {
        SdhciMark("WiFi: bus recovered (RESET_DAT)\n");
        return 0;
    }
    SdhciMark("WiFi: still stuck, RESET_ALL + 400kHz\n");
    if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0 ||
        SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
        return -1;
    }
    SdhciWrite8(Host, SDHCI_HOST_CONTROL, HostControl);
    (void)SdhciClockConfigure(Host, RestoreHertz);
    if (SdhciIoRead8(Host, 0, 0x00u, &Data) != 0) {
        return -1;
    }
    SdhciMarkHex("WiFi: bus recovered (RESET_ALL) CCCR00=", Data, 2);
    SdhciMark("\n");
    return 0;
}
