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
 *   - 落点**一律取 patch 表里的 addr_adid / addr_patch**（aic_bsp_driver.c:1417-1418
 *     用表值覆盖硬编码值）；D80 u02 表里是 adid=0x00201940、patch=0x001e0000。
 *     硬编码 0x0020b43c 只作"表里缺字段"的兜底，且必须打 (fallback, table missing)
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
#define AIC_DBG_START_APP_REQ AIC_MSGID(AIC_TASK_DBG, 13u)  /* 0x40D */
#define AIC_DBG_START_APP_CFM AIC_MSGID(AIC_TASK_DBG, 14u)  /* 0x40E */

#define AIC_TYPE_CFG_CMD_RSP 0x11u
#define AIC_TYPE_CFG_DATA_CFM 0x12u
#define AIC_TYPE_CFG_PRINT 0x13u
#define AIC_MISC_OTHER_INTR 0x80u /* fn1 0x04 bit7：芯片有消息给主机 */

#define AIC_RD_FIFO_REG 0x0Fu  /* V3：fn2 读 FIFO */
#define AIC_WR_FIFO_REG 0x10u  /* V3：fn1 写 FIFO */
#define AIC_MISC_INT_REG 0x04u /* V3：fn1 MISC_INT_STATUS */
#define AIC_INTR_PENDING_REG 0x01u /* V3：fn1 INTR_PENDING */
#define AIC_MISC_CTRL_REG 0x08u    /* V3：fn1 MISC_CTRL */
#define AIC_FLOW_CTRL_Q2_REG 0x09u /* V3：fn1 FLOW_CTRL_Q2 */

/* 帧长上限：16 B 头 + 参数；FIFO 一块 512 B，写时分块 */
#define AIC_MSG_MAX 1600u
#define AIC_FIFO_CHUNK 512u
/* 厂商 aicwf_sdio.h:28/51-53：流控队列、tx 缓冲、尾补齐 */
#define AIC_FLOW_CTRL_REG_V3 0x03u
#define AIC_TX_BUFFER_SIZE 1536u
#define AIC_FC_RETRY_COUNT 50u
#define AIC_BLK_WRITE 1024u /* 厂商一块 1 KB */
#define AIC_CFM_TIMEOUT_MS 500u
#define AIC_CFM_POLL_MS 1u

/* D80 u02 的固件落点（aic_bsp_driver.h:358-359） */
#define AIC_D80_U02_ADID_ADDR 0x00201940u
/* 兜底值，正常路径不用它：表里的 addr_patch=0x001E0000 才是真的（W3） */
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
#define AIC_FW_PATCH AIC_FW_DIR "fw_patch.bin"
#define AIC_FW_PATCH_TABLE AIC_FW_DIR "fw_patch_table.bin"
#define AIC_FW_FMAC AIC_FW_DIR "fmacfw.bin"

/* 下载落点（aic_bsp_driver.h:345、358-359） */
#define AIC_RAM_FMAC_FW_ADDR 0x00120000u
#define AIC_START_APP_AUTO 1u /* aic_bsp_driver.h:246 */

/* patch 表（fw_patch_table.bin）的格式与类型，照 aic_bsp_driver.h:361、:425-433 */
#define AICBT_PT_TAG "AICBT_PT_TAG" /* 16 B，后补 0 */
#define AICBT_PT_INF 0x00u          /* 地址信息元组 */
#define AICBT_PT_TRAP 0x01u
#define AICBT_PT_B4 0x02u
#define AICBT_PT_BTMODE 0x03u
#define AICBT_PT_PWRON 0x04u
#define AICBT_PT_AF 0x05u
#define AICBT_PT_VER 0x06u
#define AICBT_PT_NAME 16u
#define AICBT_PT_HDR 16u

/* 分块大小：**不一次性把 331 KB 读进内核堆**，按 8 KB 复用同一缓冲 */
#define AIC_FW_CHUNK 8192u
#define AIC_FW_STAGE_MAX 4u

static UINT8 gAicMsg[AIC_MSG_MAX];
static UINT8 gAicRx[512];
static UINT8 gAicFwBuf[AIC_FW_CHUNK];

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
 * 发消息前的流控（厂商 aicwf_sdio_flow_ctrl_msg，aicwf_sdio.c:101-139）：
 * 读 fn1 0x03（V3 的 FLOW_CTRL_Q1）拿到可用 buffer 数，为 0 就按厂商节奏重试
 * （<30 次 200 µs、<40 次 2 ms、其后 10 ms，共 50 轮），仍为 0 才失败。
 * 没有 credit 就发送 = 芯片 FIFO 没准备好，写进去也是白写。
 */
static int AicFlowCtrl(SDHCI_HOST *Host, UINT32 *OutFc) {
    UINT32 Count = 0;
    UINT8 Fc = 0;

    for (;;) {
        if (SdhciIoRead8(Host, 1u, AIC_FLOW_CTRL_REG_V3, &Fc) != 0) {
            /* 读失败与"读到 0"必须分开：一个是 CMD52 不通，一个是芯片没给 credit */
            AicMarkR5(Host->LastStatus);
            SdhciMark("WiFi: tx fc read failed (cmd52 fn1 0x03)\n");
            return -1;
        }
        if (Fc != 0u) {
            *OutFc = Fc;
            return 0;
        }
        if (Count >= AIC_FC_RETRY_COUNT) {
            *OutFc = 0;
            return -1;
        }
        Count++;
        if (Count < 30u) {
            SdhciDelayMicroseconds(200);
        } else if (Count < 40u) {
            SdhciDelayMilliseconds(2);
        } else {
            SdhciDelayMilliseconds(10);
        }
    }
}

/*
 * 一条消息的线上长度必须补齐到 512 的整数倍（厂商 aicwf_sdio.c:1230-1237）：
 * 先按 4 B 对齐，再补 TAIL_LEN(4) 个 0 并向上取整到 512 —— 不足一个块时整块补零。
 * 补齐后用**块模式**写（不掺字节模式），块数 = 补齐长度 / 512。
 */
