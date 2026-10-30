/*
 * WifiFw.c — Duo S AIC8800D80 固件下载（PR-30 S5b）
 *
 * 帧格式、寄存器、地址、顺序全部照厂商 aic8800_bsp，不自创：
 *   - 帧头 4 B：len(12 bit) | type(0x11=CFG_CMD_RSP) | crc8
 *     （aic_bsp_driver.c:258-283 的 rwnx_set_cmd_tx，crc8 见 aicsdio.c:2004-2027）
 *   - 之后 4 B 是厂商的 dummy word，然后 lmac_msg 头 8 B：
 *     id / dest_id / src_id / param_len（aic_bsp_driver.h:144-177）
 *   - 请求写 fn1 0x10（FIFO，固定地址；aicsdio.c:742-751 sdio_writesb(func, wr_fifo_addr)）
 *   - 应答从 fn2 0x0F 读（Linux 用 func_msg：aicsdio.c:753-780、aicsdio_txrxif.c:213-215）
 *     芯片有消息时 fn1 0x04 的 bit7 置位（aicsdio.c:1458 SDIO_OTHER_INTERRUPT），先轮询它再读
 *   - adid / patch_table 用 dbg mem block write，1 KB 一块（aic_bsp_driver.c:1233-1278）
 *   - D80 u02 的落点：adid 0x00201940、patch 0x0020b43c（aic_bsp_driver.h:358-359）
 *
 * 固件不嵌内核（A 方案）：用 FileSystemReadFileAt 从卡上分块读，换固件不用重编内核。
 * 每步一个串口判据，判据不到就停：
 *   WiFi: fw read … ok / WiFi: chip rev=… / WiFi: adid ok / WiFi: patch_table ok
 */
#include "Sdhci/Private.h"
#include "FileSystem.h"

/* ---- AIC 消息常量（aic_bsp_driver.h:144-215、aicsdio.h:35-64） ---- */
#define AIC_TASK_DBG 1u
#define AIC_DRV_TASK_ID 100u
#define AIC_MSGID(Task, Index) ((UINT16)(((UINT16)(Task) << 10) | (UINT16)(Index)))
#define AIC_DBG_MEM_READ_REQ AIC_MSGID(AIC_TASK_DBG, 0u)
#define AIC_DBG_MEM_READ_CFM AIC_MSGID(AIC_TASK_DBG, 1u)
#define AIC_DBG_MEM_BLOCK_WRITE_REQ AIC_MSGID(AIC_TASK_DBG, 11u)
#define AIC_DBG_MEM_BLOCK_WRITE_CFM AIC_MSGID(AIC_TASK_DBG, 12u)

#define AIC_TYPE_CFG_CMD_RSP 0x11u
#define AIC_TYPE_CFG_DATA_CFM 0x12u
#define AIC_TYPE_CFG_PRINT 0x13u
#define AIC_MISC_OTHER_INTR 0x80u /* fn1 0x04 bit7：芯片有消息给主机 */

#define AIC_RD_FIFO_REG 0x0Fu  /* V3：fn2 读 FIFO */
#define AIC_WR_FIFO_REG 0x10u  /* V3：fn1 写 FIFO */
#define AIC_MISC_INT_REG 0x04u /* V3：fn1 MISC_INT_STATUS */

/* 帧长上限：16 B 头 + 参数；FIFO 一块 512 B，写时分块 */
#define AIC_MSG_MAX 1600u
#define AIC_FIFO_CHUNK 512u
#define AIC_BLK_WRITE 1024u /* 厂商一块 1 KB */
#define AIC_CFM_TIMEOUT_MS 500u
#define AIC_CFM_POLL_MS 1u

/* D80 u02 的固件落点（aic_bsp_driver.h:358-359） */
#define AIC_D80_U02_ADID_ADDR 0x00201940u
#define AIC_D80_U02_PATCH_ADDR 0x0020B43Cu
#define AIC_D80_CHIP_REV_ADDR 0x40500000u /* 版本号在芯片存储区，不是寄存器 */

/* 厂商 sdio_reg 里我们还要用到的两个（aicsdio.h:44、aicsdio.c:1714-1719） */
#define AIC_BYTEMODE_ENABLE_REG 0x07u /* fn1 V3：0x01 = 允许字节模式（aicsdio.c:1862） */
#define AIC_BYTEMODE_LEN_REG 0x05u    /* fn1 V3：字节模式长度（低 8 位，单位 4 B） */
#define AIC_BYTEMODE_LEN_MSB_REG 0x06u
#define AIC_FBR_BLKSIZE_L 0x110u
#define AIC_FBR_BLKSIZE_H 0x111u

