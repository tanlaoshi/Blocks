/*
 * Sdio.c — Duo S SD1（wifi-sd）SDIO 枚举：S1 控制器 + S2 CMD52/53 + S3 卡识别/CIS
 *
 * 时序全部对照 Archive/DUO-WiFi-SDIO时序.md §2 与厂商驱动：
 *   - CMD0 → CMD5（R4）→ CMD3（R6，取 RCA）→ CMD7（R1b）→ CMD52 读 CCCR
 *     （linux drivers/mmc/core/sdio.c:695-800、sdio_ops.c）
 *   - 无 CMD8 / 无 ACMD41（SDIO-only，发了只会超时）
 *   - HS：CCCR 0x13 bit0 SHS → 写 bit1 EHS → 50 MHz（sdio.c:857-865）
 *   - 4-bit：CCCR 0x07[1:0]=2 → HOST_CONTROL[1]=1（先卡后主机，sdio.c:246-274）
 *   - CIS：指针在 fn0 0x109-0x10B，tuple 0x20 MANFID（sdio_cis.c:241-350）
 * 本轮不做中断（轮询 fn1 0x04 的路线留给 S4/S5）、不做固件下载。
 */
#include "Sdhci/Private.h"

/* CCCR / FBR 偏移（include/linux/mmc/sdio.h） */
#define SDIO_CCCR_CCCR 0x00u
#define SDIO_CCCR_IOEX 0x02u
#define SDIO_CCCR_IORX 0x03u
#define SDIO_CCCR_IF 0x07u
#define SDIO_CCCR_CAPS 0x08u
#define SDIO_CCCR_SPEED 0x13u
#define SDIO_FBR_CIS 0x09u
#define SDIO_FBR_BLKSIZE 0x10u

#define SDIO_BUS_WIDTH_MASK 0x03u
#define SDIO_BUS_WIDTH_4BIT 0x02u
#define SDIO_SPEED_SHS 0x01u
#define SDIO_SPEED_EHS 0x02u

#define SDIO_CAP_SDC 0x01u
#define SDIO_CAP_SMB 0x02u
#define SDIO_CAP_4BLS 0x80u

/* CIS tuple（sdio_cis.c:235 的 TPLMID 表） */
#define CISTPL_MANFID 0x20u
#define CISTPL_FUNCID 0x21u
#define CISTPL_FUNCE 0x22u

/* AIC fn1 私有寄存器（aicsdio.h:36-48，D80/V3 表） */
#define AIC_BYTEMODE_ENABLE_REG_V3 0x07u
#define AIC_MISC_INT_STATUS_REG_V3 0x04u
#define AIC_RD_FIFO_ADDR_V3 0x0Fu
#define AIC_WR_FIFO_ADDR_V3 0x10u

/* 自检：1536 B = 3 块 × 512（固件块消息的线上长度，时序表 §2.6） */
#define SDIO_SELFTEST_BYTES 1536u
#define SDIO_SELFTEST_BLOCKS (SDIO_SELFTEST_BYTES / SDHCI_BLOCK_BYTES)

typedef struct SDIO_CIS_INFO {
    UINT16 Vendor;
    UINT16 Device;
    UINT8 FunctionClass;
    UINT16 MaxBlockSize;
    int SawManfid;
} SDIO_CIS_INFO;

static int gWifiSdioState; /* 0 = 未试；1 = 成功；-1 = 失败（只报一次，不重试刷屏） */
static UINT8 gSdioPattern[SDIO_SELFTEST_BYTES];
static UINT8 gSdioReadback[SDIO_SELFTEST_BYTES];

static int SdioReadCisPointer(SDHCI_HOST *Host, UINT32 Function, UINT32 *Pointer) {
    UINT32 Ptr = 0;
    UINT32 i;
    UINT8 Byte;

    for (i = 0; i < 3u; i++) {
        if (SdhciIoRead8(Host, 0, Function * 0x100u + SDIO_FBR_CIS + i, &Byte) != 0) {
            return -1;
        }
        Ptr |= (UINT32)Byte << (i * 8u);
    }
    *Pointer = Ptr & 0x1FFFFu;
    return 0;
}

