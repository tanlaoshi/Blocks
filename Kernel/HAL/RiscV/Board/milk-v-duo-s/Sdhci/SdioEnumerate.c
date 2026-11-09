/*
 * SdioEnumerate.c — SdhciWifiInitialize 第 3-7 步：CMD0/5/3/7、CCCR、HS、4-bit、fn1 使能
 *
 * 职责边界：把卡从"能发命令"带到"总线就绪 + fn1 使能"；
 *           CMD5 探活工具在 SdioTransfer.c；CIS 验收在 SdioCisVerify.c。
 * 从 SdioTransfer.c 原样搬出（纯搬家，函数体逐字不动），行为零变化。
 */
#include "Sdhci/Private.h"

/*
 * SdhciWifiInitialize 第 3 步：CMD0 / CMD5（R4）/ CMD3（R6）/ CMD7（R1b）。
 * 原 Sdio.c 232-302；CMD5 的 100 次轮询与失败分支逐字保留。
 */
int CardEnumerate(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;
    UINT32 i;

    P->Ocr = 0;
    P->NoResponse = 0;
    (void)SdhciCommandSend(Host, 0, 0, SDHCI_CMD_RESP_NONE, 0);
    SdhciDelayMilliseconds(1);

    /*
     * ---------- S3：CMD5 探活（arg=0，sdio_ops.c mmc_send_io_op_cond 的 "just probing"） ----------
     * 细节在 SdioCmd5Window()：先查 CMD_INHIBIT、失败就 RESET_ALL 重来、共约 2 s 窗口。
     */
    P->Flags = (UINT16)SDHCI_CMD_RESP_SHORT; /* R4 无 CRC / 无 index */
    SdioMarkCmd5State(Host, "WiFi: pre-CMD5");
    if (SdioCmd5Window(Host) != 0) {
        return SdioFail("CMD5 (no response: module power / SD1 clock or pad)");
    }
    SdhciMarkHex("WiFi: CMD5 r4=", Host->Response[0], 8);
    SdhciMark("\n");

    /*
     * 电压窗口：卡在 R4[23:0] 报它支持的档，主机只报自己能给的（3.3 V 档）。
     * 与 Linux mmc_select_voltage(host, ocr) 同义：取交集，交集为空才用默认 3.2-3.4 V。
     */
    P->Offer = 0x00300000u & Host->Response[0];
    if (P->Offer == 0) {
        P->Offer = 0x00300000u;
    }
    SdhciMarkHex("WiFi: CMD5 offer=", P->Offer, 8);
    SdhciMark("\n");

    for (i = 0; i < 100u; i++) {
        if (SdhciCommandSend(Host, 5, P->Offer, P->Flags, 0) != 0) {
            P->NoResponse++;
            if (P->NoResponse >= 3u) {
                /* 连发都不通了：再轮下去只是白等，直接给出寄存器现场 */
                SdhciMarkHex("WiFi: CMD5 send-fail count=0x", P->NoResponse, 2);
                SdhciMarkHex(" int=", SdhciRead32(Host, SDHCI_INT_STATUS), 8);
                SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
                SdhciMark("\n");
                return SdioFail("CMD5 (keeps failing, cmd line dead)");
            }
            SdhciDelayMilliseconds(10);
            continue;
        }
        P->NoResponse = 0;
        P->Ocr = Host->Response[0];
        if ((P->Ocr & 0x80000000u) != 0) {
            break;
        }
        SdhciDelayMilliseconds(10);
    }
    if ((P->Ocr & 0x80000000u) == 0) {
        SdhciMarkHex("WiFi: CMD5 responds but not ready ocr=", P->Ocr, 8);
        SdhciMarkHex(" tries=", i, 2);
        SdhciMark("\n");
        return SdioFail("CMD5 (ready timeout: voltage/power)");
    }
    SdhciMarkHex("WiFi: ocr=", P->Ocr, 8);
    SdhciMark("\n");

    P->Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 3, 0, P->Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: CMD3 fail");
        return SdioFail("CMD3 (RCA)");
    }
    Host->RelativeCardAddress = Host->Response[0] & 0xFFFF0000u;
    SdhciMarkHex("WiFi: rca=", Host->RelativeCardAddress >> 16, 4);
    SdhciMark("\n");

    P->Flags = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 7, Host->RelativeCardAddress, P->Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: CMD7 fail");
        return SdioFail("CMD7 (select)");
    }
    return 0;
}

/*
 * SdhciWifiInitialize 第 4 步：CCCR 0x00 / 0x08 读。
 * 原 Sdio.c 304-328，日志逐字保留。
 */