#define AIC_FW_DIR "/Assets/Fw/aic8800/"
#define AIC_FW_ADID AIC_FW_DIR "fw_adid.bin"
#define AIC_FW_PATCH_TABLE AIC_FW_DIR "fw_patch_table.bin"

static UINT8 gAicMsg[AIC_MSG_MAX];
static UINT8 gAicRx[512];
static UINT8 gAicFwBuf[AIC_BLK_WRITE * 4u];

/* crc8，多项式 X8+X2+X+1（0x107），初值 0 —— 照抄 aicsdio.c:2004-2027 */
static UINT8 AicCrc8(const UINT8 *Buffer, UINT32 Size) {
    UINT8 Crc = 0;
    UINT32 i;
    UINT8 Bit;

    for (i = 0; i < Size; i++) {
        for (Bit = 0x80u; Bit != 0u; Bit = (UINT8)(Bit / 2u)) {
            if ((Crc & 0x80u) != 0) {
                Crc = (UINT8)(Crc * 2u);
                Crc ^= 0x07u;
            } else {
                Crc = (UINT8)(Crc * 2u);
            }
            if ((Buffer[i] & Bit) != 0u) {
                Crc ^= 0x07u;
            }
        }
    }
    return Crc;
}

static void AicPut16(UINT8 *At, UINT16 Value) {
    At[0] = (UINT8)(Value & 0xFFu);
    At[1] = (UINT8)((Value >> 8) & 0xFFu);
}

static UINT16 AicGet16(const UINT8 *At) {
    return (UINT16)((UINT16)At[0] | ((UINT16)At[1] << 8));
}

/* ---- 现场打印：把"门"和"写法"分得开 ---- */

static void AicMarkIoState(SDHCI_HOST *Host, const char *Tag) {
    UINT8 F2 = 0;
    UINT8 Cccr = 0;
    UINT8 IoEx = 0;
    UINT8 IoRx = 0;
    UINT8 BmEn = 0;
    UINT8 BmLen = 0;
    UINT8 Misc = 0;
    UINT8 BlkL = 0;
    UINT8 BlkH = 0;

    (void)SdhciIoRead8(Host, 0u, 0xF2u, &F2);
    (void)SdhciIoRead8(Host, 0u, 0x00u, &Cccr);
    (void)SdhciIoRead8(Host, 0u, 0x02u, &IoEx);
    (void)SdhciIoRead8(Host, 0u, 0x03u, &IoRx);
    (void)SdhciIoRead8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, &BmEn);
    (void)SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &BmLen);
    (void)SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Misc);
    /* FBR 是 fn0 地址空间（0x100-0x1FF），必须 fn0 + 绝对地址，见下面 vendor init 的注释 */
    (void)SdhciIoRead8(Host, 0u, AIC_FBR_BLKSIZE_L, &BlkL);
    (void)SdhciIoRead8(Host, 0u, AIC_FBR_BLKSIZE_H, &BlkH);

    SdhciMark(Tag);
    SdhciMarkHex(" fn0.f2=", F2, 2);
    SdhciMarkHex(" cccr=", Cccr, 2);
    SdhciMarkHex(" ioex=", IoEx, 2);
    SdhciMarkHex(" iorx=", IoRx, 2);
    SdhciMark("\n");
    SdhciMark("WiFi:  fn1.bme=");
    SdhciMarkHex("", BmEn, 2);
    SdhciMarkHex(" blen=", BmLen, 2);
    SdhciMarkHex(" misc=", Misc, 2);
    SdhciMarkHex(" fbrblk=", (UINT32)(BlkL | ((UINT32)BlkH << 8)), 4);
    SdhciMark("\n");
}

/* CMD52 写失败时把 R5 的每个错误位拆开（Io.c:16-23 的定义） */
static void AicMarkR5(UINT32 R5) {
    SdhciMarkHex(" r5=", R5, 8);
    if ((R5 & 0x8000u) != 0) {
        SdhciMark(" COM_CRC");
    }
    if ((R5 & 0x4000u) != 0) {
        SdhciMark(" ILLEGAL_CMD");
    }
    if ((R5 & 0x0800u) != 0) {
        SdhciMark(" IO_ERROR");
    }
    if ((R5 & 0x0200u) != 0) {
        SdhciMark(" FUNC_NUM");
    }
    if ((R5 & 0x0100u) != 0) {
        SdhciMark(" OUT_OF_RANGE");
    }
    SdhciMark("\n");
}