static int SdioParseCis(SDHCI_HOST *Host, UINT32 Pointer, SDIO_CIS_INFO *Info) {
    UINT32 Ptr = Pointer;
    UINT32 Guard;

    Info->Vendor = 0;
    Info->Device = 0;
    Info->FunctionClass = 0xFFu;
    Info->MaxBlockSize = 0;
    Info->SawManfid = 0;
    if (Pointer == 0 || Pointer == 0x1FFFFu) {
        return -1;
    }
    for (Guard = 0; Guard < 64u; Guard++) {
        UINT8 Code;
        UINT8 Length;
        UINT8 Data[4];
        UINT32 i;

        if (Ptr > 0x1FFF0u) {
            return -1;
        }
        if (SdhciIoRead8(Host, 0, Ptr, &Code) != 0) {
            return -1;
        }
        Ptr++;
        if (Code == 0xFFu) {
            return 0;
        }
        if (Code == 0x00u) {
            continue; /* 空条目：只有 code，没有 link/data */
        }
        if (SdhciIoRead8(Host, 0, Ptr, &Length) != 0) {
            return -1;
        }
        Ptr++;
        if (Length == 0xFFu) {
            return 0;
        }
        if (Code == CISTPL_MANFID && Length >= 4u) {
            for (i = 0; i < 4u; i++) {
                if (SdhciIoRead8(Host, 0, Ptr + i, &Data[i]) != 0) {
                    return -1;
                }
            }
            Info->Vendor = (UINT16)((UINT16)Data[0] | ((UINT16)Data[1] << 8));
            Info->Device = (UINT16)((UINT16)Data[2] | ((UINT16)Data[3] << 8));
            Info->SawManfid = 1;
        } else if (Code == CISTPL_FUNCID && Length >= 1u) {
            if (SdhciIoRead8(Host, 0, Ptr, &Data[0]) != 0) {
                return -1;
            }
            Info->FunctionClass = Data[0];
        } else if (Code == CISTPL_FUNCE && Length >= 3u) {
            for (i = 0; i < 3u; i++) {
                if (SdhciIoRead8(Host, 0, Ptr + i, &Data[i]) != 0) {
                    return -1;
                }
            }
            Info->MaxBlockSize = (UINT16)((UINT16)Data[1] | ((UINT16)Data[2] << 8));
        }
        Ptr += Length;
    }
    return -1; /* 元组过多：当失败，不无限扫 */
}

static void SdioMarkCis(UINT32 Function, UINT32 Pointer, const SDIO_CIS_INFO *Info) {
    SdhciMarkHex("WiFi: CIS fn", Function, 1);
    SdhciMarkHex(" ptr=", Pointer, 6);
    SdhciMarkHex(" manfid=", Info->Vendor, 4);
    SdhciMarkHex(" device=", Info->Device, 4);
    SdhciMarkHex(" class=", Info->FunctionClass, 2);
    SdhciMarkHex(" blk=", Info->MaxBlockSize, 4);
    SdhciMark("\n");
}

/*
 * 1536 B 一次搬完：SMB=1 走一次 3 块多块，否则拆 3 次字节模式 512 B
 * （Linux sdio_io_rw_ext_helper 的同一套分流，sdio_io.c:315-360）。
 * 两种都固定地址（OP_CODE=0）。
 */