static int AicFifoWriteMsg(SDHCI_HOST *Host, UINT8 *Buf, UINT32 *InOutLen) {
    UINT32 Len = *InOutLen;
    UINT32 Padded;
    UINT32 Blocks;
    UINT32 Fc = 0;
    UINT32 i;

    /* 4 B 对齐 */
    if ((Len % 4u) != 0u) {
        UINT32 Aligned = (Len + 3u) & ~3u;
        for (i = Len; i < Aligned; i++) {
            Buf[i] = 0;
        }
        Len = Aligned;
    }
    /* 先算补齐长度并检查上限，再落笔填 0 —— 免得算出来超界了还在写缓冲 */
    if ((Len % AIC_FIFO_CHUNK) != 0u) {
        Padded = ((Len + 4u) / AIC_FIFO_CHUNK + 1u) * AIC_FIFO_CHUNK;
    } else {
        Padded = Len;
    }
    if (Padded > AIC_MSG_MAX) {
        SdhciMark("WiFi: tx msg too long for buffer\n");
        return -1;
    }
    for (i = Len; i < Padded; i++) {
        Buf[i] = 0;
    }
    Blocks = Padded / AIC_FIFO_CHUNK;

    if (AicFlowCtrl(Host, &Fc) != 0) {
        SdhciMark("WiFi: tx msg fc retry fail: fc=");
        SdhciMarkHex("", Fc, 2);
        SdhciMarkHex(" len=", Padded, 4);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkHex("WiFi: tx fc=", Fc, 2);
    SdhciMarkHex(" padded=", Padded, 4);
    SdhciMarkHex(" blocks=", Blocks, 2);
    SdhciMark("\n");
    /* 厂商的判据：len < fc * 1536 才发 */
    if (Padded >= Fc * AIC_TX_BUFFER_SIZE) {
        SdhciMarkHex("WiFi: tx msg fc retry fail: fc=", Fc, 2);
        SdhciMarkHex(" len=", Padded, 4);
        SdhciMark("\n");
        return -1;
    }
    if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, (UINT16)Blocks, AIC_FIFO_CHUNK, Buf) !=
        0) {
        AicMarkXferParams(Host, "WiFi: tx block write FAIL", Padded, Blocks);
        (void)SdhciWifiBusRecover(Host);
        /* 卡若 SMB=0 就不吃多块；按同样长度退回字节模式再试一次，
           两次参数都打出来 —— "块失败/字节成功"说明是模式问题而不是门没开 */
        if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, 0, (UINT16)Padded, Buf) != 0) {
            AicMarkXferParams(Host, "WiFi: tx byte write FAIL", Padded, 0u);
            (void)SdhciWifiBusRecover(Host);
            return -1;
        }
        AicMarkXferParams(Host, "WiFi: tx byte write OK (block refused)", Padded, 0u);
    }
    *InOutLen = Padded;
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
    {
        UINT32 Wire = Total;
        if (AicFifoWriteMsg(Host, gAicMsg, &Wire) != 0) {
            SdhciMarkHex("WiFi: aic fifo write fail at ", Total, 4);
            SdhciMark("\n");
            return -1;
        }
    }
    return 0;
}

/*
 * 从 fn2 0x0F 收一条消息（厂商消息通道走 func_msg = fn2，aicsdio.c:1530-1545）。
 * 字节模式读取同样要求 fn1 0x07 的允许位已置，所以读之前确认一次。
 */
static int AicFifoRead(SDHCI_HOST *Host, UINT32 Fn, UINT8 *Data, UINT32 Len) {
    UINT8 Bm = 0;

    if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, &Bm) == 0 && (Bm & 0x01u) == 0u) {
        (void)SdhciIoWrite8(Host, 1u, AIC_BYTEMODE_ENABLE_REG, 0x01u);
    }
    if (SdhciIoRwExtended(Host, Fn, AIC_RD_FIFO_REG, 0, 0, 0, (UINT16)Len, Data) != 0) {
        AicMarkXferParams(Host, "WiFi: rx read FAIL", Len, 0u);
        return -1;
    }
    return 0;
}

/*
 * 芯片有没有消息 —— **严格**照 aicwf_sdio_hal_irqhandler 的 D80 分支
 * （aicsdio.c:1505-1545），三分支一个不漏：
 *
 *   intmaskf2 = status | 0x08
 *   intmaskf2 > 120  → **fn2**（msg 通道）事件：==127 字节模式 len=blen*4，
 *                      否则块模式 len=(status & 0x07)*512
 *   其余             → **fn1**（data 通道）事件：==0x78 字节模式 len=blen*4，
 *                      否则块模式 len=(status & 0x7F)*512
 *   status == 0      → 没消息，继续等
 *
 * 真机 10:13 的 `status=01` 就落在 **fn1 块模式 1 块 = 512 B** 这一支：
 * 芯片早就回了 512 B 在等我们读，是旧判据（只认 >120）把它判成"没消息"。
 */