/* 写一个寄存器并读回：失败打全 cmd/arg/old/want/r5/pst/int/err */
static int AicWriteReg(SDHCI_HOST *Host, UINT32 Fn, UINT32 Addr, UINT8 Value, const char *What) {
    UINT8 Before = 0;
    UINT8 After = 0;

    (void)SdhciIoRead8(Host, Fn, Addr, &Before);
    if (SdhciIoWrite8(Host, Fn, Addr, Value) == 0) {
        (void)SdhciIoRead8(Host, Fn, Addr, &After);
        SdhciMark("WiFi:  ");
        SdhciMark(What);
        SdhciMarkHex(" wrote=", Value, 2);
        SdhciMarkHex(" old=", Before, 2);
        SdhciMarkHex(" now=", After, 2);
        if (After != Value) {
            /* 有些位只写/固定（fn0 0xF2 初值 0x73），读到不同不算失败，打出来继续 */
            SdhciMark(" (readback differs, continue)");
        }
        SdhciMark("\n");
        return 0;
    }
    SdhciMark("WiFi:  ");
    SdhciMark(What);
    SdhciMark(" FAIL cmd=52");
    SdhciMarkHex(" arg=", ((Fn & 7u) << 28) | ((Addr & 0x1FFFFu) << 9) | (UINT32)Value, 8);
    SdhciMarkHex(" old=", Before, 2);
    SdhciMarkHex(" want=", Value, 2);
    AicMarkR5(Host->LastStatus);
    SdhciMarkHex("WiFi:  pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
    SdhciMarkHex(" int=", SdhciRead32(Host, SDHCI_INT_STATUS), 8);
    SdhciMarkHex(" err=", (Host->LastStatus >> 16) & 0xFFFFu, 4);
    SdhciMark("\n");
    return -1;
}

/*
 * 厂商 V3（D80）下载前的固定动作（aicsdio.c:1820-1877），按 SDIO 规范校正了三处：
 *
 * **1. FBR 不是 fn1 的地址。** CCCR(0x00-0xFF)、FBR(0x100-0x1FF)、CIA(0x1000+) 全在
 *    **fn0 的地址空间**里，用 function=0 + **绝对地址**访问。`0x110` 是"fn0 空间里、
 *    function 1 的 FBR 块长寄存器"。Linux `sdio_set_block_size()` 就是
 *    `mmc_io_rw_direct(card, 1, 0, SDIO_FBR_BASE(num) + 0x10, ...)`（第三个参数是 fn=0）
 *    （SDIO_FBR_BASE(f) = f*0x100）。
 *    以前写成 fn1 + 0x110 → 卡返回 OUT_OF_RANGE（真机 arg=10022000 r5=00001100），
 *    因为 fn1 根本没有 0x110 这个地址。
 *
 * **2. IOEx 只动 fn1 位**（|= 0x02），不写 0x03 —— 没有理由顺手把 fn2 也改掉。
 *    真机上 bit0 回读不变，按"只读/保留位"处理，日志里注明不可信。
 *
 * **3. FBR/IOEx 是复核，不是前置条件。** 枚举阶段已经写过 fn0 的 FBR 与 IOEx，
 *    这里只确认目标值；写 FBR 的关键是 fn0，不是"先拧 IOEx"。
 */
static int AicWifiVendorPrepare(SDHCI_HOST *Host) {
    UINT8 IoEx = 0;

    AicMarkIoState(Host, "WiFi: vendor init before:");

    /* 1. IOEx：只保证 fn1 位，幂等；bit0 只读就只读，不纠缠 */
    if (SdhciIoRead8(Host, 0u, 0x02u, &IoEx) != 0) {
        SdhciMark("WiFi: vendor init: read IOEx failed\n");
        return -1;
    }
    if ((IoEx & 0x02u) == 0u) {
        if (AicWriteReg(Host, 0u, 0x02u, (UINT8)(IoEx | 0x02u), "ioex") != 0) {
            SdhciMark("WiFi: vendor init: fn1 enable failed\n");
            return -1;
        }
    } else {
        SdhciMarkHex("WiFi:  ioex already has fn1 bit=", IoEx, 2);
        SdhciMark(" (idempotent)\n");
    }
    /* 2. fn1 FBR 块大小 512：**fn0 + 绝对地址 0x110/0x111**，两次单字节写 */
    if (AicWriteReg(Host, 0u, AIC_FBR_BLKSIZE_L, 0x00u, "fbr110") != 0 ||
        AicWriteReg(Host, 0u, AIC_FBR_BLKSIZE_H, 0x02u, "fbr111") != 0) {
        SdhciMark("WiFi: vendor init: fn1 blocksize failed\n");
        /* 失败时把"写 FBR 前后的 CCCR 快照"补上，供判断是否有复位把枚举态清掉 */
        AicMarkIoState(Host, "WiFi: fbr write failed, state:");
        return -1;
    }
    AicMarkIoState(Host, "WiFi: after fbr   :");
    /* 3. fn0 0xF2 = 0x7F（厂商 aicsdio.c:1850-1856） */
    if (AicWriteReg(Host, 0u, 0xF2u, 0x7Fu, "f0.f2") != 0) {
        SdhciMark("WiFi: vendor init: fn0 0xF2 failed\n");
        return -1;
    }
    /* 4. fn1 0x07 = 0x01：允许字节模式（厂商 aicsdio.c:1869-1874） */
    if (AicWriteReg(Host, 1u, AIC_BYTEMODE_ENABLE_REG, 0x01u, "fn1.bme") != 0) {
        SdhciMark("WiFi: vendor init: fn1 0x07 failed\n");
        return -1;
    }
    AicMarkIoState(Host, "WiFi: vendor init after :");
    return 0;
}

/* 失败时把 CMD53 的四个参数摊开：blksz / blkcnt / tmode / pst */
static void AicMarkXferParams(SDHCI_HOST *Host, const char *Tag, UINT32 Bytes, UINT32 Blocks) {
    SdhciMark(Tag);
    SdhciMarkHex(" bytes=", Bytes, 4);
    SdhciMarkHex(" blocks=", Blocks, 2);
    SdhciMarkHex(" blksz=", SdhciRead16(Host, SDHCI_BLOCK_SIZE), 4);
    SdhciMarkHex(" blkcnt=", SdhciRead16(Host, SDHCI_BLOCK_COUNT), 4);
    SdhciMarkHex(" tmode=", SdhciRead16(Host, SDHCI_TRANSFER_MODE), 4);
    SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
    SdhciMark("\n");
}

/*
 * 往 FIFO 写一条消息。厂商用的是 sdio_writesb → 小于 512 B 走**字节模式**
 * （linux/mmc/core/sdio_io.c:352-360），所以首选字节模式。
 * 字节模式一失败就立刻改用**块模式**（补 0 到 512 的整数倍）再试一次，
 * 并把两次的参数都打出来 —— 这样"门没开"（两种都失败）和"写法不对"
 * （字节失败、块成功）一眼可分。
 */
/*
 * P2：**任何**走字节模式 CMD53 的路径，用之前都得确认 fn1 0x07 的允许位，
 * 不能只指望 vendor init 里那一次。返回 0 = 位已置（或刚置上），-1 = 置不上。
 */
static int AicEnsureByteMode(SDHCI_HOST *Host) {
    UINT8 Bm = 0;

    if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, &Bm) != 0) {
        return -1;
    }
    if ((Bm & 0x01u) != 0u) {
        return 0;
    }
    if (SdhciIoWrite8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, 0x01u) != 0) {
        SdhciMark("WiFi: byte-mode enable bit write failed\n");
        return -1;
    }
    (void)SdhciIoRead8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, &Bm);
    SdhciMarkHex("WiFi: byte-mode bit was 0, set -> ", Bm, 2);
    SdhciMark("\n");
    return ((Bm & 0x01u) != 0u) ? 0 : -1;
}