static int SdioFifoTransfer(SDHCI_HOST *Host, int Write, UINT8 *Buffer, int MultiBlock) {
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

/* S3 的收尾自检：多块 + 固定地址（OP_CODE=0）FIFO 写读一遍 */
static void SdioFifoTest(SDHCI_HOST *Host, int MultiBlock) {
    UINT32 i;

    for (i = 0; i < SDIO_SELFTEST_BYTES; i++) {
        gSdioPattern[i] = (UINT8)((i * 7u + 0xA5u) & 0xFFu);
        gSdioReadback[i] = 0;
    }
    SdhciMark(MultiBlock ? "WiFi: FIFO mode=block x3\n" : "WiFi: FIFO mode=byte x3\n");
    if (SdioFifoTransfer(Host, 1, gSdioPattern, MultiBlock) != 0) {
        SdhciFail(Host, "WiFi: FIFO write 1536B fail");
        return;
    }
    SdhciMark("WiFi: FIFO write 1536B ok\n");
    if (SdioFifoTransfer(Host, 0, gSdioReadback, MultiBlock) != 0) {
        SdhciFail(Host, "WiFi: FIFO read 1536B fail");
        return;
    }
    for (i = 0; i < SDIO_SELFTEST_BYTES; i++) {
        if (gSdioReadback[i] != gSdioPattern[i]) {
            break;
        }
    }
    if (i == SDIO_SELFTEST_BYTES) {
        SdhciMark("WiFi: FIFO 1536B rw ok\n");
        return;
    }
    SdhciMarkHex("WiFi: FIFO readback diff at ", i, 4);
    SdhciMarkHex(" got=", gSdioReadback[i], 2);
    SdhciMarkHex(" want=", gSdioPattern[i], 2);
    SdhciMark("\n");
}

int SdhciWifiInitialize(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 Ocr = 0;
    UINT32 Offer;
    UINT32 i;
    UINT16 Flags;
    UINT8 Data;
    UINT8 Speed;
    UINT8 Caps = 0;
    UINT32 Ptr = 0;
    SDIO_CIS_INFO Cis;
    UINT32 Present;

    if (gWifiSdioState != 0) {
        return (gWifiSdioState > 0) ? 0 : -1;
    }
    if (Host == 0) {
        gWifiSdioState = -1;
        return -1;
    }

    /* ---------- S1：板级上电 + 卡检测覆盖 ---------- */
    if (DuoWifiPowerOn() != 0 || DuoWifiCardDetectOverride() != 0) {
        gWifiSdioState = -1;
        return -1;
    }

    Host->Specification = (UINT16)(SdhciRead16(Host, SDHCI_HOST_VERSION) & 0xFFu);
    if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0) {
        SdhciMark("WiFi: SD1 softreset timeout\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMark("Duo SDHCI1 ");
    SdhciMarkHex("ver=", Host->Specification, 8);
    SdhciMarkHex(" cap=", SdhciRead32(Host, SDHCI_CAPABILITIES), 8);
    SdhciMarkHex(" area=", Host->VendorArea, 3);
    SdhciMark("\n");
    SdhciMarkHex("WiFi: MSHC_CTRL=", SdhciRead32(Host, Host->VendorArea + CV_MSHC_CTRL), 8);
    SdhciMarkHex(" phy=", SdhciRead32(Host, Host->VendorArea + CV_PHY_TX_RX), 8);
    SdhciMark("\n");

    if (SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0) != 0) {
        SdhciMark("WiFi: SD1 clock id fail\n");
        gWifiSdioState = -1;
        return -1;
    }

    /*
     * 卡检测覆盖的验收点：TOP 0x294 写了 bit8/bit9 之后，PRESENT_STATE[16]
     * 应为 1；[17] CARD_STABLE 是否跟着变 1 只有真机能答（时序表 §8-C 第 2 条）。
     * 注意 Linux 在 non-removable 上根本不看这两位（sdhci.c:2438-2466 直接 return 1），
     * 所以这里只打印、不当作继续/放弃的门禁。
     */
    Present = SdhciRead32(Host, SDHCI_PRESENT_STATE);
    SdhciMarkHex("SD1: present=", Present, 8);
    SdhciMark(" CARD_INSERTED=");
    SdhciMark((Present & SDHCI_CARD_INSERTED) ? "1" : "0");
    SdhciMark(" CARD_STABLE=");
    SdhciMark((Present & SDHCI_CARD_STABLE) ? "1" : "0");
    SdhciMark("\n");

    /* ---------- S3：CMD0 / CMD5 / CMD3 / CMD7 ---------- */
    (void)SdhciCommandSend(Host, 0, 0, SDHCI_CMD_RESP_NONE, 0);
    SdhciDelayMilliseconds(1);

    /* CMD5 探活：arg=0 单次（sdio_ops.c mmc_send_io_op_cond 的 "just probing"） */
    Flags = (UINT16)SDHCI_CMD_RESP_SHORT; /* R4 无 CRC / 无 index */
    if (SdhciCommandSend(Host, 5, 0, Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: SD1 CMD5 no response");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMarkHex("WiFi: CMD5 r4=", Host->Response[0], 8);
    SdhciMark("\n");

    /*
     * 电压窗口：卡在 R4[23:0] 报它支持的档，主机只报自己能给的（3.3 V 档）。
     * 与 Linux mmc_select_voltage(host, ocr) 同义：取交集，交集为空才用默认 3.2-3.4 V。
     */
    Offer = 0x00300000u & Host->Response[0];
    if (Offer == 0) {
        Offer = 0x00300000u;
    }
    SdhciMarkHex("WiFi: CMD5 offer=", Offer, 8);
    SdhciMark("\n");

    for (i = 0; i < 100u; i++) {
        if (SdhciCommandSend(Host, 5, Offer, Flags, 0) != 0) {
            SdhciDelayMilliseconds(10);
            continue;
        }
        Ocr = Host->Response[0];
        if ((Ocr & 0x80000000u) != 0) {
            break;
        }
        SdhciDelayMilliseconds(10);
    }
    if ((Ocr & 0x80000000u) == 0) {
        SdhciMarkHex("WiFi: SD1 CMD5 timeout ocr=", Ocr, 8);
        SdhciMark("\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMarkHex("WiFi: ocr=", Ocr, 8);
    SdhciMark("\n");

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 3, 0, Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: SD1 CMD3 fail");
        gWifiSdioState = -1;
        return -1;
    }
    Host->RelativeCardAddress = Host->Response[0] & 0xFFFF0000u;
    SdhciMarkHex("WiFi: rca=", Host->RelativeCardAddress >> 16, 4);
    SdhciMark("\n");

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 7, Host->RelativeCardAddress, Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: SD1 CMD7 fail");
        gWifiSdioState = -1;
        return -1;
    }

    /* ---------- S3：CCCR（CMD52 读） ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CCCR, &Data) != 0) {
        SdhciMarkHex("WiFi: SD1 CCCR read fail r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMarkHex("WiFi: CCCR 00=", Data, 2);
    SdhciMarkDecimal(" rev=", (UINT32)(Data & 0x0Fu));
    SdhciMarkDecimal(" sdio=", (UINT32)(Data >> 4));
    SdhciMark("\n");

    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CAPS, &Caps) != 0) {
        SdhciMark("WiFi: CCCR caps read fail\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMarkHex("WiFi: CCCR caps=", Caps, 2);
    SdhciMark(" SMB=");
    SdhciMark((Caps & SDIO_CAP_SMB) ? "1" : "0");
    SdhciMark(" SDC=");
    SdhciMark((Caps & SDIO_CAP_SDC) ? "1" : "0");
    SdhciMark(" 4BLS=");
    SdhciMark((Caps & SDIO_CAP_4BLS) ? "1" : "0");
    SdhciMark("\n");

    /* ---------- S3：High-Speed（CCCR 0x13）+ 50 MHz + HISPD ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_SPEED, &Speed) != 0) {
        SdhciMark("WiFi: CCCR speed read fail\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMarkHex("WiFi: CCCR 13=", Speed, 2);
    SdhciMark(" SHS=");
    SdhciMark((Speed & SDIO_SPEED_SHS) ? "1" : "0");
    SdhciMark("\n");
    if ((Speed & SDIO_SPEED_SHS) != 0) {
        if (SdhciIoWrite8(Host, 0, SDIO_CCCR_SPEED, (UINT8)(Speed | SDIO_SPEED_EHS)) != 0 ||
            SdhciIoRead8(Host, 0, SDIO_CCCR_SPEED, &Speed) != 0) {
            SdhciMark("WiFi: HS switch fail\n");
            gWifiSdioState = -1;
            return -1;
        }
        SdhciMarkHex("WiFi: CCCR 13->", Speed, 2);
        SdhciMark(" EHS=");
        SdhciMark((Speed & SDIO_SPEED_EHS) ? "1" : "0");
        SdhciMark("\n");
        if ((Speed & SDIO_SPEED_EHS) == 0) {
            SdhciMark("WiFi: HS not active\n");
            gWifiSdioState = -1;
            return -1;
        }
    } else {
        SdhciMark("WiFi: card no HS, stay 25MHz\n");
    }
    if (SdhciClockConfigure(Host, (Speed & SDIO_SPEED_EHS) ? TRANSFER_HERTZ : SD_TRANSFER_HERTZ) !=
        0) {
        SdhciMark("WiFi: SD1 clock xfer fail\n");
        gWifiSdioState = -1;
        return -1;
    }
    /* Linux sdhci.c:2338-2355：MMC_TIMING_SD_HS 时置 HOST_CONTROL[2] */
    if ((Speed & SDIO_SPEED_EHS) != 0) {
        SdhciWrite8(Host, SDHCI_HOST_CONTROL,
                    (UINT8)(SdhciRead8(Host, SDHCI_HOST_CONTROL) | SDHCI_HIGH_SPEED));
    }
    SdhciMark("WiFi: SD1 clk actual=");
    SdhciMarkDecimal("", Host->ActualClockHertz / 1000u);
    SdhciMark("kHz\n");

    /* ---------- S3：4-bit（CCCR 0x07 先卡，HOST_CONTROL 后主机） ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_IF, &Data) != 0) {
        SdhciMark("WiFi: CCCR 07 read fail\n");
        gWifiSdioState = -1;
        return -1;
    }
    if (SdhciIoWrite8(Host, 0, SDIO_CCCR_IF,
                      (UINT8)((Data & (UINT8)~SDIO_BUS_WIDTH_MASK) | SDIO_BUS_WIDTH_4BIT)) != 0 ||
        SdhciIoRead8(Host, 0, SDIO_CCCR_IF, &Data) != 0 ||
        (Data & SDIO_BUS_WIDTH_MASK) != SDIO_BUS_WIDTH_4BIT) {
        SdhciMarkHex("WiFi: 4-bit switch fail if=", Data, 2);
        SdhciMark("\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciWrite8(Host, SDHCI_HOST_CONTROL,
                (UINT8)(SdhciRead8(Host, SDHCI_HOST_CONTROL) | SDHCI_WIDTH_4BIT));
    Host->BusWidth = 4;
    SdhciMarkHex("WiFi: CCCR 07=", Data, 2);
    SdhciMark(" width=4bit\n");

    /* ---------- S3 尾：fn1 块大小（CCCR/FBR 写）+ 使能 fn1 ---------- */
    {
        UINT8 Lo = 0;
        UINT8 Hi = 0;
        if (SdhciIoWrite8(Host, 0, 0x100u + SDIO_FBR_BLKSIZE, 0x00) != 0 ||
            SdhciIoWrite8(Host, 0, 0x101u + SDIO_FBR_BLKSIZE, 0x02) != 0 ||
            SdhciIoRead8(Host, 0, 0x100u + SDIO_FBR_BLKSIZE, &Lo) != 0 ||
            SdhciIoRead8(Host, 0, 0x101u + SDIO_FBR_BLKSIZE, &Hi) != 0) {
            SdhciMark("WiFi: fn1 blocksize write fail\n");
            gWifiSdioState = -1;
            return -1;
        }
        SdhciMarkHex("WiFi: fn1 blk=", (UINT32)Lo | ((UINT32)Hi << 8), 4);
        SdhciMark("\n");
    }
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_IOEX, &Data) != 0 ||
        SdhciIoWrite8(Host, 0, SDIO_CCCR_IOEX, (UINT8)(Data | 0x02u)) != 0) {
        SdhciMark("WiFi: fn1 enable write fail\n");
        gWifiSdioState = -1;
        return -1;
    }
    /* Linux func->enable_timeout 默认 500 ms（sdio_io.c:59-100） */
    for (i = 0; i < 500u; i++) {
        if (SdhciIoRead8(Host, 0, SDIO_CCCR_IORX, &Data) != 0) {
            break;
        }
        if ((Data & 0x02u) != 0) {
            break;
        }
        SdhciDelayMilliseconds(1);
    }
    if ((Data & 0x02u) == 0) {
        SdhciMarkHex("WiFi: fn1 enable timeout ior=", Data, 2);
        SdhciMark("\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMark("WiFi: fn1 enabled\n");

    /* S3 的验收行：总线（时钟/宽度）+ fn1 都就绪之后才打 */
    SdhciMark((Speed & SDIO_SPEED_EHS) ? "WiFi: SD1 init ok clock=50MHz width=4bit\n"
                                       : "WiFi: SD1 init ok clock=25MHz width=4bit\n");

    /* ---------- S3 验收：读 CIS（fn1 指针在 fn0 0x109-0x10B） ---------- */
    if (SdioReadCisPointer(Host, 1, &Ptr) != 0 || SdioParseCis(Host, Ptr, &Cis) != 0 ||
        Cis.SawManfid == 0) {
        SdhciMark("WiFi: CIS fn1 read fail\n");
        gWifiSdioState = -1;
        return -1;
    }
    SdhciMark("WiFi: CIS manfid=");
    SdhciMarkHex("", Cis.Vendor, 4);
    SdhciMark(" device=");
    SdhciMarkHex("", Cis.Device, 4);
    SdhciMark("\n");
    SdioMarkCis(1, Ptr, &Cis);

    /* fn2 的 CIS 顺手读一下（D80 上 fn2 无数据通路，只作身份对照） */
    {
        UINT32 Ptr2 = 0;
        SDIO_CIS_INFO Cis2;
        if (SdioReadCisPointer(Host, 2, &Ptr2) == 0 && SdioParseCis(Host, Ptr2, &Cis2) == 0 &&
            Cis2.SawManfid != 0) {
            SdioMarkCis(2, Ptr2, &Cis2);
        }
    }

    /* ---------- S3 收尾自检：多块 + 固定地址 FIFO（时序表 §7 卡点②） ---------- */
    SdioFifoTest(Host, (Caps & SDIO_CAP_SMB) ? 1 : 0);

    /* fn1 0x04 杂项中断状态：本轮不轮询，只打一次，供 S4 对照 */
    if (SdhciIoRead8(Host, 1, AIC_MISC_INT_STATUS_REG_V3, &Data) == 0) {
        SdhciMarkHex("WiFi: fn1 misc=0x", Data, 2);
        SdhciMark("\n");
    }
    Host->CardType = SDHCI_CARD_SDIO;
    Host->Ready = 1;
    gWifiSdioState = 1;
    return 0;
}