static int AicWaitMsgPending(SDHCI_HOST *Host, UINT32 TimeoutMs, UINT32 *OutFn, UINT32 *OutLen) {
    UINT32 Waited = 0;
    UINT8 Status = 0;
    UINT32 Masked;
    UINT8 BmLen = 0;
    const char *Mode = "block";

    while (Waited < TimeoutMs) {
        if (SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Status) != 0) {
            SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
            Waited += AIC_CFM_POLL_MS;
            continue;
        }
        if (Status == 0u) {
            SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
            Waited += AIC_CFM_POLL_MS;
            continue;
        }
        Masked = (UINT32)Status | 0x08u;
        Mode = "block";
        if (Masked > 120u) {
            *OutFn = 2u;
            if (Masked == 127u) {
                if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &BmLen) != 0) {
                    return -1;
                }
                *OutLen = (UINT32)BmLen * 4u;
                Mode = "byte";
            } else {
                *OutLen = (UINT32)(Status & 0x07u) * 512u;
            }
        } else {
            *OutFn = 1u;
            if (Status == 0x78u) {
                if (SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &BmLen) != 0) {
                    return -1;
                }
                *OutLen = (UINT32)BmLen * 4u;
                Mode = "byte";
            } else {
                *OutLen = (UINT32)(Status & 0x7Fu) * 512u;
            }
        }
        SdhciMarkHex("WiFi: rx masked=", Masked, 2);
        SdhciMarkHex(" status=", Status, 2);
        SdhciMarkHex(" fn=", *OutFn, 2);
        SdhciMarkHex(" len=", *OutLen, 4);
        SdhciMark(" mode=");
        SdhciMark(Mode);
        SdhciMark("\n");
        if (*OutLen == 0u) {
            *OutLen = 512u; /* 长度算成 0 就按一块读，宁可多读也不空转 */
        }
        return 0;
    }
    {
        UINT8 Pend = 0;
        UINT8 Ctrl = 0;
        UINT8 Blen = 0;
        UINT8 IoEx = 0;
        (void)SdhciIoRead8(Host, 1u, AIC_INTR_PENDING_REG, &Pend);
        (void)SdhciIoRead8(Host, 1u, AIC_MISC_CTRL_REG, &Ctrl);
        (void)SdhciIoRead8(Host, 1u, AIC_BYTEMODE_LEN_REG, &Blen);
        (void)SdhciIoRead8(Host, 0u, 0x02u, &IoEx);
        SdhciMarkHex("WiFi: rx poll timeout status=", Status, 2);
        SdhciMarkHex(" pend=", Pend, 2);
        SdhciMarkHex(" ctrl=", Ctrl, 2);
        SdhciMarkHex(" blen=", Blen, 2);
        SdhciMarkHex(" ioex=", IoEx, 2);
        SdhciMark("\n");
    }
    return -1;
}

/* 前 32 字节 hexdump：和厂商 0x400 请求逐字节比对手里就靠它 */
static void AicDumpBytes(const UINT8 *Data, UINT32 Len) {
    UINT32 i;

    if (Len > 32u) {
        Len = 32u;
    }
    SdhciMark("WiFi: rx dump");
    for (i = 0; i < Len; i++) {
        SdhciMarkHex(" ", Data[i], 2);
    }
    SdhciMark("\n");
}

/*
 * 收一条确认。RX 帧带 4 B 帧头（hdr[2]&0x7f == 0x11 CFG_CMD_RSP / 0x12 CFG_DATA_CFM，
 * aicsdio_txrxif.c:211-215），正文 ipc_e2a_msg 在 **+4**：
 *   +4 id / +6 dest / +8 src / +10 param_len / +12 参数
 */