static int AicFifoWrite(SDHCI_HOST *Host, const UINT8 *Data, UINT32 Len) {
    UINT32 Done = 0;
    UINT32 Step;
    UINT32 Pad;
    UINT32 i;
    UINT8 Caps = 0;

    if (AicEnsureByteMode(Host) != 0) {
        SdhciMark("WiFi: msg fifo write: byte mode not available\n");
        /* 退回块模式前先看卡支不支持多块（CCCR 0x08 bit1 SMB） */
        if (SdhciIoRead8(Host, 0u, 0x08u, &Caps) == 0 && (Caps & 0x02u) == 0u) {
            SdhciMark("WiFi: card reports SMB=0 (no multi-block): cannot use block mode\n");
            return -1;
        }
    }
    while (Done < Len) {
        Step = Len - Done;
        if (Step > AIC_FIFO_CHUNK) {
            Step = AIC_FIFO_CHUNK;
        }
        if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, 0, Step,
                             (UINT8 *)(Data + Done)) == 0) {
            Done += Step;
            continue;
        }
        /* 字节模式失败 */
        AicMarkXferParams(Host, "WiFi: msg fifo byte-mode FAIL", Len - Done, 0u);
        (void)SdhciWifiBusRecover(Host);
        if (Len > AIC_FIFO_CHUNK) {
            SdhciMark("WiFi: msg too long for block retry\n");
            return -1;
        }
        /* 块模式重试：补 0 到 1 块 */
        for (i = Len; i < AIC_FIFO_CHUNK; i++) {
            gAicMsg[i] = 0;
        }
        Pad = AIC_FIFO_CHUNK;
        if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, 1, AIC_FIFO_CHUNK, gAicMsg) != 0) {
            AicMarkXferParams(Host, "WiFi: msg fifo block-mode FAIL", Pad, 1u);
            (void)SdhciWifiBusRecover(Host);
            SdhciMark("WiFi: both byte and block write failed (gate, not format)\n");
            return -1;
        }
        AicMarkXferParams(Host, "WiFi: msg fifo block-mode OK  ", Pad, 1u);
        return 0;
    }
    return 0;
}

