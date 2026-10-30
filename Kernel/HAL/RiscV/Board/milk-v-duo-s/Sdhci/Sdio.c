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
/* S5b：CIS 里读到的 vendor/device，厂商 aicwf_sdio_chipmatch 就是拿它们认芯片的 */
static UINT16 gWifiChipVendor;
static UINT16 gWifiChipDevice;
/* S5b 恢复总线用：枚举时的 HOST_CONTROL 与时钟，供 WifiFw.c 里的消息通道复用 */
static UINT8 gWifiHostControl;
static UINT32 gWifiRestoreHertz;
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

/* 把一段 CIS 的原始字节打出来；读失败就标出偏移，一眼能看出是"读到 00"还是"读不动" */
static void SdioDumpCis(SDHCI_HOST *Host, const char *Tag, UINT32 Address, UINT32 Count) {
    UINT32 i;
    UINT8 Byte;
    int Rc;

    SdhciMark(Tag);
    for (i = 0; i < Count; i++) {
        Rc = SdhciIoRead8(Host, 0, Address + i, &Byte);
        if (Rc != 0) {
            SdhciMarkHex(" <read fail +", i, 2);
            SdhciMark(">");
            break;
        }
        SdhciMarkHex(" ", Byte, 2);
    }
    SdhciMark("\n");
}

/*
 * 在 [From,To) 里找 MANFID 元组头 0x20 0x04（MANFID 的 link 固定是 4，
 * 见 sdio_cis.c 的 CIS_TUPLE(MANFID) 表）。返回元组起始地址，找不到返回 0。
 * 只在"指针解析不出 MANFID"时当兜底用，顺序读、每字节一次 CMD52。
 */