static int AicRecvMsg(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                      UINT32 *OutParamLen) {
    UINT32 Tries;
    UINT32 Len = 0;
    UINT32 Fn = 0;
    UINT32 ParamLen;
    UINT32 i;

    for (Tries = 0; Tries < 4u; Tries++) {
        if (AicWaitMsgPending(Host, AIC_CFM_TIMEOUT_MS, &Fn, &Len) != 0) {
            SdhciMark("WiFi: aic rx: no pending message (fn1 0x04)\n");
            return -1;
        }
        if (Len > 512u) {
            SdhciMarkHex("WiFi: rx len>512, read first block only, len=", Len, 4);
            SdhciMark("\n");
            Len = 512u;
        }
        if (AicFifoRead(Host, Fn, gAicRx, Len) != 0) {
            return -1;
        }
        SdhciMarkHex("WiFi: rx len=", Len, 4);
        SdhciMarkHex(" hdr=", (UINT32)(gAicRx[0] | (gAicRx[1] << 8) |
                                       ((UINT32)gAicRx[2] << 16) | ((UINT32)gAicRx[3] << 24)),
                     8);
        SdhciMarkHex(" type=", (UINT32)(gAicRx[2] & 0x7Fu), 2);
        SdhciMarkHex(" id=", AicGet16(&gAicRx[4]), 4);
        SdhciMarkHex(" plen=", AicGet16(&gAicRx[10]), 4);
        SdhciMark("\n");
        AicDumpBytes(gAicRx, Len);
        if ((gAicRx[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_CMD_RSP &&
            (gAicRx[2] & 0x7Fu) != (UINT8)AIC_TYPE_CFG_DATA_CFM) {
            SdhciMarkHex("WiFi: rx skip type=", gAicRx[2], 2);
            SdhciMark("\n");
            continue;
        }
        if (AicGet16(&gAicRx[4]) != WantId) {
            SdhciMarkHex("WiFi: rx id=", AicGet16(&gAicRx[4]), 4);
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

/* ---------- S5b-2 / S5b-3：固件下载 ---------- */

/*
 * 阶段表（顺序照 aic8800_bsp/aic_bsp_main.c:23-33 的 fw_u02 表 +
 * aic_bsp_driver.c:1417-1428 的上传顺序 + :1774-1790 的启动顺序）：
 *
 *   adid    → FW_RAM_ADID_BASE_ADDR_8800D80_U02
 *   patch   → FW_RAM_PATCH_BASE_ADDR_8800D80_U02   （bt_patch = fw_patch.bin）
 *   fmacfw  → RAM_FMAC_FW_ADDR = 0x00120000
 *   start   → DBG_START_APP_REQ{bootaddr, boottype=1}
 *
 * 注意：厂商**不**把 fw_patch_table.bin 原样上传 —— 它是主机侧数据，由
 * `aicbt_patch_table_alloc()` 解析、再由 `aicwifi_patch_config_8800d80()`
 * 把表项写进 RAM（aic_bsp_driver.c:1517-1530、aic8800d80_compat.c:114+）。
 * 那一步本版**未实现**，写盘/启动都成功但固件起不来的话，下一个门就是它。
 */
typedef struct {
    const char *Name;
    const char *Path;
    UINT32 Addr;
    UINT32 ExpectSize;
    int Required;
} AIC_FW_STAGE;

/*
 * 落点：adid/patch 两个地址**运行期从 patch 表里取**（见 AicPatchTableParse），
 * 这里的 0x0020B43C 只是"表里没有该字段"时的兜底，用兜底会打 `(fallback, ...)`。
 * 厂商也是这个顺序：先给硬编码默认值，再用表值覆盖（aic_bsp_driver.c:1417-1418）。
 */
static AIC_FW_STAGE gAicStages[AIC_FW_STAGE_MAX] = {
    {"adid", AIC_FW_ADID, AIC_D80_U02_ADID_ADDR, 1708u, 1},
    {"patch", AIC_FW_PATCH, AIC_D80_U02_PATCH_ADDR, 30040u, 1}, /* D80 强制必需 */
    {"fmacfw", AIC_FW_FMAC, AIC_RAM_FMAC_FW_ADDR, 331348u, 1},
    {"start", 0, AIC_RAM_FMAC_FW_ADDR, 0u, 1},
};

/* patch 表里的地址信息元组（aic_bsp_driver.h:541-551 的 aicbt_patch_info_t） */
typedef struct {
    UINT32 InfoLen;
    UINT32 AdidAddrInfo;
    UINT32 AddrAdid;
    UINT32 PatchAddrInfo;
    UINT32 AddrPatch;
    UINT32 ResetAddr;
    UINT32 ResetVal;
    UINT32 AdidFlagAddr;
    UINT32 AdidFlag;
} AIC_PATCH_INFO;

/* 失败态：0=未跑 1=全部成功 2=失败（失败时 gWifiFwStageIdx 指向停在哪一阶段） */
static UINT32 gWifiFwState;
/*
 * patch 管线是否已实现并应用（patch_table 的 INF/TRAP/PWRON/PATCH_TAF 46 条写 +
 * patch_config_8800d80 的四个运行时读）。**管线落地前恒为 0**，confirm 一律拒绝。
 * volatile：故意让编译器不能把它折成常量，两条分支都必须留在二进制里可审查。
 */
static volatile UINT32 gAicPatchReady;
/* 本次运行是否真的把 patch 应用过（start_app 的前置条件） */
static UINT32 gAicPatchApplied;
static UINT32 gWifiFwStageIdx;
static UINT32 gWifiFwSent;

static void AicFwFail(const char *Stage, UINT32 Sent, const char *What) {
    gWifiFwState = 2u;
    SdhciMark("WiFi: stage=");
    SdhciMark(Stage);
    SdhciMark(" FAIL ");
    SdhciMark(What);
    SdhciMarkHex(" sent=", Sent, 8);
    SdhciMark("\n");
}

/* 这两个定义在本段之后（分块读/阶段检查），先声明，避免顺序依赖 */
static int AicFwReadChunk(const char *Path, UINT32 Offset, UINT8 *Buffer, UINT32 Want,
                          UINT32 *OutGot);

/* 表里那个 16 B 名字是不是 Want（允许后面补 0） */
static int AicPatchNameIs(const UINT8 *Name, const char *Want) {
    UINT32 i;

    for (i = 0; i < AICBT_PT_NAME; i++) {
        char C = Want[i];
        if (C == 0) {
            return (Name[i] == 0u) ? 1 : 0;
        }
        if ((char)Name[i] != C) {
            return 0;
        }
    }
    return 1;
}

/*
 * 解析 patch 表（厂商 aicbt_patch_table_alloc，aic_bsp_driver.c:1295-1352）：
 *   [0..15]   "AICBT_PT_TAG"
 *   循环： [16] name / [16..19] type / [20..23] len(以 8 字节为单位) / data[len*8]
 *
 * 只做两件事：验 tag、把类型 0（AICBT_PT_INF）的地址信息取出来。
 * tag 不对就明确失败 —— 绝不"猜着用"。
 */
static int AicPatchTableParse(const UINT8 *Buf, UINT32 Len, AIC_PATCH_INFO *Info) {
    UINT32 Off = AICBT_PT_HDR;
    UINT32 Type;
    UINT32 EntryLen;
    UINT32 i;

    for (i = 0; i < (UINT32)sizeof(*Info); i++) {
        ((UINT8 *)Info)[i] = 0;
    }
    if (Buf == 0 || Len < AICBT_PT_HDR + 24u) {
        SdhciMarkHex("WiFi: pt too short len=", Len, 4);
        SdhciMark("\n");
        return -1;
    }
    /* 厂商 memcmp 的是 sizeof(AICBT_PT_TAG)=13（含结尾 0），这里按"前缀匹配 16 B"更严 */
    for (i = 0; i < 12u; i++) {
        if (Buf[i] != (UINT8)AICBT_PT_TAG[i]) {
            SdhciMark("WiFi: pt tag BAD (want AICBT_PT_TAG)\n");
            return -1;
        }
    }
    while (Off + 24u <= Len) {
        Type = (UINT32)Buf[Off + 16u] | ((UINT32)Buf[Off + 17u] << 8) |
               ((UINT32)Buf[Off + 18u] << 16) | ((UINT32)Buf[Off + 19u] << 24);
        EntryLen = (UINT32)Buf[Off + 20u] | ((UINT32)Buf[Off + 21u] << 8) |
                   ((UINT32)Buf[Off + 22u] << 16) | ((UINT32)Buf[Off + 23u] << 24);
        if (Type >= 1000u) {
            EntryLen = 0; /* 厂商的临时兼容：类型 >=1000 时长度按 0 处理 */
        }
        if (Type == AICBT_PT_INF) {
            if (AicPatchNameIs(&Buf[Off], "AICBT_PINF_T") == 0) {
                SdhciMark("WiFi: pt info tuple name BAD (want AICBT_PINF_T)\n");
                return -1;
            }
            Info->InfoLen = EntryLen;
            if (EntryLen == 0u) {
                SdhciMark("WiFi: pt info_len=0 (D80 requires != 0)\n");
                return -1;
            }
            if (Off + 24u + EntryLen * 8u > Len) {
                SdhciMarkHex("WiFi: pt info overruns, len=", EntryLen, 4);
                SdhciMark("\n");
                return -1;
            }
            {
                const UINT8 *D = &Buf[Off + 24u];
                UINT32 W[8];
                UINT32 k;
                UINT32 MaxW = EntryLen * 2u;
                if (MaxW > 8u) {
                    MaxW = 8u;
                }
                for (k = 0; k < MaxW; k++) {
                    W[k] = (UINT32)D[k * 4u] | ((UINT32)D[k * 4u + 1u] << 8) |
                           ((UINT32)D[k * 4u + 2u] << 16) | ((UINT32)D[k * 4u + 3u] << 24);
                }
                for (k = MaxW; k < 8u; k++) {
                    W[k] = 0;
                }
                /* 顺序照 aicbt_patch_info_unpack：从 adid_addrinf 起连续拷进去 */
                Info->AdidAddrInfo = W[0];
                Info->AddrAdid = W[1];
                Info->PatchAddrInfo = W[2];
                Info->AddrPatch = W[3];
                Info->ResetAddr = W[4];
                Info->ResetVal = W[5];
                Info->AdidFlagAddr = W[6];
                Info->AdidFlag = W[7];
            }
            return 0;
        }
        Off += 24u + EntryLen * 8u;
    }
    SdhciMark("WiFi: pt no info tuple found\n");
    return -1;
}

/*
 * B2 要做的事先摆在这里：把表里 INF/TRAP/PWRON/PATCH_TAF 的条目写进 RAM，
 * 再跑 patch_config_8800d80。现在只解析 + 报数，然后返回"未实现"。
 */
#define AIC_PATCH_NOT_IMPLEMENTED (-2)

static int AicPatchApply(SDHCI_HOST *Host, const UINT8 *Table, UINT32 Len) {
    AIC_PATCH_INFO Info;
    UINT32 Off = AICBT_PT_HDR;
    UINT32 Type;
    UINT32 EntryLen;
    UINT32 Traps = 0;
    UINT32 Pwrons = 0;
    UINT32 Pairs = 0;

    (void)Host;
    if (AicPatchTableParse(Table, Len, &Info) != 0) {
        return -1;
    }
    /* 数一遍表里有多少条要写（B2 的输入规模，顺便证明解析走通了） */
    while (Off + 24u <= Len) {
        Type = (UINT32)Table[Off + 16u] | ((UINT32)Table[Off + 17u] << 8) |
               ((UINT32)Table[Off + 18u] << 16) | ((UINT32)Table[Off + 19u] << 24);
        EntryLen = (UINT32)Table[Off + 20u] | ((UINT32)Table[Off + 21u] << 8) |
                   ((UINT32)Table[Off + 22u] << 16) | ((UINT32)Table[Off + 23u] << 24);
        if (Type >= 1000u) {
            EntryLen = 0;
        }
        if (Type == AICBT_PT_TRAP) {
            Traps++;
            Pairs += EntryLen * 2u;
        } else if (Type == AICBT_PT_PWRON) {
            Pwrons++;
            Pairs += EntryLen * 2u;
        }
        Off += 24u + EntryLen * 8u;
    }
    SdhciMarkHex("WiFi: patch apply: trap=", Traps, 4);
    SdhciMarkHex(" pwron=", Pwrons, 4);
    SdhciMarkHex(" words=", Pairs, 4);
    SdhciMark(" entries, NOT IMPLEMENTED (B2)\n");
    return AIC_PATCH_NOT_IMPLEMENTED;
}

/* 读 patch 表、解析、把 adid/patch 落点改成表里的值；任何一步不过就失败 */
static int AicFwLoadPatchTable(SDHCI_HOST *Host, UINT8 *Buf, UINT32 BufSize, UINT32 *OutLen) {
    FAT_FILE_STAT Stat;
    UINT32 Got = 0;
    AIC_PATCH_INFO Info;
    int Rc;

    (void)Host;
    if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) != FAT_OK) {
        SdhciMark("WiFi: fw table missing " AIC_FW_PATCH_TABLE "\n");
        return -1;
    }
    if (Stat.Size == 0u || Stat.Size > BufSize) {
        SdhciMarkDecimal("WiFi: fw table size out of range ", Stat.Size);
        SdhciMark("\n");
        return -1;
    }
    if (AicFwReadChunk(AIC_FW_PATCH_TABLE, 0u, Buf, Stat.Size, &Got) != 0 || Got != Stat.Size) {
        SdhciMark("WiFi: fw table read failed\n");
        return -1;
    }
    Rc = AicPatchTableParse(Buf, Got, &Info);
    if (Rc != 0) {
        SdhciMark("WiFi: fw table parse FAILED (refuse to guess)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: fw table: tag ok info_len=", Info.InfoLen, 4);
    SdhciMarkHex(" adid=", Info.AddrAdid, 8);
    SdhciMarkHex(" patch=", Info.AddrPatch, 8);
    SdhciMarkHex(" base=", Info.AdidAddrInfo, 8);
    SdhciMark("\n");
    /* B2 要用的两个按键：reset 与 adid_flag（评审 P2-2 的判据 0x40500150==1 就在这里） */
    SdhciMarkHex("WiFi: fw table: reset=", Info.ResetAddr, 8);
    SdhciMarkHex(":", Info.ResetVal, 8);
    SdhciMarkHex(" adid_flag=", Info.AdidFlagAddr, 8);
    SdhciMarkHex(":", Info.AdidFlag, 8);
    SdhciMark("\n");
    /* 表值优先；表里没有（0）才用兜底，并且明说是兜底 */
    if (Info.AddrAdid != 0u) {
        gAicStages[0].Addr = Info.AddrAdid;
    } else {
        SdhciMarkHex("WiFi: fw table: adid addr missing, fallback=", gAicStages[0].Addr, 8);
        SdhciMark(" (fallback, table missing)\n");
    }
    if (Info.AddrPatch != 0u) {
        gAicStages[1].Addr = Info.AddrPatch;
    } else {
        SdhciMarkHex("WiFi: fw table: patch addr missing, fallback=", gAicStages[1].Addr, 8);
        SdhciMark(" (fallback, table missing)\n");
    }
    *OutLen = Got;
    return 0;
}

/*
 * 先算后写：每个文件先 stat、比对预期长度，任何不一致立刻停，
 * 绝不允许"写到一半才发现文件不对"。
 */
static int AicFwCheckStage(AIC_FW_STAGE *Stage, UINT32 *OutSize) {
    FAT_FILE_STAT Stat;

    *OutSize = 0;
    if (Stage->Path == 0) {
        return 0; /* start 阶段没有文件 */
    }
    if (FileSystemFileStat(Stage->Path, &Stat) != FAT_OK) {
        if (Stage->Required != 0) {
            SdhciMark("WiFi:  ");
            SdhciMark(Stage->Name);
            SdhciMark(" missing ");
            SdhciMark(Stage->Path);
            SdhciMark("\n");
            return -1;
        }
        return 1; /* 可选阶段缺失：跳过 */
    }
    if (Stage->ExpectSize != 0u && Stat.Size != Stage->ExpectSize) {
        SdhciMark("WiFi:  ");
        SdhciMark(Stage->Name);
        SdhciMarkDecimal(" size mismatch have=", Stat.Size);
        SdhciMarkDecimal(" want=", Stage->ExpectSize);
        SdhciMark("\n");
        return -1;
    }
    if (Stat.Size == 0u) {
        SdhciMark("WiFi:  ");
        SdhciMark(Stage->Name);
        SdhciMark(" empty file\n");
        return -1;
    }
    *OutSize = Stat.Size;
    return 0;
}

/* 把一段（≤8 KB）从卡上读进复用缓冲 */
static int AicFwReadChunk(const char *Path, UINT32 Offset, UINT8 *Buffer, UINT32 Want,
                          UINT32 *OutGot) {
    UINTN Got = 0;
    int Err;

    Err = FileSystemReadFileAt(Path, Offset, Buffer, Want, &Got);
    if (Err != FAT_OK) {
        SdhciMarkHex("WiFi: fw read err at +", Offset, 8);
        SdhciMarkDecimal(" err=", (UINT32)Err);
        SdhciMark("\n");
        return -1;
    }
    if (Got == 0u) {
        SdhciMarkHex("WiFi: fw read short at +", Offset, 8);
        SdhciMark("\n");
        return -1;
    }
    *OutGot = (UINT32)Got;
    return 0;
}

/*
 * 一个阶段的落盘：8 KB 分块读 → 每 1 KB 一块 dbg_mem_block_write（带 CFM 确认）
 * → 每块读回首字做抽样校验。块数、进度、失败点全打出来。
 */
static int AicFwPushStage(SDHCI_HOST *Host, AIC_FW_STAGE *Stage, UINT32 Size) {
    UINT32 Off = 0;
    UINT32 Got = 0;
    UINT32 InChunk;
    UINT32 BlkIdx = 0;
    UINT32 TotalBlk = (Size + AIC_BLK_WRITE - 1u) / AIC_BLK_WRITE;
    UINT32 Word;
    UINT32 Want;

    SdhciMark("WiFi: stage=");
    SdhciMark(Stage->Name);
    SdhciMarkHex(" len=", Size, 4);
    SdhciMarkHex(" addr=", Stage->Addr, 8);
    SdhciMarkHex(" blocks=", TotalBlk, 4);
    SdhciMark("\n");
    while (Off < Size) {
        InChunk = Size - Off;
        if (InChunk > AIC_FW_CHUNK) {
            InChunk = AIC_FW_CHUNK;
        }
        if (AicFwReadChunk(Stage->Path, Off, gAicFwBuf, InChunk, &Got) != 0) {
            AicFwFail(Stage->Name, gWifiFwSent, "read");
            return -1;
        }
        if (AicMemBlockWrite(Host, Stage->Addr + Off, Got, gAicFwBuf) != 0) {
            AicFwFail(Stage->Name, gWifiFwSent, "write");
            return -1;
        }
        /* 抽样校验：这块的首字读回来对一下（每 8 KB 一次，够抓地址/长度错） */
        Want = (UINT32)gAicFwBuf[0] | ((UINT32)gAicFwBuf[1] << 8) |
               ((UINT32)gAicFwBuf[2] << 16) | ((UINT32)gAicFwBuf[3] << 24);
        if (AicMemRead(Host, Stage->Addr + Off, &Word) != 0) {
            AicFwFail(Stage->Name, gWifiFwSent, "verify-read");
            return -1;
        }
        if (Word != Want) {
            SdhciMarkHex("WiFi: verify got=", Word, 8);
            SdhciMarkHex(" want=", Want, 8);
            SdhciMark("\n");
            AicFwFail(Stage->Name, gWifiFwSent, "verify-mismatch");
            return -1;
        }
        Off += Got;
        gWifiFwSent = Off;
        BlkIdx = (Off + AIC_BLK_WRITE - 1u) / AIC_BLK_WRITE;
        if ((BlkIdx % 32u) == 0u || Off == Size) {
            SdhciMark("WiFi: stage=");
            SdhciMark(Stage->Name);
            SdhciMarkHex(" blk=", BlkIdx, 4);
            SdhciMarkHex("/", TotalBlk, 4);
            SdhciMark(" ok\n");
        }
    }
    SdhciMark("WiFi: stage=");
    SdhciMark(Stage->Name);
    SdhciMarkDecimal(" len=", Size);
    SdhciMarkDecimal(" sent=", gWifiFwSent);
    SdhciMark(" ok\n");
    return 0;
}

/* DBG_START_APP_REQ：让固件从 RAM 起跑（aic_bsp_driver.c:1936-1946） */
static int AicFwStartApp(SDHCI_HOST *Host, UINT32 BootAddr) {
    UINT8 Param[8];
    UINT8 Cfm[8];
    UINT32 CfmLen = 0;

    /*
     * 评审 W4：start_app 之前必须确认 patch 已应用，否则就是"固件写进去但起不来"。
     * patch 管线（patch_table 的 46 条写 + patch_config_8800d80）未实现，这里恒为 0。
     */
    if (gAicPatchApplied == 0) {
        SdhciMark("WiFi: start_app refused: patch not applied (review W4)\n");
        AicFwFail("start", 0u, "patch-not-applied");
        return -1;
    }

    AicPut16(&Param[0], (UINT16)(BootAddr & 0xFFFFu));
    AicPut16(&Param[2], (UINT16)((BootAddr >> 16) & 0xFFFFu));
    AicPut16(&Param[4], (UINT16)AIC_START_APP_AUTO);
    AicPut16(&Param[6], 0u);
    if (AicSendMsg(Host, AIC_DBG_START_APP_REQ, Param, 8u) != 0) {
        AicFwFail("start", 0u, "send");
        return -1;
    }
    if (AicRecvMsg(Host, AIC_DBG_START_APP_CFM, Cfm, 8u, &CfmLen) != 0) {
        AicFwFail("start", 0u, "cfm-timeout");
        return -1;
    }
    /* dbg_start_app_cfm { u32 bootstatus; } */
    SdhciMark("WiFi: stage=start");
    SdhciMarkHex(" bootaddr=", BootAddr, 8);
    SdhciMarkHex(" bootstatus=", (UINT32)(Cfm[0] | (Cfm[1] << 8) | (Cfm[2] << 16) | (Cfm[3] << 24)),
                 8);
    SdhciMark("\n");
    return 0;
}

/* 起跑后等第一条消息：只读、有上限，看不到也不算失败（可能只走 fn2/中断） */
static void AicFwWaitBootMsg(SDHCI_HOST *Host) {
    UINT32 Fn = 0;
    UINT32 Len = 0;

    if (AicWaitMsgPending(Host, 2000u, &Fn, &Len) != 0) {
        SdhciMark("WiFi: boot poll: no msg in 2000ms\n");
        return;
    }
    SdhciMarkHex("WiFi: boot poll fn=", Fn, 2);
    SdhciMarkHex(" len=", Len, 4);
    SdhciMark("\n");
    if (Len > 0u && Len <= 512u) {
        if (AicFifoRead(Host, Fn, gAicRx, Len) == 0) {
            SdhciMarkHex("WiFi: boot msg type=", (UINT32)(gAicRx[2] & 0x7Fu), 2);
            SdhciMarkHex(" id=", AicGet16(&gAicRx[4]), 4);
            SdhciMark("\n");
            AicDumpBytes(gAicRx, Len);
        }
    }
}

/*
 * 检查模式（评审 P0-3）：**纯读**。
 * 不写任何寄存器（不跑 vendor init）、不发任何消息（不读 chip rev），
 * 只 stat 文件 + 读缓存里的身份结论，把"将要写什么"打全。
 * 通道是否可用由 `wififw id` 单独证明，本命令不再替它"顺手探一下"。
 */
static int WifiFwCheck(SDHCI_HOST *Host) {
    UINT32 i;
    UINT32 Size = 0;
    int Rc;

    UINT32 TableLen = 0;

    (void)Host;
    SdhciMarkDecimal("WiFi: fw check: buf=", AIC_FW_CHUNK);
    SdhciMark(" blk=1024 dir=" AIC_FW_DIR "\n");
    SdhciMark("WiFi: fw check: read-only (files only, no register write, no msg)\n");
    /*
     * 只读文件：把 patch 表读进来解析，好让下面 stage 行显示**表里的真实落点**。
     * 这一步不碰芯片（不发消息、不写寄存器），符合 P0-3 的"纯读检查"。
     */
    if (AicFwLoadPatchTable(Host, gAicFwBuf, AIC_FW_CHUNK, &TableLen) != 0) {
        /* 表不可用就直接停：不许用兜底地址去"猜"落点（评审 W3 的原话） */
        SdhciMark("WiFi: fw check FAILED: patch table unusable (refuse to guess addr)\n");
        return -1;
    }
    SdhciMark("WiFi: fw check: id=");
    SdhciMark(SdhciWifiChipIsD80() != 0 ? "aic8800d80 (c8a1:0082)\n"
                                        : "unknown (wififw write will refuse)\n");
    for (i = 0; i < AIC_FW_STAGE_MAX; i++) {
        AIC_FW_STAGE *Stage = &gAicStages[i];
        SdhciMarkDecimal("WiFi:  stage ", i + 1u);
        SdhciMark("/");
        SdhciMarkDecimal("", AIC_FW_STAGE_MAX);
        SdhciMark(" ");
        SdhciMark(Stage->Name);
        if (Stage->Path == 0) {
            SdhciMarkHex(" bootaddr=", Stage->Addr, 8);
            SdhciMark(" (msg DBG_START_APP_REQ)\n");
            continue;
        }
        SdhciMark(" file=");
        SdhciMark(Stage->Path);
        Rc = AicFwCheckStage(Stage, &Size);
        if (Rc == 1) {
            SdhciMark(" (optional, missing -> skip)\n");
            continue;
        }
        if (Rc != 0) {
            SdhciMark(" (WOULD FAIL)\n");
            return -1;
        }
        SdhciMarkDecimal(" size=", Size);
        SdhciMarkHex(" -> ", Stage->Addr, 8);
        SdhciMark(Stage->Required != 0 ? " (required)\n" : " (optional)\n");
    }
    SdhciMark("WiFi: fw check done, NOTHING WRITTEN (no reg write, no msg)\n");
    SdhciMark("WiFi: fw check: channel not probed here, run 'wififw id' for that\n");
    return 0;
}

/* 真写：检查 → 逐阶段写 → 启动 → 等首条消息，任一步失败即停 */
static int WifiFwWrite(SDHCI_HOST *Host) {
    UINT32 i;
    UINT32 Size = 0;
    int Rc;

    gWifiFwState = 0u;
    gWifiFwStageIdx = 0u;
    gWifiFwSent = 0u;
    SdhciMarkDecimal("WiFi: fw write CONFIRM buf=", AIC_FW_CHUNK);
    SdhciMark("\n");

    /* patch 表是必需件（D80 强制 info_len != 0）：先解析它，落点全部以表为准 */
    {
        UINT32 TableLen = 0;
        if (AicFwLoadPatchTable(Host, gAicFwBuf, AIC_FW_CHUNK, &TableLen) != 0) {
            SdhciMark("WiFi: fw precheck failed (patch table), nothing written\n");
            gWifiFwState = 2u;
            return -1;
        }
    }
    /* 先算后写：所有必填文件先过一遍尺寸，任何一条不对就一步都不写 */
    for (i = 0; i < AIC_FW_STAGE_MAX; i++) {
        Rc = AicFwCheckStage(&gAicStages[i], &Size);
        if (Rc < 0) {
            gWifiFwState = 2u;
            gWifiFwStageIdx = i;
            SdhciMark("WiFi: fw precheck failed, nothing written\n");
            return -1;
        }
    }
    for (i = 0; i < AIC_FW_STAGE_MAX; i++) {
        AIC_FW_STAGE *Stage = &gAicStages[i];
        gWifiFwStageIdx = i;
        gWifiFwSent = 0u;
        Rc = AicFwCheckStage(Stage, &Size);
        if (Rc == 1) {
            SdhciMark("WiFi: stage=");
            SdhciMark(Stage->Name);
            SdhciMark(" skipped (optional, no file)\n");
            continue;
        }
        if (Rc != 0) {
            return -1;
        }
        if (Stage->Path == 0) {
            /*
             * B2 的接口位置：patch 必须在 start_app 之前应用。
             * 现在 AicPatchApply 返回"未实现"，所以这里必然失败并停下 ——
             * 这正是我们要的：patch 没做就不许起跑。
             */
            {
                UINT32 TableLen = 0;
                FAT_FILE_STAT Stat;
                if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) != FAT_OK ||
                    Stat.Size == 0u || Stat.Size > AIC_FW_CHUNK ||
                    AicFwReadChunk(AIC_FW_PATCH_TABLE, 0u, gAicFwBuf, Stat.Size, &TableLen) != 0) {
                    AicFwFail("patch", 0u, "table-read");
                    return -1;
                }
                if (AicPatchApply(Host, gAicFwBuf, TableLen) != 0) {
                    AicFwFail("patch", 0u, "apply-not-implemented");
                    return -1;
                }
                gAicPatchApplied = 1u;
            }
            if (AicFwStartApp(Host, Stage->Addr) != 0) {
                return -1;
            }
            AicFwWaitBootMsg(Host);
            continue;
        }
        if (AicFwPushStage(Host, Stage, Size) != 0) {
            return -1;
        }
    }
    gWifiFwState = 1u;
    SdhciMark("WiFi: fw state=done (all stages ok)\n");
    return 0;
}

/*
 * 评审 W10：这个命令以前叫 `read` 却只做 stat，名不副实。
 * 现在叫 `stat`，并且输出里明说"仅 stat、不读内容"；
 * 真的按字节读 + 比对留给 `write` 的分块读路径（那里每个 chunk 都有抽样校验）。
 */
static int WifiFwStatCheck(void) {
    FAT_FILE_STAT Stat;
    int Missing = 0;

    SdhciMark("WiFi: fw stat only (no content read)\n");
    if (FileSystemFileStat(AIC_FW_ADID, &Stat) == FAT_OK) {
        SdhciMarkDecimal("WiFi:  fw_adid.bin ", Stat.Size);
        SdhciMark("B ok\n");
    } else {
        SdhciMark("WiFi:  fw_adid.bin MISSING\n");
        Missing = 1;
    }
    if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) == FAT_OK) {
        SdhciMarkDecimal("WiFi:  fw_patch_table.bin ", Stat.Size);
        SdhciMark("B ok\n");
    } else {
        SdhciMark("WiFi:  fw_patch_table.bin MISSING\n");
        Missing = 1;
    }
    if (FileSystemFileStat(AIC_FW_FMAC, &Stat) == FAT_OK) {
        SdhciMarkDecimal("WiFi:  fmacfw.bin ", Stat.Size);
        SdhciMark("B ok\n");
    } else {
        SdhciMark("WiFi:  fmacfw.bin MISSING (write confirm needs it)\n");
        Missing = 1;
    }
    return (Missing != 0) ? -1 : 0;
}