/* 从 fn2 读：先 4 B 帧头，再按长度分块读正文 */
static int AicFifoRead(SDHCI_HOST *Host, UINT8 *Out, UINT32 Len) {
    UINT32 Done = 0;
    UINT32 Step;

    while (Done < Len) {
        Step = Len - Done;
        if (Step > AIC_FIFO_CHUNK) {
            Step = AIC_FIFO_CHUNK;
        }
        if (SdhciIoRwExtended(Host, 2u, AIC_RD_FIFO_REG, 0, 0, 0, Step, Out + Done) != 0) {
            SdhciMarkHex("WiFi: aic fifo read fail at ", Done, 4);
            SdhciMark("\n");
            (void)SdhciWifiBusRecover(Host);
            return -1;
        }
        Done += Step;
    }
    return 0;
}

static int AicSendMsg(SDHCI_HOST *Host, UINT16 Id, const void *Param, UINT16 ParamLen) {
    UINT32 Body = 8u + (UINT32)ParamLen; /* lmac_msg 头 + 参数 */
    UINT32 Total = Body + 8u;            /* 再加 4 B 帧头 + 4 B dummy */
    UINT32 i;

    if (Total > AIC_MSG_MAX) {
        return -1;
    }
    for (i = 0; i < Total; i++) {
        gAicMsg[i] = 0;
    }
    /* 帧头：len 字段 = 总长 - 4（aic_bsp_driver.c:262-266） */
    AicPut16(&gAicMsg[0], (UINT16)((Body + 4u) & 0x0FFFu));
    gAicMsg[2] = (UINT8)AIC_TYPE_CFG_CMD_RSP;
    gAicMsg[3] = AicCrc8(&gAicMsg[0], 3u);
    /* gAicMsg[4..7]：厂商的 dummy word，保持 0 */
    AicPut16(&gAicMsg[8], Id);
    AicPut16(&gAicMsg[10], (UINT16)AIC_TASK_DBG);
    AicPut16(&gAicMsg[12], (UINT16)AIC_DRV_TASK_ID);
    AicPut16(&gAicMsg[14], ParamLen);
    for (i = 0; i < (UINT32)ParamLen; i++) {
        gAicMsg[16u + i] = ((const UINT8 *)Param)[i];
    }
    return AicFifoWrite(Host, gAicMsg, Total);
}

/*
 * 芯片有没有消息 —— 照 aicwf_sdio_hal_irqhandler 的 D80 分支（aicsdio.c:1505-1545）：
 *   intmaskf2 = (fn1 0x04) | 0x08；
 *   intmaskf2 > 120          → 这是 fn2（消息）的事件
 *   intmaskf2 == 127         → 字节模式，长度 = fn1 0x05 × 4
 *   否则                     → 块模式，长度 = (status & 0x07) × 512
 * 注意 bit7 不是"有消息"的标志（0x77 就没有 bit7），照厂商这么算才对。
 */