static UINT32 SdioScanCis(SDHCI_HOST *Host, UINT32 From, UINT32 To) {
    UINT32 Addr;
    UINT8 Prev = 0;
    UINT8 Byte;

    if (SdhciIoRead8(Host, 0, From, &Prev) != 0) {
        return 0;
    }
    for (Addr = From + 1u; Addr < To; Addr++) {
        if (SdhciIoRead8(Host, 0, Addr, &Byte) != 0) {
            return 0;
        }
        if (Prev == CISTPL_MANFID && Byte == 4u) {
            return Addr - 1u;
        }
        Prev = Byte;
    }
    return 0;
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
static void SdioMarkXfer(SDHCI_HOST *Host, const char *Tag) {
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
static int SdioRecoverBus(SDHCI_HOST *Host, UINT8 HostControl, UINT32 RestoreHertz) {
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

/*
 * DAT 读方向的"确定有货"验证：用 CMD53 字节模式从 fn0 的 Common CIS（0x1000）
 * 读 16 B，再和 CMD52 逐字节读的结果比对。
 * CIS 是卡自己的 ROM，一定回数据 —— 与模块固件在不在、FIFO 里有没有消息无关，
 * 所以它能单独证明 DAT0-3 的读方向没问题。
 */
/*
 * 把"CMD53 读回来的一串字节"和"CMD52 逐字节读的同一地址"并排打出来并比对。
 * 两个来源不同：CMD53 走数据线（DAT0-3）批量读，CMD52 走命令线单字节读。
 * 两者一致 → 数据线读方向可信；不一致 → 直接把两边原文摆出来，不用猜。
 */
static int SdioCmpCisRead(SDHCI_HOST *Host, const char *Tag, UINT32 Address, UINT8 *Buf,
                          UINT32 Bytes) {
    UINT8 Ref[16];
    UINT32 i;
    UINT32 Bad = Bytes;

    if (Bytes > 16u) {
        Bytes = 16u;
    }

    SdhciMark(Tag);
    SdhciMarkHex(" cmd53@", Address, 6);
    SdhciMark(":");
    for (i = 0; i < Bytes; i++) {
        SdhciMarkHex(" ", Buf[i], 2);
    }
    SdhciMark("\n");
    SdhciMark("WiFi:  cmd52 ref            :");
    for (i = 0; i < Bytes; i++) {
        if (SdhciIoRead8(Host, 0, Address + i, &Ref[i]) != 0) {
            SdhciMark(" <read fail>");
            break;
        }
        SdhciMarkHex(" ", Ref[i], 2);
    }
    SdhciMark("\n");
    for (i = 0; i < Bytes; i++) {
        if (Buf[i] != Ref[i]) {
            Bad = i;
            break;
        }
    }
    if (Bad == Bytes) {
        return 0;
    }
    SdhciMarkHex("WiFi:  mismatch +", Bad, 2);
    SdhciMarkHex(" cmd53=", Buf[Bad], 2);
    SdhciMarkHex(" cmd52=", Ref[Bad], 2);
    SdhciMark("\n");
    return -1;
}

/*
 * DAT 读方向自证：CMD53（字节模式、**地址自增**）从 fn0 的 Common CIS 读 16 B，
 * 与 CMD52 逐字节读的结果比对。
 *
 * 09:23 真机上的 `DAT read mismatch at 01 got=20 want=04` 就是这里的 bug：
 * 当时 Increment 传的是 0 → OP_CODE=0 = **固定地址**，卡老老实实把 0x1000 的
 * 那个 0x20 重复回了 16 次，所以 buf[1] 还是 0x20，而 CIS 的真值是 0x04。
 * 读路径本身没错，是参数错了（`20 04 a1 c8` 里 0x20 恰好是 MANFID tuple 头，
 * 看着像"读到了正确内容"，特别容易骗过人）。
 *
 * 失败时退 25 MHz / 1-bit 再对照一次（用户要求第 4 点），然后恢复总线。
 */
static int SdioDatReadCheck(SDHCI_HOST *Host, UINT8 HostControl, UINT32 RestoreHertz) {
    UINT32 i;
    UINT8 Buf[16];
    UINT8 Low[16];
    int Rc;

    for (i = 0; i < 16u; i++) {
        Buf[i] = 0;
    }
    /* Increment=1：CIS 是存储区，必须自增；固定地址只适合 FIFO */
    if (SdhciIoRwExtended(Host, 0, 0x1000u, 0, 1, 0, 16u, Buf) != 0) {
        SdioMarkXfer(Host, "WiFi: dat rd cmd53 fn0 cis incr=1 FAIL");
        return -1;
    }
    Rc = SdioCmpCisRead(Host, "WiFi: dat rd", 0x1000u, Buf, 16u);
    if (Rc == 0) {
        SdhciMark("WiFi: DAT read ok (cmd53 incr=1 == cmd52, 16B)\n");
        return 0;
    }

    /* 降速对照：25 MHz / 1-bit（卡侧仍是 4-bit，CMD53 只走 DAT0 时无影响） */
    SdhciMark("WiFi: dat rd retry 25MHz 1-bit\n");
    SdhciWrite8(Host, SDHCI_HOST_CONTROL, 0);
    (void)SdhciClockConfigure(Host, SD_TRANSFER_HERTZ);
    for (i = 0; i < 16u; i++) {
        Low[i] = 0;
    }
    if (SdhciIoRwExtended(Host, 0, 0x1000u, 0, 1, 0, 16u, Low) == 0) {
        Rc = SdioCmpCisRead(Host, "WiFi: low cis0", 0x1000u, Low, 16u);
        SdhciMark(Rc == 0 ? "WiFi: low-speed read matches cmd52\n"
                          : "WiFi: low-speed read ALSO mismatches\n");
    } else {
        SdhciMark("WiFi: low-speed cmd53 failed\n");
        Rc = -1;
    }
    SdhciWrite8(Host, SDHCI_HOST_CONTROL, HostControl);
    (void)SdhciClockConfigure(Host, RestoreHertz);
    SdhciMarkHex("WiFi: bus restored hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
    SdhciMarkHex(" clk=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
    SdhciMark("\n");
    return Rc;
}

/*
 * DAT 方向自检（S3 收尾）。
 *
 * 顺序按"能确定的事先做"：
 *   1) CMD53 fn0 CIS 读 16 B   —— 证明 DAT0-3 读方向通（与固件无关）
 *   2) FIFO 写 1536 B          —— 证明写方向通（S5 下载固件要用）
 *   3) FIFO 读"探一下"         —— **只读 4 B，且允许失败**
 *
 * 第 3 步为什么不再读 1536 B：AIC 的 fn1 FIFO 是**消息通道**，不是回环。
 * 厂商只在芯片有消息时才读（aicwf_sdio_bus_rx → sdio_readsb，且由 flow-ctrl/中断
 * 驱动），FIFO 空的时候去读，DAT 线永远不结束 —— 09:10 那次
 * `pst=03f20206`（DOING_READ + DAT_ACTIVE + DATA_INHIBIT）就是这个。
 * 所以这里只在"有消息"时才有意义；没有就是正常，收尾把总线拉回来即可。
 */
static void SdioFifoTest(SDHCI_HOST *Host, int MultiBlock) {
    UINT32 i;
    UINT8 HostControl = SdhciRead8(Host, SDHCI_HOST_CONTROL);
    UINT32 RestoreHertz = Host->ActualClockHertz;

    /* fn1 的两个 FIFO 窗口是固定常量（aicsdio.c:1708-1719 按 chipid 选），
       不需要先写什么 window/len 寄存器（用户要求第 2 点）。 */
    SdhciMarkHex("WiFi: fn1 fifo rd=", AIC_RD_FIFO_ADDR_V3, 2);
    SdhciMarkHex(" wr=", AIC_WR_FIFO_ADDR_V3, 2);
    SdhciMark(" (fixed, no window reg)\n");

    if (SdioDatReadCheck(Host, HostControl, RestoreHertz) != 0) {
        (void)SdioRecoverBus(Host, HostControl, RestoreHertz);
    }

    for (i = 0; i < SDIO_SELFTEST_BYTES; i++) {
        gSdioPattern[i] = (UINT8)((i * 7u + 0xA5u) & 0xFFu);
        gSdioReadback[i] = 0;
    }
    SdhciMark(MultiBlock ? "WiFi: FIFO mode=block x3\n" : "WiFi: FIFO mode=byte x3\n");
    if (SdioFifoTransfer(Host, 1, gSdioPattern, MultiBlock) != 0) {
        SdhciFail(Host, "WiFi: FIFO write 1536B fail");
        (void)SdioRecoverBus(Host, HostControl, RestoreHertz);
        return;
    }
    SdhciMark("WiFi: FIFO write 1536B ok\n");

    /* 读方向只探 4 B：有消息就是消息头，没有就超时退出 */
    for (i = 0; i < 4u; i++) {
        gSdioReadback[i] = 0;
    }
    if (SdhciIoRwExtended(Host, 1, AIC_RD_FIFO_ADDR_V3, 0, 0, 0, 4u, gSdioReadback) != 0) {
        SdioMarkXfer(Host, "WiFi: FIFO read 4B");
        SdhciMark("WiFi: FIFO read: no message pending (expect before fw download)\n");
        (void)SdioRecoverBus(Host, HostControl, RestoreHertz);
        return;
    }
    SdhciMarkHex("WiFi: FIFO read 4B ok hdr=", gSdioReadback[0], 2);
    SdhciMarkHex(" ", gSdioReadback[1], 2);
    SdhciMarkHex(" ", gSdioReadback[2], 2);
    SdhciMarkHex(" ", gSdioReadback[3], 2);
    SdhciMark("\n");
}

/* 每条失败路径统一走这里：打一行原因 + 置失败态（避免"静默返回"查不到信息） */
static int SdioFail(const char *Where) {
    SdhciMark("WiFi: FAIL at ");
    SdhciMark(Where);
    SdhciMark("\n");
    gWifiSdioState = -1;
    return -1;
}

/*
 * 厂商芯片表（aicwf_sdio.h:75-80，判定逻辑 aicsdio.c:239-259 / aicwf_sdio.c:528-552）：
 *   AIC8801    : vendor 0x5449 / device 0x0145
 *   AIC8800DC  : vendor 0xc8a1 / device 0xc08d
 *   AIC8800D80 : vendor 0xc8a1 / device 0x0082   ← Duo S 板载这颗
 * 注意：厂商"认芯片"用的是 **CIS 里的 vendor/device**，不是读某个寄存器。
 */
static const char *SdioChipName(UINT16 Vendor, UINT16 Device) {
    if (Vendor == 0xC8A1u && Device == 0x0082u) {
        return "aic8800d80";
    }
    if (Vendor == 0xC8A1u && Device == 0xC08Du) {
        return "aic8800dc";
    }
    if (Vendor == 0x5449u && Device == 0x0145u) {
        return "aic8801";
    }
    return 0;
}

static int SdioChipIdentify(void) {
    const char *Name = SdioChipName(gWifiChipVendor, gWifiChipDevice);

    if (Name == 0) {
        SdhciMarkHex("WiFi: chip id=unknown manfid=", gWifiChipVendor, 4);
        SdhciMarkHex(" pid=", gWifiChipDevice, 4);
        SdhciMark("\n");
        SdhciMark("WiFi:   vendor table: 8801=5449/0145 8800dc=c8a1/c08d 8800d80=c8a1/0082\n");
        return -1;
    }
    SdhciMark("WiFi: chip id=");
    SdhciMark(Name);
    SdhciMarkHex(" (manfid=", gWifiChipVendor, 4);
    SdhciMarkHex(" pid=", gWifiChipDevice, 4);
    SdhciMark(")\n");
    /*
     * chip_rev（0x40500000）在 AIC 里**不是寄存器读**，要发 dbg-mem-read 消息走 msg 通道
     * （aic_bsp_driver.c:1876-1880、1924-1929），而它只用来在 fw_8800d80_u01/u02 之间选；
     * SDK 里 D80 只发了 u02 一套（fmacfw_8800d80_u02.bin 等），所以 S5b 直接锁 u02。
     */
    SdhciMark("WiFi: fw profile=8800d80_u02 (only u02 ships; chip_rev read deferred)\n");
    return 0;
}

/*
 * fn1 的 V3 寄存器快照（aicsdio.h:35-48 的地址表）：只读，不写一个字节。
 * 00 INTR_ENABLE / 01 INTR_PENDING / 02 INTR_TO_DEVICE / 03 FLOW_CTRL_Q1 /
 * 04 MISC_INT_STATUS / 05 BYTEMODE_LEN / 06 BYTEMODE_LEN_MSB / 07 BYTEMODE_ENABLE /
 * 08 MISC_CTRL / 09 FLOW_CTRL_Q2 / 0A CLK_TEST_RESULT
 */
static void SdioMarkFn1Regs(SDHCI_HOST *Host) {
    static const UINT8 Off[11] = {0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u,
                                  0x06u, 0x07u, 0x08u, 0x09u, 0x0Au};
    static const char *const Name[11] = {"ien", "ipd", "itd", "fc1", "misc", "len",
                                         "lenh", "bme", "mctl", "fc2", "clk"};
    UINT32 i;
    UINT8 Byte;

    SdhciMark("WiFi: fn1 regs");
    for (i = 0; i < 11u; i++) {
        Byte = 0;
        SdhciMark(" ");
        SdhciMark(Name[i]);
        SdhciMark("=");
        if (SdhciIoRead8(Host, 1, Off[i], &Byte) != 0) {
            SdhciMark("xx");
        } else {
            SdhciMarkHex("", Byte, 2);
        }
    }
    SdhciMark("\n");
}

/* CMD5 现场：PRESENT_STATE / 原始 INT_STATUS / 高 16 位错误位 / INT_ENABLE / 时钟 */
static void SdioMarkCmd5State(SDHCI_HOST *Host, const char *Tag) {
    UINT32 St = SdhciRead32(Host, SDHCI_INT_STATUS);

    SdhciMark(Tag);
    SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
    SdhciMarkHex(" int=", St, 8);
    SdhciMarkHex(" err=", (St >> 16) & 0xFFFFu, 4);
    SdhciMarkHex(" ien=", SdhciRead32(Host, SDHCI_INT_ENABLE), 8);
    SdhciMarkHex(" clk=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
    SdhciMark("\n");
}

/*
 * CMD5 探活窗口（厂商给芯片 2000 ms，见 aicsdio.c:534）。
 * 两道门：
 *   1) 进循环前先看 CMD_INHIBIT：卡住就先 RESET_ALL + 400k 把命令 FSM 拽回来，
 *      还卡就直接判死，不白跑 12 次（08:24 那次就是这个形态）。
 *   2) 每次失败后再看一次，一旦 inhibit 卡住立刻跳出去做同一套恢复。
 * 返回 0 = 通了。
 */
static int SdioCmd5Window(SDHCI_HOST *Host) {
    UINT16 Flags = (UINT16)SDHCI_CMD_RESP_SHORT; /* R4 无 CRC / 无 index */
    UINT32 Pst;
    UINT32 Round;
    UINT32 i;
    UINT32 Tries;

    for (Round = 0; Round < 2u; Round++) {
        Tries = (Round == 0) ? DUO_WIFI_CMD5_TRIES : 4u;
        Pst = SdhciRead32(Host, SDHCI_PRESENT_STATE);
        if ((Pst & SDHCI_CMD_INHIBIT) != 0) {
            SdhciMark("WiFi: CMD5 blocked by CMD_INHIBIT (cmd line busy)\n");
            SdioMarkCmd5State(Host, "WiFi:  ");
            if (Round != 0) {
                return -1;
            }
            SdhciMark("WiFi: recover: RESET_ALL + 400kHz\n");
            if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0 ||
                SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
                return -1;
            }
            continue;
        }
        for (i = 0; i < Tries; i++) {
            if (SdhciCommandSend(Host, 5, 0, Flags, 0) == 0) {
                SdhciMarkDecimal("WiFi: CMD5 ok after #", i + 1u);
                SdhciMark("\n");
                return 0;
            }
            SdhciMarkDecimal("WiFi: CMD5 #", i + 1u);
            SdhciMarkHex(" st=", Host->LastStatus, 8);
            SdhciMarkHex(" pst=", Host->LastPresentState, 8);
            SdhciMark("\n");
            if ((Host->LastPresentState & SDHCI_CMD_INHIBIT) != 0) {
                SdhciMark("WiFi: CMD_INHIBIT stuck after this try\n");
                break;
            }
            SdhciDelayMilliseconds(DUO_WIFI_CMD5_GAP_MS);
        }
        if (Round != 0) {
            break;
        }
        SdhciMark("WiFi: recover: RESET_ALL + 400kHz\n");
        if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0 ||
            SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
            return -1;
        }
    }
    return -1;
}

/*
 * 供 shell 的 wifiprobe 调：Force=1 清掉"只试一次"的状态再跑。
 * 不清 SD0 的 Ready（启动盘不受影响）。
 */
int SdhciWifiProbe(UINT32 Flags) {
    SDHCI_HOST *Host = SdhciHostAt(1);

    if ((Flags & SDHCI_WIFI_PROBE_WAKE) != 0) {
        DuoWifiWakeDrive(1);
    }
    if ((Flags & SDHCI_WIFI_PROBE_FORCE) != 0) {
        gWifiSdioState = 0;
        if (Host != 0) {
            Host->Ready = 0;
            Host->CardType = SDHCI_CARD_NONE;
            Host->BusWidth = 1;
            Host->HighCapacity = 0;
            Host->RelativeCardAddress = 0;
        }
        SdhciMark("WiFi: state cleared for retry\n");
    }
    return SdhciWifiInitialize();
}

/*
 * 显式 FIFO 自检入口（shell: wififw fifotest）。只做人工诊断用：
 * 无论成败都不改 Host->Ready / gWifiSdioState —— 枚举成功与否只由
 * CMD5→3→7 + CCCR + CIS 决定。
 */
int SdhciWifiFifoTest(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT8 Caps = 0;

    if (Host == 0 || gWifiSdioState != 1) {
        SdhciMark("WiFi: fifotest needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CAPS, &Caps) != 0) {
        Caps = 0;
    }
    /* 走字节模式的路径必须先确认 fn1 0x07 的允许位（厂商 V3 写 0x01） */
    {
        UINT8 Bm = 0;
        if (SdhciIoRead8(Host, 1, AIC_BYTEMODE_ENABLE_REG_V3, &Bm) == 0 && (Bm & 0x01u) == 0) {
            (void)SdhciIoWrite8(Host, 1, AIC_BYTEMODE_ENABLE_REG_V3, 0x01u);
            (void)SdhciIoRead8(Host, 1, AIC_BYTEMODE_ENABLE_REG_V3, &Bm);
            SdhciMarkHex("WiFi: fifotest set fn1 0x07 -> ", Bm, 2);
            SdhciMark("\n");
        }
    }
    SdhciMark("WiFi: fifotest begin (explicit, not part of enum)\n");
    SdioFifoTest(Host, (Caps & SDIO_CAP_SMB) ? 1 : 0);
    SdhciMark("WiFi: fifotest end (state untouched)\n");
    return 0;
}

/* S5b 用：枚举是否成功 / 总线卡住时自救 / 身份是否 D80 */
int SdhciWifiReady(void) {
    return (gWifiSdioState == 1) ? 1 : 0;
}

int SdhciWifiChipIsD80(void) {
    return (gWifiChipVendor == 0xC8A1u && gWifiChipDevice == 0x0082u) ? 1 : 0;
}

int SdhciWifiBusRecover(SDHCI_HOST *Host) {
    if (Host == 0) {
        return -1;
    }
    return SdioRecoverBus(Host, gWifiHostControl, gWifiRestoreHertz);
}

int SdhciWifiInitialize(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 Ocr = 0;
    UINT32 Offer;
    UINT32 i;
    UINT32 NoResponse = 0;
    UINT16 Flags;
    UINT16 Version;
    UINT8 Data;
    UINT8 Speed;
    UINT8 Caps = 0;
    SDIO_CIS_INFO Cis;
    UINT32 Present;

    /* 无条件第一行：证明本函数被调到、以及跑的是哪一次构建 */
    SdhciMark("WiFi: probe start build=" __DATE__ " " __TIME__ "\n");

    if (gWifiSdioState != 0) {
        SdhciMark(gWifiSdioState > 0 ? "WiFi: skip (already ok; use wifiprobe to retry)\n"
                                     : "WiFi: skip (last=fail; use wifiprobe to retry)\n");
        return (gWifiSdioState > 0) ? 0 : -1;
    }
    if (Host == 0) {
        return SdioFail("SdhciHostAt(1)==NULL");
    }

    Version = SdhciRead16(Host, SDHCI_HOST_VERSION);
    SdhciMarkHex("WiFi: SD1 base=", (UINT32)Host->Base, 8);
    SdhciMarkHex(" version=", Version, 4);
    if (Version == 0x0000u || Version == 0xFFFFu) {
        SdhciMark(" (MMIO no response: clock/power/base?)");
    }
    SdhciMark("\n");

    /*
     * ---------- S1：时钟门控（先于控制器复位） ----------
     * Linux 由 CCF 打开 clk_axi4_sd1/clk_sd1/clk_100k_sd1，我们得自己开；
     * 真机上 shift 前的 CLK_EN_0 就是这段要查的东西。
     */
    (void)DuoWifiClockGate();
    DuoWifiPadPeek();

    Host->Specification = (UINT16)(Version & 0xFFu);
    if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0) {
        SdhciMarkHex("WiFi:   SRSI=", SdhciRead8(Host, SDHCI_SOFTWARE_RESET), 2);
        SdhciMarkHex(" clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
        SdhciMark("\n");
        return SdioFail("software reset ALL");
    }
    SdhciMark("Duo SDHCI1 ");
    SdhciMarkHex("ver=", Host->Specification, 8);
    SdhciMarkHex(" cap=", SdhciRead32(Host, SDHCI_CAPABILITIES), 8);
    SdhciMarkHex(" area=", Host->VendorArea, 3);
    SdhciMark("\n");
    SdhciMarkHex("WiFi: MSHC_CTRL=", SdhciRead32(Host, Host->VendorArea + CV_MSHC_CTRL), 8);
    SdhciMarkHex(" phy=", SdhciRead32(Host, Host->VendorArea + CV_PHY_TX_RX), 8);
    SdhciMark("\n");

    /*
     * INT_ENABLE = 0xFFFFFFFF（和 SD0 的 Card.c:52 完全一样）。
     *
     * 这里原来是 0 —— 那是 08:24 那次真机 "CMD5 有去无回、INT_STATUS 恒 0、
     * PRESENT_STATE[0] CMD_INHIBIT 卡死" 的根因：SDHCI 的 Interrupt Status
     * Enable 是状态位的总闸，闸关着 -> 事件不发生 -> 命令完成/超时都看不见 ->
     * 命令 FSM 等软件 W1C 应答等到天荒地老 -> CMD_INHIBIT 恒 1，重发全部
     * 在第一道门就失败。SD0 一直是 0xFFFFFFFF，所以启动盘从没出过这问题。
     */
    if (SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
        SdhciMarkHex("WiFi:   clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
        SdhciMarkHex(" hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
        SdhciMark("\n");
        return SdioFail("clock 400kHz");
    }
    SdhciMarkHex("WiFi: 400k ok clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
    SdhciMarkHex(" pwr=", SdhciRead8(Host, SDHCI_POWER_CONTROL), 2);
    SdhciMarkHex(" hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
    SdhciMark("\n");

    /*
     * ---------- S1：上电（时钟已经跑起来了，厂商也是先有 400k 再给模块上电） ----------
     * 厂商 aicsdio.c:499-520：gpio 高 → mdelay(50)；这里多一个低 100ms 的断电脉冲，
     * 因为我们是接手 FSBL/U-Boot 留下的现场。
     */
    if (DuoWifiPowerOn() != 0) {
        return SdioFail("GPIOA15 power-on");
    }
    DuoWifiWakePeek();

    /*
     * 卡检测覆盖 + 极性自测（要控制器起来才能读 PRESENT_STATE）。
     * Linux 在 non-removable 上根本不看这两位（sdhci.c:2438-2466 直接 return 1），
     * 所以只打印、不当作继续/放弃的门禁。
     */
    if (DuoWifiCardDetect(Host) != 0) {
        return SdioFail("TOP 0x294 card-detect override");
    }

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

    /*
     * ---------- S3：CMD5 探活（arg=0，sdio_ops.c mmc_send_io_op_cond 的 "just probing"） ----------
     * 细节在 SdioCmd5Window()：先查 CMD_INHIBIT、失败就 RESET_ALL 重来、共约 2 s 窗口。
     */
    Flags = (UINT16)SDHCI_CMD_RESP_SHORT; /* R4 无 CRC / 无 index */
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
    Offer = 0x00300000u & Host->Response[0];
    if (Offer == 0) {
        Offer = 0x00300000u;
    }
    SdhciMarkHex("WiFi: CMD5 offer=", Offer, 8);
    SdhciMark("\n");

    for (i = 0; i < 100u; i++) {
        if (SdhciCommandSend(Host, 5, Offer, Flags, 0) != 0) {
            NoResponse++;
            if (NoResponse >= 3u) {
                /* 连发都不通了：再轮下去只是白等，直接给出寄存器现场 */
                SdhciMarkHex("WiFi: CMD5 send-fail count=0x", NoResponse, 2);
                SdhciMarkHex(" int=", SdhciRead32(Host, SDHCI_INT_STATUS), 8);
                SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
                SdhciMark("\n");
                return SdioFail("CMD5 (keeps failing, cmd line dead)");
            }
            SdhciDelayMilliseconds(10);
            continue;
        }
        NoResponse = 0;
        Ocr = Host->Response[0];
        if ((Ocr & 0x80000000u) != 0) {
            break;
        }
        SdhciDelayMilliseconds(10);
    }
    if ((Ocr & 0x80000000u) == 0) {
        SdhciMarkHex("WiFi: CMD5 responds but not ready ocr=", Ocr, 8);
        SdhciMarkHex(" tries=", i, 2);
        SdhciMark("\n");
        return SdioFail("CMD5 (ready timeout: voltage/power)");
    }
    SdhciMarkHex("WiFi: ocr=", Ocr, 8);
    SdhciMark("\n");

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 3, 0, Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: CMD3 fail");
        return SdioFail("CMD3 (RCA)");
    }
    Host->RelativeCardAddress = Host->Response[0] & 0xFFFF0000u;
    SdhciMarkHex("WiFi: rca=", Host->RelativeCardAddress >> 16, 4);
    SdhciMark("\n");

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 7, Host->RelativeCardAddress, Flags, 0) != 0) {
        SdhciFail(Host, "WiFi: CMD7 fail");
        return SdioFail("CMD7 (select)");
    }

    /* ---------- S3：CCCR（CMD52 读） ---------- */
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CCCR, &Data) != 0) {
        SdhciMarkHex("WiFi:   CMD52 00 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x00 read (CMD52 path)");
    }
    SdhciMarkHex("WiFi: CCCR 00=", Data, 2);
    SdhciMarkDecimal(" rev=", (UINT32)(Data & 0x0Fu));
    SdhciMarkDecimal(" sdio=", (UINT32)(Data >> 4));
    SdhciMark("\n");

    if (SdhciIoRead8(Host, 0, SDIO_CCCR_CAPS, &Caps) != 0) {
        SdhciMarkHex("WiFi:   CMD52 08 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x08 read");
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
        SdhciMarkHex("WiFi:   CMD52 13 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x13 read (speed)");
    }
    SdhciMarkHex("WiFi: CCCR 13=", Speed, 2);
    SdhciMark(" SHS=");
    SdhciMark((Speed & SDIO_SPEED_SHS) ? "1" : "0");
    SdhciMark("\n");
    if ((Speed & SDIO_SPEED_SHS) != 0) {
        if (SdhciIoWrite8(Host, 0, SDIO_CCCR_SPEED, (UINT8)(Speed | SDIO_SPEED_EHS)) != 0 ||
            SdhciIoRead8(Host, 0, SDIO_CCCR_SPEED, &Speed) != 0) {
            SdhciMarkHex("WiFi:   EHS write r5=", Host->LastStatus, 8);
            SdhciMark("\n");
            return SdioFail("CCCR 0x13 write (HS)");
        }
        SdhciMarkHex("WiFi: CCCR 13->", Speed, 2);
        SdhciMark(" EHS=");
        SdhciMark((Speed & SDIO_SPEED_EHS) ? "1" : "0");
        SdhciMark("\n");
        if ((Speed & SDIO_SPEED_EHS) == 0) {
            return SdioFail("HS switch (card refused EHS)");
        }
    } else {
        SdhciMark("WiFi: card no HS, stay 25MHz\n");
    }
    if (SdhciClockConfigure(Host, (Speed & SDIO_SPEED_EHS) ? TRANSFER_HERTZ : SD_TRANSFER_HERTZ) !=
        0) {
        SdhciMarkHex("WiFi:   clock=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
        SdhciMark("\n");
        return SdioFail("clock 50MHz");
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
        SdhciMarkHex("WiFi:   CMD52 07 r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("CCCR 0x07 read (bus if)");
    }
    if (SdhciIoWrite8(Host, 0, SDIO_CCCR_IF,
                      (UINT8)((Data & (UINT8)~SDIO_BUS_WIDTH_MASK) | SDIO_BUS_WIDTH_4BIT)) != 0 ||
        SdhciIoRead8(Host, 0, SDIO_CCCR_IF, &Data) != 0 ||
        (Data & SDIO_BUS_WIDTH_MASK) != SDIO_BUS_WIDTH_4BIT) {
        SdhciMarkHex("WiFi: 4bit readback if=", Data, 2);
        SdhciMarkHex(" r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("4-bit switch");
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
            SdhciMarkHex("WiFi:   fn1 blk r5=", Host->LastStatus, 8);
            SdhciMark("\n");
            return SdioFail("fn1 block size");
        }
        SdhciMarkHex("WiFi: fn1 blk=", (UINT32)Lo | ((UINT32)Hi << 8), 4);
        SdhciMark("\n");
    }
    if (SdhciIoRead8(Host, 0, SDIO_CCCR_IOEX, &Data) != 0 ||
        SdhciIoWrite8(Host, 0, SDIO_CCCR_IOEX, (UINT8)(Data | 0x02u)) != 0) {
        SdhciMarkHex("WiFi:   IOEx r5=", Host->LastStatus, 8);
        SdhciMark("\n");
        return SdioFail("fn1 enable (IOEx)");
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
        SdhciMarkHex("WiFi:   IORx=", Data, 2);
        SdhciMarkHex(" waited=", i, 4);
        SdhciMark(" ms\n");
        return SdioFail("fn1 enable timeout (IORx)");
    }
    SdhciMark("WiFi: fn1 enabled\n");

    /* S3 的验收行：总线（时钟/宽度）+ fn1 都就绪之后才打 */
    SdhciMark((Speed & SDIO_SPEED_EHS) ? "WiFi: SD1 init ok clock=50MHz width=4bit\n"
                                       : "WiFi: SD1 init ok clock=25MHz width=4bit\n");

    /*
     * ---------- S3 验收：读 CIS ----------
     *
     * 上一版在这里判死的根因：**MANFID(0x20) 是 Common CIS 的元组，不在 function CIS 里**
     * （SDIO 规范 §16.4；Linux sdio_cis.c:201 的 sdio_read_common_cis 读 fn0 指针，
     * sdio_read_func_cis 读 fnN 指针）。旧代码拿 fn1 的指针 0x1011 去解析却要求 MANFID，
     * 那个地址是**函数自己的 CIS**（只有 FUNCE 之类的元组），所以 manfid=0000 —— 
     * 指针没错，是找错了地方。
     *
     * 现在：Common CIS（fn0 指针 0x09-0x0B）拿 MANFID/器件号，
     *       fn1 的指针（0x109-0x10B）只拿函数自己的元组（FUNCE 块大小）。
     * 找不到 MANFID 也不再判死：打真实值、扫一遍、必要时降速重读一次，然后继续。
     */
    {
        UINT32 CommonPtr = 0;
        UINT32 Fn1Ptr = 0;
        UINT32 Hit = 0;
        SDIO_CIS_INFO Fn1;
        int CisOk = -1;
        UINT8 HostControl = SdhciRead8(Host, SDHCI_HOST_CONTROL);
        UINT32 RestoreHertz =
            ((Speed & SDIO_SPEED_EHS) != 0) ? TRANSFER_HERTZ : SD_TRANSFER_HERTZ;

        gWifiHostControl = HostControl;
        gWifiRestoreHertz = RestoreHertz;
        if (SdioReadCisPointer(Host, 0, &CommonPtr) != 0) {
            SdhciMark("WiFi: common CIS pointer (0x09-0x0B) read failed\n");
            CommonPtr = 0;
        }
        if (SdioReadCisPointer(Host, 1, &Fn1Ptr) != 0) {
            return SdioFail("CIS pointer fn1 (0x109-0x10B)");
        }
        SdhciMarkHex("WiFi: cis ptr fn0=", CommonPtr, 6);
        SdhciMarkHex(" fn1=", Fn1Ptr, 6);
        SdhciMark("\n");
        /* 一次把现场抓全：fn1 的 FBR 原文 + 两个 CIS 的头 16 B */
        SdioDumpCis(Host, "WiFi: fbr1 ", 0x100u, 16u);
        if (CommonPtr != 0) {
            SdioDumpCis(Host, "WiFi: cis0 ", CommonPtr, 16u);
            CisOk = SdioParseCis(Host, CommonPtr, &Cis);
        }
        SdioDumpCis(Host, "WiFi: cis1 ", Fn1Ptr, 16u);
        if (SdioParseCis(Host, Fn1Ptr, &Fn1) == 0) {
            SdioMarkCis(1, Fn1Ptr, &Fn1);
        }

        if (CisOk != 0 || Cis.SawManfid == 0) {
            /* 兜底 1：直接扫 CIS 区里的 0x20 0x04 */
            Hit = SdioScanCis(Host, 0x1000u, 0x1400u);
            if (Hit != 0) {
                SdhciMarkHex("WiFi: MANFID scan hit ", Hit, 6);
                SdhciMark("\n");
                if (SdioParseCis(Host, Hit, &Cis) == 0 && Cis.SawManfid != 0) {
                    CisOk = 0;
                }
            }
        }
        if (CisOk != 0 || Cis.SawManfid == 0) {
            /* 兜底 2：退回 1-bit / 400 kHz 再读一次 Common CIS（对照 50 MHz 4-bit 的结果） */
            SdhciMark("WiFi: no MANFID yet, retry 1-bit 400kHz\n");
            SdhciWrite8(Host, SDHCI_HOST_CONTROL, 0);
            (void)SdhciClockConfigure(Host, IDENTIFY_HERTZ);
            CommonPtr = 0;
            if (SdioReadCisPointer(Host, 0, &CommonPtr) == 0 && CommonPtr != 0) {
                SdhciMarkHex("WiFi:   low cis0 ptr=", CommonPtr, 6);
                SdhciMark("\n");
                SdioDumpCis(Host, "WiFi:   low cis0 ", CommonPtr, 16u);
                if (SdioParseCis(Host, CommonPtr, &Cis) == 0 && Cis.SawManfid != 0) {
                    CisOk = 0;
                }
            }
            SdhciWrite8(Host, SDHCI_HOST_CONTROL, HostControl);
            (void)SdhciClockConfigure(Host, RestoreHertz);
            SdhciMarkHex("WiFi: bus restored hctl=", SdhciRead8(Host, SDHCI_HOST_CONTROL), 2);
            SdhciMarkHex(" clk=", SdhciRead16(Host, SDHCI_CLOCK_CONTROL), 4);
            SdhciMark("\n");
        }

        if (CisOk == 0 && Cis.SawManfid != 0) {
            SdhciMark("WiFi: module=");
            SdhciMarkHex("", Cis.Vendor, 4);
            SdhciMarkHex(" device=", Cis.Device, 4);
            SdhciMark((Cis.Vendor == 0xC8A1u) ? " (aic8800)\n" : " (unexpected, continue)\n");
            gWifiChipVendor = Cis.Vendor;
            gWifiChipDevice = Cis.Device;
            SdioMarkCis(0, CommonPtr, &Cis);
        } else {
            SdhciMark("WiFi: no MANFID tuple, continue without module id\n");
        }

        /*
         * ---------- S5b-1：芯片识别（只读，不写任何寄存器） ----------
         * 串口判据：`WiFi: chip id=aic8800d80 (manfid=c8a1 pid=0082)`。
         * 判据不到不写固件 —— 这道闸放在下载命令（wififw）里，不拦 S3 枚举，
         * 否则一颗身份异常但枚举正常的卡会把 S3 的验收线也一起弄挂。
         */
        (void)SdioChipIdentify();
        SdioMarkFn1Regs(Host);

        /*
         * fn2 的 CIS 顺手读一下。注意：fn2 **不是**没用的 —— 厂商把消息/确认通道放在
         * fn2（func_msg，aicwf_sdio_recv_pkt 的 msg=1 分支，aicsdio.c:766-772），
         * S5b 的 dbg mem read/write 确认就是从 fn2 0x0F 读回来的。
         */
        {
            UINT32 Ptr2 = 0;
            SDIO_CIS_INFO Cis2;
            if (SdioReadCisPointer(Host, 2, &Ptr2) == 0 &&
                SdioParseCis(Host, Ptr2, &Cis2) == 0 && Cis2.SawManfid != 0) {
                SdioMarkCis(2, Ptr2, &Cis2);
            }
        }
    }

    /*
     * ---------- S3 收尾 ----------
     * **验收线是 CIS，不是 FIFO 回环。** 早期把"写 1536 B 图案再读回来比对"当成功判据，
     * 那是错的：1536 是固件消息补齐后的线上长度（时序表 §2.6），读回来的长度得由
     * fn1 0x04 的待收块数决定，图案回读对不上是正常现象。
     * 更要紧的是：协议还没开始就往发送 FIFO 推 1536 B 垃圾，可能污染 S5 的第一条消息。
     * 所以默认路径**不跑**，只留一句"跳过"，要跑得人工 `wififw fifotest`。
     */
    SdhciMark("WiFi: sdio enum ok (cis)\n");
    SdhciMark("WiFi: sdio fifo self-test skipped (run 'wififw fifotest')\n");

    /* fn1 0x04 杂项中断状态：本轮不轮询，只打一次，供 S4 对照 */
    if (SdhciIoRead8(Host, 1, AIC_MISC_INT_STATUS_REG_V3, &Data) == 0) {
        SdhciMarkHex("WiFi: fn1 misc=0x", Data, 2);
        SdhciMark("\n");
    }
    Host->CardType = SDHCI_CARD_SDIO;
    Host->Ready = 1;
    gWifiSdioState = 1;
    SdhciMark("WiFi: probe end ok\n");
    return 0;
}