/* 只读：读芯片版本（0x40500000），同时证明消息通道可用 */
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
    int WantWrite = (Mode & (SDHCI_WIFI_FW_WRITE | SDHCI_WIFI_FW_CONFIRM)) != 0u;

    SdhciMark("WiFi: fw stage start\n");

    if ((Mode & SDHCI_WIFI_FW_STAT) != 0u) {
        if (WifiFwStatCheck() != 0) {
            return -1;
        }
    }

    if ((Mode & SDHCI_WIFI_FW_ID) != 0u) {
        if (WifiFwChipRev() != 0) {
            return -1;
        }
    }

    if (WantWrite != 0) {
        if (Host == 0 || !SdhciWifiReady()) {
            SdhciMark("WiFi: fw needs a probed card (run wifiprobe first)\n");
            return -1;
        }
        /* 身份硬闸：不是 c8a1:0082 不许写（厂商 aicwf_sdio_chipmatch 的同一张表） */
        if (!SdhciWifiChipIsD80()) {
            SdhciMark("WiFi: chip is not aic8800d80, refuse to write firmware\n");
            return -1;
        }
        if ((Mode & SDHCI_WIFI_FW_CONFIRM) == 0u) {
            /* 检查模式：纯读，不写寄存器、不发消息（评审 P0-3） */
            if (WifiFwCheck(Host) != 0) {
                return -1;
            }
            SdhciMark("WiFi: fw stage end (checked only)\n");
            return 0;
        }
        /*
         * ---- 硬闸（评审 W3/W4 + 用户指令）----
         * patch 管线（patch_table 的 INF/TRAP/PWRON/PATCH_TAF 写 + patch_config_8800d80）
         * 还没实现，此时写 adid/fmacfw 再 start_app 一定是"看起来绿、芯片起不来"。
         * 在管线落地之前，confirm 一律拒绝，绝不允许打出 done。
         */
        if (gAicPatchReady == 0u) {
            SdhciMark("WiFi: write refused: patch apply not implemented (B2)\n");
            SdhciMark("WiFi: need: 46 writes (INF/TRAP/PWRON/PATCH_TAF) + patch_config\n");
            SdhciMark("WiFi: see Archive/Duo-WiFi-Review.md W3/W4\n");
            SdhciMark("WiFi: nothing written, nothing started\n");
            gWifiFwState = 2u;
            SdhciMark("WiFi: fw stage end (refused)\n");
            return -1;
        }
        if (WifiFwWrite(Host) != 0) {
            SdhciMarkDecimal("WiFi: fw failed at stage ", gWifiFwStageIdx + 1u);
            SdhciMark(", sent=");
            SdhciMarkDecimal("", gWifiFwSent);
            SdhciMark(" of current stage\n");
            SdhciMark("WiFi: fw stage end (failed)\n");
            return -1;
        }
    }

    SdhciMark("WiFi: fw stage end\n");
    return 0;
}