static int AicWaitMsgPending(SDHCI_HOST *Host, UINT32 TimeoutMs, UINT32 *OutLen) {
    UINT32 Waited = 0;
    UINT8 Status = 0;
    UINT32 Masked;
    UINT8 Len = 0;

    while (Waited < TimeoutMs) {
        if (SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Status) != 0) {
            SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
            Waited += AIC_CFM_POLL_MS;
            continue;
        }
        Masked = (UINT32)Status | 0x08u;
        if (Masked > 120u) {
            if (Masked == 127u) {
                if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &Len) != 0) {
                    return -1;
                }
                *OutLen = (UINT32)Len * 4u;
            } else {
                *OutLen = (UINT32)(Status & 0x07u) * 512u;
            }
            if (*OutLen == 0u || *OutLen > 500u) {
                *OutLen = 4u; /* 兜底：先读帧头再决定 */
            }
            return 0;
        }
        SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
        Waited += AIC_CFM_POLL_MS;
    }
    return -1;
}

/*
 * 收一条确认。RX 帧同样带 4 B 帧头（hdr[2]&0x7f == 0x11 即 CFG_CMD_RSP，
 * 见 aicsdio_txrxif.c:211-215），正文 ipc_e2a_msg 在 +4：
 *   +4 id / +6 dest / +8 src / +10 param_len / +12 参数
 */