int CccrConfigure(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;

    /* ---------- S3：CCCR（CMD52 读） ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CCCR, &P->Data) != 0) {
        SdhciMarkHex("WiFi:   CMD52 00 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x00 read (CMD52 path)");
    }
    SdhciMarkHex("WiFi: CCCR 00=", P->Data, 2);
    SdhciMarkDecimal(" rev=", (UINT32)(P->Data & 0x0Fu));
    SdhciMarkDecimal(" sdio=", (UINT32)(P->Data >> 4));
    SdhciMark("\n");

    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CAPS, &P->Caps) != 0) {
        SdhciMarkHex("WiFi:   CMD52 08 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x08 read");
    }
    SdhciMarkHex("WiFi: CCCR caps=", P->Caps, 2);
    SdhciMark(" SMB=");
    SdhciMark((P->Caps & SDIO_CAP_SMB) ? "1" : "0");
    SdhciMark(" SDC=");
    SdhciMark((P->Caps & SDIO_CAP_SDC) ? "1" : "0");
    SdhciMark(" 4BLS=");
    SdhciMark((P->Caps & SDIO_CAP_4BLS) ? "1" : "0");
    SdhciMark("\n");
    return 0;
}

/*
 * SdhciWifiInitialize 第 5 步：High-Speed（CCCR 0x13）+ 50 MHz + HISPD。
 * 原 Sdio.c 329-370，分支/日志逐字保留。
 */
int SpeedSwitch(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;

    /* ---------- S3：High-Speed（CCCR 0x13）+ 50 MHz + HISPD ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_SPEED, &P->Speed) != 0) {
        SdhciMarkHex("WiFi:   CMD52 13 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x13 read (speed)");
    }
    SdhciMarkHex("WiFi: CCCR 13=", P->Speed, 2);
    SdhciMark(" SHS=");
    SdhciMark((P->Speed & SDIO_SPEED_SHS) ? "1" : "0");
    SdhciMark("\n");
    if ((P->Speed & SDIO_SPEED_SHS) != 0) {
        if (SdhciIoWrite8(Host, 0, SDIO_CCCR_SPEED, (UINT8)(P->Speed | SDIO_SPEED_EHS)) != 0 ||
            SdhciIoRead8(Host, 0, SDIO_CCCR_SPEED, &P->Speed) != 0) {
            SdhciMarkHex("WiFi:   EHS write r5=", Host->LastStatus, 8);
            SdhciMark("\n");
            return SdioFail("CCCR 0x13 write (HS)");
        }
        SdhciMarkHex("WiFi: CCCR 13->", P->Speed, 2);
        SdhciMark(" EHS=");
        SdhciMark((P->Speed & SDIO_SPEED_EHS) ? "1" : "0");
        SdhciMark("\n");
        if ((P->Speed & SDIO_SPEED_EHS) == 0) {
            return SdioFail("HS switch (card refused EHS)");
        }
    } else {
        SdhciMark("WiFi: card no HS, stay 25MHz\n");
    }
    if (SdhciClockConfigure(Host, (P->Speed & SDIO_SPEED_EHS) ? TRANSFER_HERTZ : SD_TRANSFER_HERTZ) !=
        0) {
        SdhciMarkHex("WiFi:   clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMark("\n");
        return SdioFail("clock 50MHz");
    }
    /* Linux sdhci.c:2338-2355：MMC_TIMING_SD_HS 时置 HOST_CONTROL[2] */
    if ((P->Speed & SDIO_SPEED_EHS) != 0) {
        SdhciWrite8(Host, SDHCI_HOST_CONTROL,
                    (UINT8)(SdhciRead8(Host, SDHCI_HOST_CONTROL) | SDHCI_HIGH_SPEED));
    }
    SdhciMark("WiFi: SD1 clk actual=");
    SdhciMarkDecimal("", Host->ActualClockHertz / 1000u);
    SdhciMark("kHz\n");
    return 0;
}

/*
 * SdhciWifiInitialize 第 6 步：4-bit（CCCR 0x07 先卡，HOST_CONTROL 后主机）。
 * 原 Sdio.c 371-391，日志逐字保留。
 */
int BusWidthSwitch(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;

    /* ---------- S3：4-bit（CCCR 0x07 先卡，HOST_CONTROL 后主机） ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_IF, &P->Data) != 0) {
        SdhciMarkHex("WiFi:   CMD52 07 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x07 read (bus if)");
    }
    if (SdhciIoWrite8(Host, 0, SDIO_CCCR_IF,
                      (UINT8)((P->Data & (UINT8)~SDIO_BUS_WIDTH_MASK) | SDIO_BUS_WIDTH_4BIT)) !=
            0 ||
        SdhciIoRead8(Host, 0, SDIO_CCCR_IF, &P->Data) != 0 ||
        (P->Data & SDIO_BUS_WIDTH_MASK) != SDIO_BUS_WIDTH_4BIT) {
        SdhciMarkHex("WiFi: 4bit readback if=", P->Data, 2);
        SdhciMarkHex(" r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("4-bit switch");
    }
    SdhciWrite8(Host, SDHCI_HOST_CONTROL,
                (UINT8)(SdhciRead8(Host, SDHCI_HOST_CONTROL) | SDHCI_WIDTH_4BIT));
    Host->BusWidth = 4;
    SdhciMarkHex("WiFi: CCCR 07=", P->Data, 2);
    SdhciMark(" width=4bit\n");
    return 0;
}

/*
 * SdhciWifiInitialize 第 7 步：fn1 块大小（FBR）+ IOEx 使能 fn1 + IORx 校验。
 * 原 Sdio.c 392-450，读失败/超时分支与日志逐字保留。
 */
int FunctionEnable(WIFI_PROBE *P) {
    SDHCI_HOST *Host = P->Host;
    UINT32 i;

    /* ---------- S3 尾：fn1 块大小（CCCR/FBR 写）+ 使能 fn1 ---------- */
    {
        UINT8 Lo = 0;
        UINT8 Hi = 0;
        if (SdhciIoWrite8(Host, 0, 0x100u + SDIO_FBR_BLKSIZE, 0x00) != 0 ||
            SdhciIoWrite8(Host, 0, 0x101u + SDIO_FBR_BLKSIZE, 0x02) != 0 ||
            SdhciIoRead8(Host, 0, 0x100u + SDIO_FBR_BLKSIZE, &Lo) != 0 ||
            SdhciIoRead8(Host, 0, 0x101u + SDIO_FBR_BLKSIZE, &Hi) != 0) {
            SdhciMarkHex("WiFi:   fn1 blk r5=", Host->LastStatus, 8);
            SdhciMark("\n");
            return SdioFail("fn1 block size");
        }
        SdhciMarkHex("WiFi: fn1 blk=", (UINT32)Lo | ((UINT32)Hi << 8), 4);
        SdhciMark("\n");
    }
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_IOEX, &P->Data) != 0 ||
        SdhciIoWrite8(Host, 0, SDIO_CCCR_IOEX, (UINT8)(P->Data | 0x02u)) != 0) {
        SdhciMarkHex("WiFi:   IOEx r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("fn1 enable (IOEx)");
    }
    /*
     * Linux func->enable_timeout 默认 500 ms（sdio_io.c:59-100）。
     * 评审 P0-1：这里以前 `read 失败 → break`，然后拿**上一轮 IOEx 的残值**继续判
     * `bit1 != 0`，于是读失败也会打出 `fn1 enabled` —— 假绿。现在读失败单独判红，
     * 且用独立变量接 IORx，绝不与 IOEx 复用同一个缓冲。
     */
    {
        UINT8 IoRx = 0;
        int ReadErr = 0;
        for (i = 0; i < 500u; i++) {
            if (SdhciIoRead8(Host, 0, SDIO_CCCR_IORX, &IoRx) != 0) {
                ReadErr = 1;
                break;
            }
            if ((IoRx & 0x02u) != 0) {
                break;
            }
            SdhciDelayMilliseconds(1);
        }
        if (ReadErr != 0) {
            SdhciMarkHex("WiFi:   FAIL at IORx read r5=", Host->LastR5, 8);
            SdhciMark("\n");
            return SdioFail("fn1 enable (IORx read)");
        }
        if ((IoRx & 0x02u) == 0) {
            SdhciMarkHex("WiFi:   IORx=", IoRx, 2);
            SdhciMarkHex(" waited=", i, 4);
            SdhciMark(" ms\n");
            return SdioFail("fn1 enable timeout (IORx)");
        }
        SdhciMark("WiFi: fn1 enabled\n");
    }

    /* S3 的验收行：总线（时钟/宽度）+ fn1 都就绪之后才打 */
    SdhciMark((P->Speed & SDIO_SPEED_EHS) ? "WiFi: SD1 init ok clock=50MHz width=4bit\n"
                                         : "WiFi: SD1 init ok clock=25MHz width=4bit\n");
    return 0;
}