static int AicRecvMsg(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                      UINT32 *OutParamLen) {
    UINT32 Tries;
    UINT32 Len = 0;
    UINT32 ParamLen;
    UINT32 i;

    for (Tries = 0; Tries < 4u; Tries++) {
        if (AicWaitMsgPending(Host, AIC_CFM_TIMEOUT_MS, &Len) != 0) {
            SdhciMark("WiFi: aic rx: no pending message (fn1 0x04 / 0x05)\n");
            return -1;
        }
        if (Len < 8u) {
            Len = 4u;
        }
        if (AicFifoRead(Host, gAicRx, Len) != 0) {
            return -1;
        }
        SdhciMarkHex("WiFi: aic rx len=", Len, 4);
        SdhciMarkHex(" hdr=", (UINT32)(gAicRx[0] | (gAicRx[1] << 8) |
                                       ((UINT32)gAicRx[2] << 16) | ((UINT32)gAicRx[3] << 24)),
                     8);
        SdhciMark("\n");
        if ((gAicRx[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_CMD_RSP &&
            (gAicRx[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_DATA_CFM) {
            SdhciMarkHex("WiFi: aic rx skip type=", gAicRx[2], 2);
            SdhciMark("\n");
            continue;
        }
        if (AicGet16(&gAicRx[4]) != WantId) {
            SdhciMarkHex("WiFi: aic rx id=", AicGet16(&gAicRx[4]), 4);
            SdhciMarkHex(" want=", WantId, 4);
            SdhciMark("\n");
            continue;
        }
        ParamLen = AicGet16(&gAicRx[10]);
        if (ParamLen > MaxParam) {
            ParamLen = MaxParam;
        }
        if (ParamLen > (Len > 12u ? Len - 12u : 0u)) {
            ParamLen = (Len > 12u) ? Len - 12u : 0u;
        }
        if (OutParamLen != 0) {
            *OutParamLen = ParamLen;
        }
        if (Param != 0 && ParamLen > 0u) {
            for (i = 0; i < ParamLen; i++) {
                Param[i] = gAicRx[12u + i];
            }
        }
        return 0;
    }
    return -1;
}

/* DBG_MEM_READ_REQ：读一个 32 位字（顺带证明 fn2 消息通道通） */
static int AicMemRead(SDHCI_HOST *Host, UINT32 Address, UINT32 *Value) {
    UINT8 Param[4];
    UINT8 Cfm[8];
    UINT32 CfmLen = 0;

    AicPut16(&Param[0], (UINT16)(Address & 0xFFFFu));
    AicPut16(&Param[2], (UINT16)((Address >> 16) & 0xFFFFu));
    if (AicSendMsg(Host, AIC_DBG_MEM_READ_REQ, Param, 4u) != 0) {
        SdhciMark("WiFi: mem read send failed\n");
        return -1;
    }
    if (AicRecvMsg(Host, AIC_DBG_MEM_READ_CFM, Cfm, 8u, &CfmLen) != 0) {
        SdhciMark("WiFi: mem read cfm timeout\n");
        return -1;
    }
    /* dbg_mem_read_cfm { u32 memaddr; u32 memdata; }（aic_bsp_driver.h:276-280） */
    *Value = (UINT32)Cfm[4] | ((UINT32)Cfm[5] << 8) | ((UINT32)Cfm[6] << 16) |
             ((UINT32)Cfm[7] << 24);
    return 0;
}

/* DBG_MEM_BLOCK_WRITE_REQ：1 KB 一块（aic_bsp_driver.c:1256-1266） */
static int AicMemBlockWrite(SDHCI_HOST *Host, UINT32 Address, UINT32 Size, const UINT8 *Data) {
    UINT8 Param[8u + AIC_BLK_WRITE];
    UINT32 Done = 0;
    UINT32 Step;
    UINT32 i;

    while (Done < Size) {
        Step = Size - Done;
        if (Step > AIC_BLK_WRITE) {
            Step = AIC_BLK_WRITE;
        }
        AicPut16(&Param[0], (UINT16)((Address + Done) & 0xFFFFu));
        AicPut16(&Param[2], (UINT16)(((Address + Done) >> 16) & 0xFFFFu));
        AicPut16(&Param[4], (UINT16)(Step & 0xFFFFu));
        AicPut16(&Param[6], (UINT16)((Step >> 16) & 0xFFFFu));
        for (i = 0; i < Step; i++) {
            Param[8u + i] = Data[Done + i];
        }
        if (AicSendMsg(Host, AIC_DBG_MEM_BLOCK_WRITE_REQ, Param, (UINT16)(8u + Step)) != 0) {
            SdhciMarkHex("WiFi: blk write send fail addr=", Address + Done, 8);
            SdhciMark("\n");
            return -1;
        }
        if (AicRecvMsg(Host, AIC_DBG_MEM_BLOCK_WRITE_CFM, Param, 8u, &i) != 0) {
            SdhciMarkHex("WiFi: blk write cfm fail addr=", Address + Done, 8);
            SdhciMark("\n");
            return -1;
        }
        /* dbg_mem_block_write_cfm { u32 wstatus; } —— 非 0 即失败 */
        if ((Param[0] | Param[1] | Param[2] | Param[3]) != 0u) {
            SdhciMarkHex("WiFi: blk write status=", (UINT32)(Param[0] | (Param[1] << 8)), 4);
            SdhciMarkHex(" addr=", Address + Done, 8);
            SdhciMark("\n");
            return -1;
        }
        Done += Step;
    }
    return 0;
}

/* 写完读回校验：每 1 KB 块校验首尾两个字（全量读太慢，够抓地址/长度错误） */
static int AicVerifyBlock(SDHCI_HOST *Host, UINT32 Address, UINT32 Size, const UINT8 *Data) {
    UINT32 Off = 0;
    UINT32 Word;
    UINT32 Want;

    while (Off < Size) {
        Want = (UINT32)Data[Off] | ((UINT32)Data[Off + 1] << 8) | ((UINT32)Data[Off + 2] << 16) |
               ((UINT32)Data[Off + 3] << 24);
        if (AicMemRead(Host, Address + Off, &Word) != 0) {
            SdhciMarkHex("WiFi: verify read fail at +", Off, 4);
            SdhciMark("\n");
            return -1;
        }
        if (Word != Want) {
            SdhciMarkHex("WiFi: verify mismatch at +", Off, 4);
            SdhciMarkHex(" got=", Word, 8);
            SdhciMarkHex(" want=", Want, 8);
            SdhciMark("\n");
            return -1;
        }
        Off += AIC_BLK_WRITE;
    }
    return 0;
}

/* A 方案：从卡上分块读固件（FileSystemReadFileAt，不嵌内核） */
static int AicFwReadFile(const char *Path, UINT8 *Buffer, UINT32 MaxSize, UINT32 *OutSize) {
    FAT_FILE_STAT Stat;
    UINT32 Done = 0;
    UINTN Got = 0;
    int Err;

    if (FileSystemFileStat(Path, &Stat) != FAT_OK) {
        SdhciMark("WiFi: fw missing ");
        SdhciMark(Path);
        SdhciMark("\n");
        return -1;
    }
    if (Stat.Size == 0u || Stat.Size > MaxSize) {
        SdhciMarkDecimal("WiFi: fw size out of range ", Stat.Size);
        SdhciMark(" ");
        SdhciMark(Path);
        SdhciMark("\n");
        return -1;
    }
    while (Done < Stat.Size) {
        Err = FileSystemReadFileAt(Path, Done, Buffer + Done, Stat.Size - Done, &Got);
        if (Err != FAT_OK || Got == 0u) {
            SdhciMarkHex("WiFi: fw read fail at +", Done, 4);
            SdhciMark(" ");
            SdhciMark(Path);
            SdhciMark("\n");
            return -1;
        }
        Done += (UINT32)Got;
    }
    if (Done != Stat.Size) {
        SdhciMarkHex("WiFi: fw size mismatch read=", Done, 4);
        SdhciMarkHex(" stat=", Stat.Size, 4);
        SdhciMark("\n");
        return -1;
    }
    SdhciMark("WiFi: fw read ");
    SdhciMark(Path);
    SdhciMarkDecimal(" ", Done);
    SdhciMark("B ok\n");
    *OutSize = Done;
    return 0;
}

/*
 * 两个小文件一次读进同一块缓冲：adid 放开头，patch_table 放 2 KB 偏移处
 * （U02 的 adid 1708 B、patch_table 1048 B，加起来不到 3 KB）。
 * 主固件 fmacfw（331 KB）不在这里读，S5b-3 用 4 KB 分块边读边送。
 */
static int WifiFwLoadBoth(UINT32 *AdidSize, UINT32 *PatchSize) {
    if (AicFwReadFile(AIC_FW_ADID, gAicFwBuf, AIC_BLK_WRITE * 2u, AdidSize) != 0) {
        return -1;
    }
    return AicFwReadFile(AIC_FW_PATCH_TABLE, gAicFwBuf + AIC_BLK_WRITE * 2u,
                         AIC_BLK_WRITE * 2u, PatchSize);
}

/* 只读：读芯片版本（0x40500000），同时证明 fn2 消息通道可用 */
static int WifiFwChipRev(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 Value = 0;

    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: fw needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    /* 厂商下载前固定动作（f0 0xF2 / fn1 使能 / 块大小 / 字节模式位），每次进来都做一遍：
       幂等，且桌面/真机都能看到"做之前/做之后"两组寄存器值 */
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: vendor init failed\n");
        return -1;
    }
    if (AicMemRead(Host, AIC_D80_CHIP_REV_ADDR, &Value) != 0) {
        SdhciMark("WiFi: chip rev read failed (msg channel not up)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: chip rev raw=", Value, 8);
    SdhciMarkHex(" rev=", (Value >> 16) & 0xFFu, 2);
    SdhciMark("\n");
    return 0;
}

int SdhciWifiFw(UINT32 Mode) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 AdidSize = 0;
    UINT32 PatchSize = 0;

    SdhciMark("WiFi: fw stage start\n");

    if ((Mode & SDHCI_WIFI_FW_READ) != 0u) {
        if (WifiFwLoadBoth(&AdidSize, &PatchSize) != 0) {
            return -1;
        }
        SdhciMarkDecimal("WiFi: fw sizes adid=", AdidSize);
        SdhciMarkDecimal(" patch_table=", PatchSize);
        SdhciMark("\n");
    }

    if ((Mode & SDHCI_WIFI_FW_ID) != 0u) {
        if (WifiFwChipRev() != 0) {
            return -1;
        }
    }

    if ((Mode & SDHCI_WIFI_FW_WRITE) != 0u) {
        if (Host == 0 || !SdhciWifiReady()) {
            SdhciMark("WiFi: fw needs a probed card (run wifiprobe first)\n");
            return -1;
        }
        /* 身份硬闸：不是 c8a1:0082 不许写（厂商 aicwf_sdio_chipmatch 的同一张表） */
        if (!SdhciWifiChipIsD80()) {
            SdhciMark("WiFi: chip is not aic8800d80, refuse to write firmware\n");
            return -1;
        }
        /* 写之前先证明消息通道通（读一次版本号），不通就不写 */
        if (WifiFwChipRev() != 0) {
            SdhciMark("WiFi: no msg channel, refuse to write firmware\n");
            return -1;
        }
        if (WifiFwLoadBoth(&AdidSize, &PatchSize) != 0) {
            return -1;
        }
        /* adid：先写后校验 */
        if (AicMemBlockWrite(Host, AIC_D80_U02_ADID_ADDR, AdidSize, gAicFwBuf) != 0 ||
            AicVerifyBlock(Host, AIC_D80_U02_ADID_ADDR, AdidSize, gAicFwBuf) != 0) {
            SdhciMark("WiFi: adid FAIL\n");
            return -1;
        }
        SdhciMark("WiFi: adid ok\n");
        if (AicMemBlockWrite(Host, AIC_D80_U02_PATCH_ADDR, PatchSize,
                             gAicFwBuf + AIC_BLK_WRITE * 2u) != 0 ||
            AicVerifyBlock(Host, AIC_D80_U02_PATCH_ADDR, PatchSize,
                           gAicFwBuf + AIC_BLK_WRITE * 2u) != 0) {
            SdhciMark("WiFi: patch_table FAIL\n");
            return -1;
        }
        SdhciMark("WiFi: patch_table ok\n");
    }

    SdhciMark("WiFi: fw stage end\n");
    return 0;
}
