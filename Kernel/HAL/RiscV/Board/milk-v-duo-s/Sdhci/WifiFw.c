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
#define AIC_TASK_MM 0u
#define AIC_TASK_DBG 1u
/* MM_VERSION_REQ = LMAC_FIRST_MSG(TASK_MM)+4，CFM 紧随其后（lmac_msg.h:136-150） */
#define AIC_MM_RESET_REQ 0x0000u
#define AIC_MM_RESET_CFM 0x0001u
#define AIC_MM_VERSION_REQ 0x0004u
#define AIC_MM_VERSION_CFM 0x0005u
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
/*
 * e2a 消息的"有效性戳"（aic_bsp_driver.h:62-71 struct ipc_e2a_msg 的 pattern 字段）。
 * 厂商拿它判断一个 MSG buffer 里是不是真有一条消息；我们也拿它当硬判据，
 * 比 `hdr[2]&0x7f == 0x11` 强得多。
 *
 * 帧布局（实测 20 B 一帧，plen=4）：
 *   +0  4 B 块头： len(12bit)=总长-4 | type | crc8
 *   +4  4 B e2a：  id(u16) | dummy_dest(u16)
 *   +8  4 B e2a：  dummy_src(u16) | param_len(u16)
 *   +12 4 B       pattern = 0xaddede2a
 *   +16 params    ← 以前从 +12 拷，整体偏移 4 B，把 pattern 当成了 param[0]
 * 即 总长 = 16 + param_len，块头 len 字段 = 12 + param_len。
 */
#define AIC_E2A_PATTERN 0xADDEDE2Au
#define AIC_E2A_PARAM_OFF 16u
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
/* fmacfw 长度：越界检查用（patch 写绝不允许落到这片主固件区） */
#define AIC_D80_FMAC_SIZE 331348u
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

/* DBG_MEM_WRITE_REQ/CFM：单字32 位写（aic_bsp_driver.h:187-188，REQ=0x402 CFM=0x403） */
#define AIC_DBG_MEM_WRITE_REQ AIC_MSGID(AIC_TASK_DBG, 2u)
#define AIC_DBG_MEM_WRITE_CFM AIC_MSGID(AIC_TASK_DBG, 3u)

/* 分块大小：**不一次性把 331 KB 读进内核堆**，按 8 KB 复用同一缓冲 */
#define AIC_FW_CHUNK 8192u
#define AIC_FW_STAGE_MAX 5u

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

/* 固定 dest=TASK_DBG 的常用写法（先用后定义，这里先声明） */
static int AicSendMsg(SDHCI_HOST *Host, UINT16 Id, const void *Param, UINT16 ParamLen);

static int AicSendMsgEx(SDHCI_HOST *Host, UINT16 Id, UINT16 DestId, const void *Param,
                        UINT16 ParamLen) {
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
    AicPut16(&gAicMsg[10], DestId);
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
static int AicRecvMsgTimeout(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                             UINT32 *OutParamLen, UINT32 Tries, UINT32 TimeoutMs) {
    UINT32 Len = 0;
    UINT32 Fn = 0;
    UINT32 ParamLen;
    UINT32 i;
    UINT32 T;

    for (T = 0; T < Tries; T++) {
        if (AicWaitMsgPending(Host, TimeoutMs, &Fn, &Len) != 0) {
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
        SdhciMarkHex(" pattern=",
                     (UINT32)(gAicRx[12] | (gAicRx[13] << 8) | ((UINT32)gAicRx[14] << 16) |
                              ((UINT32)gAicRx[15] << 24)),
                     8);
        SdhciMark("\n");
        AicDumpBytes(gAicRx, Len);
        /* 硬判据：不是 e2a 有效性戳就丢帧（评审：比 type 判断可靠） */
        {
            UINT32 Pat = (UINT32)(gAicRx[12] | (gAicRx[13] << 8) | ((UINT32)gAicRx[14] << 16) |
                                  ((UINT32)gAicRx[15] << 24));
            if (Pat != AIC_E2A_PATTERN) {
                SdhciMarkHex("WiFi: rx bad pattern=", Pat, 8);
                SdhciMark(" (want addede2a) drop\n");
                continue;
            }
        }
        SdhciMark("WiFi: rx pattern=addede2a ok\n");
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
        /* params 从 +16 起：可拷长度 = Len - 16（不是 Len - 12） */
        if (Len < AIC_E2A_PARAM_OFF) {
            SdhciMarkHex("WiFi: rx frame too short len=", Len, 4);
            SdhciMark("\n");
            continue;
        }
        if (ParamLen > Len - AIC_E2A_PARAM_OFF) {
            ParamLen = Len - AIC_E2A_PARAM_OFF;
        }
        if (OutParamLen != 0) {
            *OutParamLen = ParamLen;
        }
        if (Param != 0 && ParamLen > 0u) {
            for (i = 0; i < ParamLen; i++) {
                Param[i] = gAicRx[AIC_E2A_PARAM_OFF + i];
            }
        }
        return 0;
    }
    return -1;
}

/* 常用写法：4 次 × 500 ms = 2 s 预算 */
static int AicRecvMsg(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                      UINT32 *OutParamLen) {
    return AicRecvMsgTimeout(Host, WantId, Param, MaxParam, OutParamLen, 4u, AIC_CFM_TIMEOUT_MS);
}

/* DBG_MEM_READ_REQ：读一个 32 位字（顺带证明 fn2 消息通道通） */
static int AicMemRead(SDHCI_HOST *Host, UINT32 Address, UINT32 *Value) {
    UINT8 Param[4];
    UINT8 Cfm[8];
    UINT32 CfmLen = 0;
    UINT32 k;

    /* 评审 P1-1：Cfm 必须先清零。短 CFM 时 AicRecvMsg 会把长度 clamp 到 Len-12，
       以前会 return 0 并把栈残留当 memdata —— 直接影响 config_base/str_base/version。 */
    for (k = 0; k < sizeof(Cfm); k++) {
        Cfm[k] = 0;
    }
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
    if (CfmLen != 8u) {
        SdhciMarkHex("WiFi: mem read cfm short len=", CfmLen, 4);
        SdhciMark("\n");
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
        /* dbg_mem_block_write_cfm { u32 wstatus; }：wstatus == 0 才算写成功 */
        {
            UINT32 WStatus = (UINT32)Param[0] | ((UINT32)Param[1] << 8) |
                             ((UINT32)Param[2] << 16) | ((UINT32)Param[3] << 24);
            if (WStatus != 0u) {
                SdhciMarkHex("WiFi: blk write wstatus=", WStatus, 8);
                SdhciMarkHex(" addr=", Address + Done, 8);
                SdhciMark(" FAIL\n");
                return -1;
            }
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
/* 阶段类型：文件 / patch 表落地 / start_app */
#define AIC_STAGE_FILE 0u
#define AIC_STAGE_PATCHTBL 1u
#define AIC_STAGE_START 2u

typedef struct {
    const char *Name;
    const char *Path;
    UINT32 Addr;
    UINT32 ExpectSize;
    int Required;
    UINT8 Kind;
} AIC_FW_STAGE;

/*
 * 落点：adid/patch 两个地址**运行期从 patch 表里取**（见 AicPatchTableParse），
 * 这里的 0x0020B43C 只是"表里没有该字段"时的兜底，用兜底会打 `(fallback, ...)`。
 * 厂商也是这个顺序：先给硬编码默认值，再用表值覆盖（aic_bsp_driver.c:1417-1418）。
 */
/*
 * 评审 P0-1：patch 表落地不再是独立命令，而是 write confirm 的第 4 个阶段。
 * 顺序：adid → patch → fmacfw → patchtbl(100 写 + cfg) → start_app。
 */
static AIC_FW_STAGE gAicStages[AIC_FW_STAGE_MAX] = {
    {"adid", AIC_FW_ADID, AIC_D80_U02_ADID_ADDR, 1708u, 1, AIC_STAGE_FILE},
    {"patch", AIC_FW_PATCH, AIC_D80_U02_PATCH_ADDR, 30040u, 1, AIC_STAGE_FILE},
    {"fmacfw", AIC_FW_FMAC, AIC_RAM_FMAC_FW_ADDR, 331348u, 1, AIC_STAGE_FILE},
    {"patchtbl", 0, 0u, 0u, 1, AIC_STAGE_PATCHTBL},
    {"start", 0, AIC_RAM_FMAC_FW_ADDR, 0u, 1, AIC_STAGE_START},
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
 * patch 管线是否已实现并应用（patch_table 全量 100 条写 + patch_config_8800d80 的
 * 4 读 10 写）。0 = 未开闸，confirm 一律拒绝；1 = 已开闸，允许走 5 阶段真写。
 *
 * 2026-10-30 开闸：真机 dry 三条判据全过（101 items / sentinels=6 / self-check ok，
 * 见提交 e21645c6），B2 独立审查的 P0-1..P0-3、P1-1..P1-6 已全部修完，
 * 用户明确批准执行首次真写。开闸只此一次，改动须留痕：
 *   以前 = 恒 0（拒），现在 = 1（允许）。要收回就把 1 改回 0 并说明原因。
 * volatile：故意让编译器不能把它折成常量，两条分支都必须留在二进制里可审查。
 */
static volatile UINT32 gAicPatchReady = 1u;
/* 本次运行是否真的把 patch 应用过（start_app 的前置条件） */
/* 评审 P1-4：三态，避免"失败后重放"和"掉电后仍以为已应用" */
#define AIC_PATCH_NONE 0u
#define AIC_PATCH_DONE 1u
#define AIC_PATCH_POISONED 2u
static UINT32 gAicPatchState;

/* 评审 P1-4：probe 成功后允许重新应用（掉电/重插卡后芯片里的东西已经不在了） */
void SdhciWifiPatchStateReset(void) {
    gAicPatchState = AIC_PATCH_NONE;
}
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
    UINT32 Found = 0;

    for (i = 0; i < (UINT32)sizeof(*Info); i++) {
        ((UINT8 *)Info)[i] = 0;
    }
    if (Buf == 0 || Len < AICBT_PT_HDR + 24u) {
        SdhciMarkHex("WiFi: pt too short len=", Len, 4);
        SdhciMark("\n");
        return -1;
    }
    /*
     * 评审 P0-3：厂商比的是 sizeof(AICBT_PT_TAG) = 13（12 字符 + 结尾 0），
     * 以前这里只比 12 B，比厂商更松。现在统一成 13 B。
     */
    for (i = 0; i < sizeof(AICBT_PT_TAG); i++) {
        if (Buf[i] != (UINT8)AICBT_PT_TAG[i]) {
            SdhciMark("WiFi: pt tag BAD (want AICBT_PT_TAG + NUL, 13B)\n");
            return -1;
        }
    }
    while (Off < Len) {
        /* 尾部 < 24 B 不许静默丢弃（厂商 aic_bsp_driver.c:1314 是 < size 继续） */
        if (Len - Off < 24u) {
            SdhciMarkHex("WiFi: pt trailing bytes ", Len - Off, 4);
            SdhciMark(" (tail < 24) -> BAD\n");
            return -1;
        }
        Type = (UINT32)Buf[Off + 16u] | ((UINT32)Buf[Off + 17u] << 8) |
               ((UINT32)Buf[Off + 18u] << 16) | ((UINT32)Buf[Off + 19u] << 24);
        EntryLen = (UINT32)Buf[Off + 20u] | ((UINT32)Buf[Off + 21u] << 8) |
                   ((UINT32)Buf[Off + 22u] << 16) | ((UINT32)Buf[Off + 23u] << 24);
        if (Type >= 1000u) {
            EntryLen = 0; /* 厂商的临时兼容：类型 >=1000 时长度按 0 处理 */
        }
        /*
         * 评审 P0-3：EntryLen 来自文件，`Off + 24 + EntryLen*8` 在 32 位下会回绕
         * （EntryLen >= 0x20000000 时乘积为 0），随后按 8 B 步长读出缓冲区外，
         * 拿到"对齐的野地址"。必须**先除后乘**判断，不能先乘再比。
         */
        if (EntryLen > (Len - Off - 24u) / 8u) {
            SdhciMarkHex("WiFi: pt entry overruns, len=", EntryLen, 4);
            SdhciMarkHex(" at off=", Off, 8);
            SdhciMark("\n");
            return -1;
        }
        if (Type == AICBT_PT_INF) {
            if (Found != 0) {
                SdhciMark("WiFi: pt duplicate INF record\n");
                return -1;
            }
            if (AicPatchNameIs(&Buf[Off], "AICBT_PINF_T") == 0) {
                SdhciMark("WiFi: pt info tuple name BAD (want AICBT_PINF_T)\n");
                return -1;
            }
            Info->InfoLen = EntryLen;
            if (EntryLen == 0u) {
                SdhciMark("WiFi: pt info_len=0 (D80 requires != 0)\n");
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
            /*
             * 评审 P0-3：不在这里 return —— 必须把整张表走完，
             * 否则尾部/结构损伤会被"读完 INF 就走"掩盖。
             */
            Found = 1;
        }
        Off += 24u + EntryLen * 8u;
    }
    if (Found == 0) {
        SdhciMark("WiFi: pt no info tuple found\n");
        return -1;
    }
    if (Off != Len) {
        SdhciMarkHex("WiFi: pt walk ended at ", Off, 8);
        SdhciMarkHex(" but file len=", Len, 8);
        SdhciMark("\n");
        return -1;
    }
    return 0;
}

/* ---------- B2：patch 表落地（实解析 100 条 + patch_config 14 op） ---------- */

/* 单字写：dbg_mem_write_req { u32 memaddr; u32 memdata; }（aic_bsp_driver.h:264-270） */
static int AicFwLoadPatchTable(SDHCI_HOST *Host, UINT8 *Buf, UINT32 BufSize, UINT32 *OutLen);

static int AicMemWrite32(SDHCI_HOST *Host, UINT32 Address, UINT32 Value) {
    UINT8 Param[8];
    UINT8 Cfm[8];
    UINT32 CfmLen = 0;

    AicPut16(&Param[0], (UINT16)(Address & 0xFFFFu));
    AicPut16(&Param[2], (UINT16)((Address >> 16) & 0xFFFFu));
    AicPut16(&Param[4], (UINT16)(Value & 0xFFFFu));
    AicPut16(&Param[6], (UINT16)((Value >> 16) & 0xFFFFu));
    if (AicSendMsg(Host, AIC_DBG_MEM_WRITE_REQ, Param, 8u) != 0) {
        return -1;
    }
    if (AicRecvMsg(Host, AIC_DBG_MEM_WRITE_CFM, Cfm, 8u, &CfmLen) != 0) {
        return -1;
    }
    /*
     * 评审 P1-1：厂商传 NULL 不校验回显，于是两种假绿会发生——
     *   ① 某条超时后，迟到的 CFM 被下一条请求按同一 id 命中，之后整体错位一拍；
     *   ② 块写失败退回字节模式重发，同一请求两条 CFM，同样错位。
     * 所以这里必须校验"回显长度 + 回显地址"，不匹配就是协议失步。
     */
    if (CfmLen < 8u) {
        SdhciMarkHex("WiFi: mem write cfm short len=", CfmLen, 4);
        SdhciMark("\n");
        return -1;
    }
    {
        UINT32 Echo = (UINT32)Cfm[0] | ((UINT32)Cfm[1] << 8) | ((UINT32)Cfm[2] << 16) |
                      ((UINT32)Cfm[3] << 24);
        if (Echo != Address) {
            SdhciMarkHex("WiFi: mem write cfm echo=", Echo, 8);
            SdhciMarkHex(" want=", Address, 8);
            SdhciMark(" (protocol desync)\n");
            return -1;
        }
    }
    return 0;
}

/* 计划表：把"要写哪些字"先全部算出来（先算后写） */
#define AIC_PLAN_MAX 160
#define AIC_PLAN_MEM 0u   /* 普通写 */
#define AIC_PLAN_CFG 1u   /* 厂商会用运行时配置覆盖的写（BTMODE） */
#define AIC_PLAN_DELAY 2u /* PWRON 之后的 udelay(500) */

typedef struct {
    UINT8 Kind;  /* mem / cfg */
    UINT8 Type;  /* AICBT_PT_* */
    UINT32 Addr;
    UINT32 Val;  /* 实际要写的值（MODE 已被厂商运行时值覆盖） */
    UINT32 Tbl;  /* 表里原本的值，只用于对账打印 */
} AIC_PLAN_ITEM;

static AIC_PLAN_ITEM gAicPlan[AIC_PLAN_MAX];
/* MODE 那 9 个 pair 的最终值（pair 下标 0..8），自检用 */
static UINT32 gAicModeVal[9];

/* D80 的 BT 默认值（aic_bsp_driver.h:505-517；CONFIG_SDIO_BT=n → btport=UART） */
#define AIC_BT_HWINFO 0xFFFFFFFFu /* AICBSP_HWINFO_DEFAULT (-1)：未知 */
#define AIC_BT_CPMODE 0u          /* AICBSP_CPMODE_WORK */
#define AIC_BT_BTMODE 5u          /* AICBT_BTMODE_BT_ONLY_COANT */
#define AIC_BT_BTPORT 2u          /* AICBT_BTPORT_UART */
#define AIC_BT_BAUD 1500000u      /* AICBT_UART_BAUD_1_5M */
#define AIC_BT_FLOWCTRL 1u        /* AICBT_UART_FLOWCTRL_ENABLE */
#define AIC_BT_LPM 0u
#define AIC_BT_TXPWR 0x00006F2Fu /* AICBT_TXPWR_LVL_8800d80 */

/* 越界检查：不许写主固件区（0x00120000 起 331348 B），不许非对齐 */
static int AicPatchAddrOk(UINT32 Addr) {
    if ((Addr & 3u) != 0u) {
        return 0;
    }
    if (Addr >= AIC_RAM_FMAC_FW_ADDR && Addr < AIC_RAM_FMAC_FW_ADDR + AIC_D80_FMAC_SIZE) {
        return 0;
    }
    return 1;
}

/* 把表里的 7 条记录摊平成计划（顺序 = 厂商 for 循环顺序，不重排） */
static int AicPatchBuildPlan(const UINT8 *Table, UINT32 Len, UINT32 *OutCount) {
    UINT32 Off = AICBT_PT_HDR;
    UINT32 Type;
    UINT32 EntryLen;
    UINT32 i;
    UINT32 N = 0;
    UINT32 PerTypeGot[7];
    UINT32 ModeRecs = 0;
    UINT32 k2;

    /* 评审 P1-5：gAicModeVal 是 static，表里 MODE 少于 9 对时会读到上一次的残留 */
    for (k2 = 0; k2 < 9u; k2++) {
        gAicModeVal[k2] = 0u;
    }
    for (k2 = 0; k2 < 7u; k2++) {
        PerTypeGot[k2] = 0u;
    }
    while (Off < Len) {
        if (Len - Off < 24u) {
            SdhciMarkHex("WiFi: patch plan: trailing bytes ", Len - Off, 4);
            SdhciMark(" (tail < 24)\n");
            return -1;
        }
        Type = (UINT32)Table[Off + 16u] | ((UINT32)Table[Off + 17u] << 8) |
               ((UINT32)Table[Off + 18u] << 16) | ((UINT32)Table[Off + 19u] << 24);
        EntryLen = (UINT32)Table[Off + 20u] | ((UINT32)Table[Off + 21u] << 8) |
                   ((UINT32)Table[Off + 22u] << 16) | ((UINT32)Table[Off + 23u] << 24);
        if (Type >= 1000u) {
            EntryLen = 0;
        }
        /* 评审 P0-3：先除后乘，杜绝 EntryLen*8 的 32 位回绕 */
        if (EntryLen > (Len - Off - 24u) / 8u) {
            SdhciMarkHex("WiFi: patch plan: entry overruns, len=", EntryLen, 4);
            SdhciMarkHex(" at off=", Off, 8);
            SdhciMark("\n");
            return -1;
        }
        if (Type < 7u) {
            PerTypeGot[Type] += EntryLen;
        }
        if (Type == AICBT_PT_BTMODE) {
            ModeRecs++;
        }
        if (Type == AICBT_PT_VER) {
            char Ver[72];
            UINT32 k;
            UINT32 NChars = EntryLen * 8u;
            if (NChars > (UINT32)sizeof(Ver) - 1u) {
                NChars = (UINT32)sizeof(Ver) - 1u;
            }
            for (k = 0; k < NChars; k++) {
                UINT8 C = Table[Off + 24u + k];
                Ver[k] = (C >= 0x20u && C < 0x7Fu) ? (char)C : '.';
            }
            Ver[NChars] = 0;
            SdhciMark("WiFi: patch ver=\"");
            SdhciMark(Ver);
            SdhciMark("\" (print only, no write)\n");
            Off += 24u + EntryLen * 8u;
            continue;
        }
        for (i = 0; i < EntryLen; i++) {
            const UINT8 *P = &Table[Off + 24u + i * 8u];
            UINT32 Addr = (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
                          ((UINT32)P[3] << 24);
            UINT32 Val = (UINT32)P[4] | ((UINT32)P[5] << 8) | ((UINT32)P[6] << 16) |
                         ((UINT32)P[7] << 24);
            UINT32 Tbl = Val; /* 表里原值，覆盖后仍保留一列用于对账 */
            UINT8 Kind = AIC_PLAN_MEM;

            if (Type == AICBT_PT_BTMODE) {
                /*
                 * 厂商 aic_bsp_driver.c:1481-1493 覆盖的是 **pair 的 value**：
                 * data 是 u32*，每对是 (data[0]=addr, data[1]=val)，所以
                 *   *(data+1)=hwinfo<0  *(data+3)=hwinfo  *(data+5)=cpmode
                 *   *(data+7)=btmode  *(data+9)=btport  *(data+11)=baud
                 *   *(data+13)=flowctrl  *(data+15)=lpm  *(data+17)=txpwr
                 * 换算成 pair 下标就是 0..8 —— 不是 1/3/5/…/17。
                 * 上一版把字下标当 pair 下标，9 条整体错位（btmode 槽位写进了 0xffffffff）。
                 */
                switch (i) {
                case 0u: Val = 1u; break; /* hwinfo < 0 → true（hwinfo 未知） */
                case 1u: Val = AIC_BT_HWINFO; break;
                case 2u: Val = AIC_BT_CPMODE; break;
                case 3u: Val = AIC_BT_BTMODE; break;
                case 4u: Val = AIC_BT_BTPORT; break;
                case 5u: Val = AIC_BT_BAUD; break;
                case 6u: Val = AIC_BT_FLOWCTRL; break;
                case 7u: Val = AIC_BT_LPM; break;
                case 8u: Val = AIC_BT_TXPWR; break;
                default: break;
                }
                if (i < 9u) {
                    gAicModeVal[i] = Val;
                }
                Kind = AIC_PLAN_CFG;
            }
            if (AicPatchAddrOk(Addr) == 0) {
                SdhciMarkHex("WiFi: patch plan: bad addr ", Addr, 8);
                SdhciMarkHex(" at entry off=", Off, 8);
                SdhciMark("\n");
                return -1;
            }
            if (N >= AIC_PLAN_MAX - 2u) {
                SdhciMark("WiFi: patch plan overflow\n");
                return -1;
            }
            gAicPlan[N].Kind = Kind;
            gAicPlan[N].Type = (UINT8)Type;
            gAicPlan[N].Addr = Addr;
            gAicPlan[N].Val = Val;
            gAicPlan[N].Tbl = Tbl;
            N++;
        }
        if (Type == AICBT_PT_PWRON) {
            gAicPlan[N].Kind = AIC_PLAN_DELAY;
            gAicPlan[N].Type = (UINT8)Type;
            gAicPlan[N].Addr = 500u;
            gAicPlan[N].Val = 0u;
            N++;
        }
        Off += 24u + EntryLen * 8u;
    }
    if (Off != Len) {
        SdhciMarkHex("WiFi: patch plan: walk ended at ", Off, 8);
        SdhciMarkHex(" len=", Len, 8);
        SdhciMark("\n");
        return -1;
    }
    /*
     * 评审 P0-2：条数自检。表被截断时（例如只读到 768 B）解析会"成功"但少几百条，
     * 以前照样 apply。这里把按类型的小计钉死在 u02 的画像上，不一致就 INCOMPLETE。
     */
    {
        static const UINT32 Want[7] = {4u, 19u, 45u, 9u, 3u, 20u, 8u};
        static const char *KindName[7] = {"inf", "trap", "tb4", "mode", "pwron", "taf", "ver"};
        for (k2 = 0; k2 < 7u; k2++) {
            if (PerTypeGot[k2] != Want[k2]) {
                SdhciMark("WiFi: patch plan INCOMPLETE: ");
                SdhciMark(KindName[k2]);
                SdhciMarkDecimal(" got=", PerTypeGot[k2]);
                SdhciMarkDecimal(" want=", Want[k2]);
                SdhciMark("\n");
                return -1;
            }
        }
        if (ModeRecs != 1u) {
            SdhciMarkDecimal("WiFi: patch plan INCOMPLETE: MODE records=", ModeRecs);
            SdhciMark(" (want 1)\n");
            return -1;
        }
        if (N != 1u + 4u + 19u + 45u + 9u + 3u + 20u) {
            SdhciMarkDecimal("WiFi: patch plan INCOMPLETE: items=", N);
            SdhciMark(" (want 101)\n");
            return -1;
        }
    }
    *OutCount = N;
    return 0;
}

/* ---------- B2：dry 清单 + apply ---------- */

#define AIC_PATCH_NOT_IMPLEMENTED (-2)

static const char *AicPatchTypeName(UINT32 Type) {
    switch (Type) {
    case AICBT_PT_INF: return "INF";
    case AICBT_PT_TRAP: return "TRAP";
    case AICBT_PT_B4: return "TB4";
    case AICBT_PT_BTMODE: return "MODE";
    case AICBT_PT_PWRON: return "PWRON";
    case AICBT_PT_AF: return "TAF";
    case AICBT_PT_VER: return "VER";
    default: return "?";
    }
}

/*
 * MODE 自检（真机评审要求）：写完清单立刻验算，不合法就不许进 apply。
 *   btmode 不能是 0xFF（AICBT_MODE_NULL = 非法值）
 *   baud 不能是 0
 *   flowctrl 只能是 0/1
 */
static int AicPatchSelfCheck(void) {
    if (gAicModeVal[3] == 0xFFu) {
        SdhciMark("WiFi: patch plan INVALID: btmode=000000ff (AICBT_MODE_NULL)\n");
        return -1;
    }
    if (gAicModeVal[5] == 0u) {
        SdhciMark("WiFi: patch plan INVALID: baud=0\n");
        return -1;
    }
    if (gAicModeVal[6] > 1u) {
        SdhciMarkHex("WiFi: patch plan INVALID: flowctrl=", gAicModeVal[6], 8);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkDecimal("WiFi: patch self-check ok (btmode=", gAicModeVal[3]);
    SdhciMarkDecimal(" baud=", gAicModeVal[5]);
    SdhciMarkDecimal(" fc=", gAicModeVal[6]);
    SdhciMark(")\n");
    return 0;
}

/* 打一遍清单（dry 用，也用于 apply 前的回显）：一条一行，序号两位 */
static void AicPatchPrintPlan(UINT32 Count) {
    UINT32 i;
    UINT32 Mem = 0;
    UINT32 Cfg = 0;
    UINT32 Delay = 0;

    UINT32 PerType[7];
    UINT32 k;

    for (k = 0; k < 7u; k++) {
        PerType[k] = 0;
    }
    for (i = 0; i < Count; i++) {
        AIC_PLAN_ITEM *It = &gAicPlan[i];
        if (It->Kind == AIC_PLAN_DELAY) {
            Delay++;
            continue;
        }
        if (It->Kind == AIC_PLAN_CFG) {
            Cfg++;
        } else {
            Mem++;
        }
        if (It->Type < 7u) {
            PerType[It->Type]++;
        }
    }
    SdhciMarkDecimal("WiFi: patch plan: 0", Mem + Cfg);
    SdhciMark(" writes, sentinels=6\n");
    SdhciMarkDecimal("WiFi: patch plan: mem=", Mem);
    SdhciMarkDecimal(" cfg=", Cfg);
    SdhciMarkDecimal(" delay=", Delay);
    SdhciMarkDecimal(" (+14 cfg ops: rd=4 wr=10), items=", Count);
    SdhciMark("\n");
    SdhciMarkDecimal("WiFi: patch plan: per type inf=", PerType[AICBT_PT_INF]);
    SdhciMarkDecimal(" trap=", PerType[AICBT_PT_TRAP]);
    SdhciMarkDecimal(" tb4=", PerType[AICBT_PT_B4]);
    SdhciMarkDecimal(" mode=", PerType[AICBT_PT_BTMODE]);
    SdhciMarkDecimal(" pwron=", PerType[AICBT_PT_PWRON]);
    SdhciMarkDecimal(" taf=", PerType[AICBT_PT_AF]);
    SdhciMark("\n");
    for (i = 0; i < Count; i++) {
        AIC_PLAN_ITEM *It = &gAicPlan[i];
        if (It->Kind == AIC_PLAN_DELAY) {
            SdhciMarkDecimal("WiFi: patch[", i);
            SdhciMark("] kind=delay type=PWRON udelay=500\n");
            continue;
        }
        SdhciMarkDecimal("WiFi: patch[", i);
        SdhciMark("] ");
        if (It->Kind == AIC_PLAN_CFG) {
            /* 这些值来自厂商运行时覆盖（不是表值），表值留一列便于对账 */
            SdhciMark("kind=cfg src=vendor ");
        } else {
            SdhciMark("kind=mem ");
        }
        SdhciMark("type=");
        SdhciMark(AicPatchTypeName(It->Type));
        SdhciMarkHex(" addr=", It->Addr, 8);
        SdhciMarkHex(" val=", It->Val, 8);
        if (It->Kind == AIC_PLAN_CFG) {
            SdhciMarkHex(" (table=", It->Tbl, 8);
            SdhciMark(")");
        }
        SdhciMark("\n");
    }
    /* patch_config_8800d80（aic8800d80_compat.c:114-248）的 14 个 op，地址是符号式的 */
    SdhciMark("WiFi: patch[cfg] kind=rd addr=00120198 -> config_base\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=001201a0 -> str_base\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=0012001c -> fw_version\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=001201a4 -> patch_buff_base (only if ver>06090100)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x00 val=48435450 (magic)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x08 val=50544348 (magic2)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x04 val=<patch_addr> (pair_start)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x0c val=<pair_cnt> (pair_count)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x30..3c val=00000000 (block_size[0..3])\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=pair+0x00 val=<config_base+0x00b4>\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=pair+0x04 val=f3010000\n");
}

/*
 * patch_config_8800d80（aic8800d80_compat.c:114-248）。
 * 评审 P1-2：4 个读是"最可能失败"的一步，必须提到 100 条写之前做纯读 preflight，
 * 读不到/不合法就 0 写退出；评审 P1-3：合法窗口按镜像实际范围写死，不再只看 0/0xffffffff。
 */
typedef struct {
    UINT32 ConfigBase;
    UINT32 StrBase;
    UINT32 Version;
    UINT32 PatchAddr;
    UINT32 PairCnt;
} AIC_PATCH_CFG;

/* 合法窗口：fmacfw 0x120000..0x170E54、patch 0x1E0000..0x1E7558、adid 0x201940..0x201FEC
   全部落在 [0x00010000, 0x00210000) 内；并要求 4 字节对齐。 */
#define AIC_RAM_LO 0x00010000u
#define AIC_RAM_HI 0x00210000u

static int AicPatchAddrWindowOk(const char *What, UINT32 Addr) {
    if ((Addr & 3u) != 0u) {
        SdhciMarkHex("WiFi: patch cfg: ", Addr, 8);
        SdhciMark(" not 4-aligned (");
        SdhciMark(What);
        SdhciMark(")\n");
        return 0;
    }
    if (Addr < AIC_RAM_LO || Addr >= AIC_RAM_HI) {
        SdhciMarkHex("WiFi: patch cfg: ", Addr, 8);
        SdhciMark(" outside RAM window 0x10000..0x20ffff (");
        SdhciMark(What);
        SdhciMark(")\n");
        return 0;
    }
    return 1;
}

/* 4 个读 + 判据（不写任何东西） */
static int AicPatchCfgReads(SDHCI_HOST *Host, AIC_PATCH_CFG *Cfg) {
    UINT32 k;
    for (k = 0; k < sizeof(*Cfg); k++) {
        ((UINT8 *)Cfg)[k] = 0;
    }
    Cfg->PairCnt = 1u; /* patch_tbl_8800d80 在 u02 非 5G 下只有 1 对 */

    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x0198u, &Cfg->ConfigBase) != 0) {
        SdhciMark("WiFi: patch cfg: rd 00120198 FAIL (no config_base)\n");
        return -1;
    }
    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x01A0u, &Cfg->StrBase) != 0) {
        SdhciMark("WiFi: patch cfg: rd 001201a0 FAIL (no str_base)\n");
        return -1;
    }
    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x001Cu, &Cfg->Version) != 0) {
        SdhciMark("WiFi: patch cfg: rd 0012001c FAIL (no version)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: patch cfg: config_base=", Cfg->ConfigBase, 8);
    SdhciMarkHex(" str_base=", Cfg->StrBase, 8);
    SdhciMarkHex(" ver=", Cfg->Version, 8);
    SdhciMark("\n");
    if (!AicPatchAddrWindowOk("str_base", Cfg->StrBase)) {
        return -1;
    }
    if (!AicPatchAddrWindowOk("config_base", Cfg->ConfigBase)) {
        return -1;
    }
    if (Cfg->Version > 0x06090100u) {
        if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x01A4u, &Cfg->PatchAddr) != 0) {
            SdhciMark("WiFi: patch cfg: rd 001201a4 FAIL (no patch_buff_base)\n");
            return -1;
        }
        SdhciMarkHex("WiFi: patch cfg: patch_buff_base=", Cfg->PatchAddr, 8);
        SdhciMark(" (new map)\n");
        /* 评审 P1-3：厂商会把它当 0 用，那更糟；这里直接失败，不许猜 */
        if (Cfg->PatchAddr == 0u || !AicPatchAddrWindowOk("patch_buff_base", Cfg->PatchAddr)) {
            SdhciMark("WiFi: patch cfg: patch_buff_base unusable, refuse (no guessing)\n");
            return -1;
        }
    } else {
        Cfg->PatchAddr = 0x0016F800u;
        SdhciMark("WiFi: patch cfg: old map (ver<=06090100), use 0016f800\n");
    }
    SdhciMark("WiFi: patch cfg: 4 reads ok (preflight, nothing written yet)\n");
    return 0;
}

/* 10 个写（aic8800d80_compat.c:196-248）：magic/pair 4 条 + block_size[4] + pair 2 条 */
static int AicPatchCfgWrites(SDHCI_HOST *Host, const AIC_PATCH_CFG *Cfg) {
    UINT32 k;

    if (AicMemWrite32(Host, Cfg->StrBase + 0x00u, 0x48435450u) != 0 ||
        AicMemWrite32(Host, Cfg->StrBase + 0x08u, 0x50544348u) != 0 ||
        AicMemWrite32(Host, Cfg->StrBase + 0x04u, Cfg->PatchAddr) != 0 ||
        AicMemWrite32(Host, Cfg->StrBase + 0x0Cu, Cfg->PairCnt) != 0) {
        SdhciMark("WiFi: patch cfg: magic/pair write FAIL\n");
        return -1;
    }
    for (k = 0; k < 4u; k++) {
        if (AicMemWrite32(Host, Cfg->StrBase + 0x30u + k * 4u, 0u) != 0) {
            SdhciMarkDecimal("WiFi: patch cfg: block_size write FAIL k=", k);
            SdhciMark("\n");
            return -1;
        }
    }
    if (AicMemWrite32(Host, Cfg->PatchAddr + 0u, Cfg->ConfigBase + 0x00B4u) != 0 ||
        AicMemWrite32(Host, Cfg->PatchAddr + 4u, 0xF3010000u) != 0) {
        SdhciMark("WiFi: patch cfg: pair write FAIL\n");
        return -1;
    }
    SdhciMark("WiFi: patch cfg: 10 writes ok\n");
    return 0;
}

/* dry 打印用：把要做的 14 个 op 列出来 */
static void AicPatchCfgPrint(void) {
    SdhciMark("WiFi: patch[cfg] kind=rd addr=00120198 -> config_base\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=001201a0 -> str_base\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=0012001c -> fw_version\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=001201a4 -> patch_buff_base (only if ver>06090100)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x00 val=48435450 (magic)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x08 val=50544348 (magic2)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x04 val=<patch_addr> (pair_start)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x0c val=<pair_cnt> (pair_count)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x30..3c val=00000000 (block_size[0..3])\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=pair+0x00 val=<config_base+0x00b4>\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=pair+0x04 val=f3010000\n");
}

/* 评审 P1-6：写完必须读回。6 个哨兵，任一不等即失败 */
static int AicPatchSentinels(SDHCI_HOST *Host, const AIC_PATCH_CFG *Cfg) {
    UINT32 V = 0;
    (void)Cfg; /* 6 个哨兵地址是固定的；StrBase 的 magic 由 cfg 写路径自己验 */
    UINT32 Want = 0;
    UINT8 F[4];
    UINT32 Got = 0;
    UINT32 k;

    struct {
        UINT32 Addr;
        UINT32 Want;
        const char *Name;
    } S[6];
    UINT32 N = 0;

    S[N].Addr = 0x40500150u; S[N].Want = 1u;          S[N].Name = "INF reset";        N++;
    S[N].Addr = 0x001E7294u; S[N].Want = 0x001E0000u; S[N].Name = "INF patch_addr";   N++;
    S[N].Addr = 0x0020F600u; S[N].Want = 0xF0223201u; S[N].Name = "TRAP first";       N++;
    S[N].Addr = 0x00201FC8u; S[N].Want = 5u;          S[N].Name = "MODE btmode";      N++;

    for (k = 0; k < N; k++) {
        if (AicMemRead(Host, S[k].Addr, &V) != 0) {
            SdhciMark("WiFi: sentinel ");
            SdhciMark(S[k].Name);
            SdhciMark(" read FAIL\n");
            return -1;
        }
        if (V != S[k].Want) {
            SdhciMark("WiFi: sentinel ");
            SdhciMark(S[k].Name);
            SdhciMarkHex(" got=", V, 8);
            SdhciMarkHex(" want=", S[k].Want, 8);
            SdhciMark(" MISMATCH\n");
            return -1;
        }
    }
    /* adid 首字 == fw_adid.bin 首 4 字节 */
    if (AicFwReadChunk(AIC_FW_ADID, 0, F, 4u, &Got) != 0 || Got != 4u) {
        SdhciMark("WiFi: sentinel adid: cannot read file head\n");
        return -1;
    }
    Want = (UINT32)F[0] | ((UINT32)F[1] << 8) | ((UINT32)F[2] << 16) | ((UINT32)F[3] << 24);
    if (AicMemRead(Host, 0x00201940u, &V) != 0 || V != Want) {
        SdhciMarkHex("WiFi: sentinel adid head got=", V, 8);
        SdhciMarkHex(" want=", Want, 8);
        SdhciMark(" MISMATCH\n");
        return -1;
    }
    /* fmacfw 首字 == fmacfw.bin 首 4 字节 */
    if (AicFwReadChunk(AIC_FW_FMAC, 0, F, 4u, &Got) != 0 || Got != 4u) {
        SdhciMark("WiFi: sentinel fmacfw: cannot read file head\n");
        return -1;
    }
    Want = (UINT32)F[0] | ((UINT32)F[1] << 8) | ((UINT32)F[2] << 16) | ((UINT32)F[3] << 24);
    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR, &V) != 0 || V != Want) {
        SdhciMarkHex("WiFi: sentinel fmacfw head got=", V, 8);
        SdhciMarkHex(" want=", Want, 8);
        SdhciMark(" MISMATCH\n");
        return -1;
    }
    SdhciMark("WiFi: sentinels 6/6 ok\n");
    return 0;
}

/* dry：只解析表 + 打清单，一个字节不写、一条消息不发 */
static int AicPatchDry(void) {
    UINT32 TableLen = 0;
    UINT32 Count = 0;

    SdhciMark("WiFi: patch dry start\n");
    if (AicFwLoadPatchTable(0, gAicFwBuf, AIC_FW_CHUNK, &TableLen) != 0) {
        SdhciMark("WiFi: patch dry: table unusable\n");
        return -1;
    }
    if (AicPatchBuildPlan(gAicFwBuf, TableLen, &Count) != 0) {
        SdhciMark("WiFi: patch dry: plan build FAILED\n");
        return -1;
    }
    AicPatchPrintPlan(Count);
    if (AicPatchSelfCheck() != 0) {
        SdhciMark("WiFi: patch plan end INVALID, nothing written\n");
        return -1;
    }
    AicPatchCfgPrint(); /* dry 只打印，不读不写 */
    SdhciMark("WiFi: patch plan end, nothing written\n");
    return 0;
}

/* apply：真写。必须先过 dry，失败即停，绝不续写 */
static int AicPatchApply(SDHCI_HOST *Host, const UINT8 *Table, UINT32 Len) {
    UINT32 Count = 0;
    UINT32 i;
    UINT32 Done = 0;
    AIC_PATCH_CFG Cfg;

    /* 评审 P1-4：三态。DONE 不许重放；POISONED（上次写失败）也不许续写，必须重新 probe。 */
    if (gAicPatchState == AIC_PATCH_DONE) {
        SdhciMark("WiFi: patch already applied (this power cycle)\n");
        return -1;
    }
    if (gAicPatchState == AIC_PATCH_POISONED) {
        SdhciMark("WiFi: patch state POISONED by an earlier failure, re-probe before retry\n");
        return -1;
    }
    if (AicPatchBuildPlan(Table, Len, &Count) != 0) {
        SdhciMark("WiFi: patch apply: plan build FAILED\n");
        return -1;
    }
    if (AicPatchSelfCheck() != 0) {
        SdhciMark("WiFi: patch apply refused: plan INVALID\n");
        return -1;
    }
    /*
     * 评审 P1-2：先把 4 个 cfg 读做完（纯读）。这里是全链路最可能失败的一步，
     * 排在 100 条写之前 —— 不合法就 0 写退出。
     */
    if (AicPatchCfgReads(Host, &Cfg) != 0) {
        SdhciMark("WiFi: patch apply refused: cfg preflight FAILED (0 writes done)\n");
        return -1;
    }
    SdhciMarkDecimal("WiFi: patch apply start, ", Count);
    SdhciMark(" items\n");
    for (i = 0; i < Count; i++) {
        AIC_PLAN_ITEM *It = &gAicPlan[i];
        if (It->Kind == AIC_PLAN_DELAY) {
            SdhciDelayMicroseconds(It->Addr);
            continue;
        }
        if (AicMemWrite32(Host, It->Addr, It->Val) != 0) {
            SdhciMarkDecimal("WiFi: patch[", i);
            SdhciMark("] type=");
            SdhciMark(AicPatchTypeName(It->Type));
            SdhciMarkHex(" addr=", It->Addr, 8);
            SdhciMarkHex(" val=", It->Val, 8);
            SdhciMark(" FAIL (stop, no resume)\n");
            SdhciMarkDecimal("WiFi: patch applied ", Done);
            SdhciMark("/");
            SdhciMarkDecimal("", Count);
            SdhciMark(" then stopped; state=POISONED (re-probe required)\n");
            gAicPatchState = AIC_PATCH_POISONED;
            return -1;
        }
        Done++;
        if ((Done % 16u) == 0u || Done == Count) {
            SdhciMarkDecimal("WiFi: patch progress ", Done);
            SdhciMark("/");
            SdhciMarkDecimal("", Count);
            SdhciMark(" ok\n");
        }
    }
    if (AicPatchCfgWrites(Host, &Cfg) != 0) {
        SdhciMark("WiFi: patch cfg FAILED (table writes done, cfg not)\n");
        gAicPatchState = AIC_PATCH_POISONED;
        return -1;
    }
    /* 评审 P1-6：没有读回的"成功"不算成功 */
    if (AicPatchSentinels(Host, &Cfg) != 0) {
        SdhciMark("WiFi: patch sentinels FAILED (chip does not hold what we wrote)\n");
        gAicPatchState = AIC_PATCH_POISONED;
        return -1;
    }
    gAicPatchState = AIC_PATCH_DONE;
    SdhciMarkDecimal("WiFi: patch applied: ", Count);
    SdhciMark("/");
    SdhciMarkDecimal("", Count);
    SdhciMark(" ok, state=DONE\n");
    return 0;
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
    if (Stage->Kind != AIC_STAGE_FILE) {
        return 0; /* patchtbl / start 阶段没有文件 */
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
        /* 评审 P0-2：短读一律失败，绝不"读到多少写多少" */
        if (Got != InChunk) {
            SdhciMarkHex("WiFi: short read got=", Got, 4);
            SdhciMarkHex(" want=", InChunk, 4);
            SdhciMark("\n");
            AicFwFail(Stage->Name, gWifiFwSent, "short-read");
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
    if (gAicPatchState != AIC_PATCH_DONE) {
        SdhciMark("WiFi: start_app refused: patch not applied cleanly (review W4)\n");
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

/* 固定 dest=TASK_DBG 的常用写法（DBG 系列消息） */
static int AicSendMsg(SDHCI_HOST *Host, UINT16 Id, const void *Param, UINT16 ParamLen) {
    return AicSendMsgEx(Host, Id, (UINT16)AIC_TASK_DBG, Param, ParamLen);
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
        if (Stage->Kind == AIC_STAGE_PATCHTBL) {
            SdhciMark(" table=" AIC_FW_PATCH_TABLE " (100 writes + cfg 14 ops)\n");
            continue;
        }
        if (Stage->Kind == AIC_STAGE_START) {
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
        if (Stage->Kind == AIC_STAGE_PATCHTBL) {
            /*
             * 评审 P0-1：patch 表落地是 write confirm 的第 4 阶段。
             * 表必须整份读进来（长度 == stat 长度），读完才允许开始写。
             */
            UINT32 TableLen = 0;
            FAT_FILE_STAT Stat;
            if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) != FAT_OK || Stat.Size == 0u ||
                Stat.Size > AIC_FW_CHUNK ||
                AicFwReadChunk(AIC_FW_PATCH_TABLE, 0u, gAicFwBuf, Stat.Size, &TableLen) != 0 ||
                TableLen != Stat.Size) {
                AicFwFail("patchtbl", 0u, "table-read");
                return -1;
            }
            if (AicPatchApply(Host, gAicFwBuf, TableLen) != 0) {
                AicFwFail("patchtbl", 0u, "apply");
                return -1;
            }
            continue;
        }
        if (Stage->Kind == AIC_STAGE_START) {
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
    SdhciMarkHex("WiFi: mem rd addr=", AIC_D80_CHIP_REV_ADDR, 8);
    SdhciMarkHex(" data=", Value, 8);
    SdhciMark("\n");
    SdhciMarkHex("WiFi: chip rev data=", Value, 8);
    SdhciMarkHex(" rev=", (Value >> 16) & 0xFFu, 2);
    SdhciMark("\n");
    return 0;
}

/*
 * 评审 P0-1：`wififw patch apply` 已删除。它能在芯片里没有 fmacfw 时单独跑，
 * cfg 读会拿到 ROM 残留 → 拿野地址写 magic；一旦"成功"还会置位，把真正的
 * write confirm 卡在 "patch already applied"。现在 patch 表落地只能是
 * write confirm 的第 4 阶段。保留这个入口只为打印一句明确的迁移提示。
 */
/*
 * 只读诊断：`wififw rd <hexaddr>`。真写之后要能读回内存对账，这是唯一手段。
 * 只做准备动作 + 一次 mem read，不改 patch 状态、不发任何写消息。
 */
int SdhciWifiFwRead(UINT32 Address) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 Value = 0;

    if ((Address & 3u) != 0u) {
        SdhciMarkHex("WiFi: mem rd refused: not 4-aligned addr=", Address, 8);
        SdhciMark("\n");
        return -1;
    }
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: mem rd needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: mem rd: vendor prepare failed\n");
        return -1;
    }
    if (AicMemRead(Host, Address, &Value) != 0) {
        SdhciMarkHex("WiFi: mem rd FAIL addr=", Address, 8);
        SdhciMarkHex(" r5=", Host->LastR5, 8);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkHex("WiFi: mem rd addr=", Address, 8);
    SdhciMarkHex(" data=", Value, 8);
    SdhciMark("\n");
    return 0;
}

/*
 * 单字写诊断：`wififw wr <addr> <val>`。
 * 走 DBG_MEM_WRITE_REQ（不是 block write），**写完立刻读回**（同一命令、同一上电周期），
 * 用来回答"写到底通不通"这个至今没有证据的问题。
 *
 * 护栏：只允许写 patch/fw 会用到的两段——
 *   [0x00100000, 0x00210000)  RAM（adid/patch/fmacfw 都在这片，重下即覆盖）
 *   [0x40500000, 0x40510000)  外设寄存器（PWRON/TRAP 要打的那些）
 * 其它地址一律拒绝并说明原因。
 */
int SdhciWifiFwWriteDiag(UINT32 Address, UINT32 Value) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT32 ReadBack = 0;

    if ((Address & 3u) != 0u) {
        SdhciMarkHex("WiFi: mem wr refused: not 4-aligned addr=", Address, 8);
        SdhciMark("\n");
        return -1;
    }
    if (!((Address >= 0x00100000u && Address < 0x00210000u) ||
          (Address >= 0x40500000u && Address < 0x40510000u))) {
        SdhciMarkHex("WiFi: mem wr refused: addr outside allowed windows ", Address, 8);
        SdhciMark("\n");
        SdhciMark("WiFi: allowed: [00100000,0020ffff] ram, [40500000,4050ffff] regs\n");
        return -1;
    }
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: mem wr needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: mem wr: vendor prepare failed\n");
        return -1;
    }
    SdhciMarkHex("WiFi: mem wr addr=", Address, 8);
    SdhciMarkHex(" val=", Value, 8);
    SdhciMark("\n");
    if (AicMemWrite32(Host, Address, Value) != 0) {
        SdhciMarkHex("WiFi: mem wr FAIL addr=", Address, 8);
        SdhciMarkHex(" r5=", Host->LastR5, 8);
        SdhciMark("\n");
        return -1;
    }
    /* 同一命令内立刻读回：中间不允许插入别的操作 */
    if (AicMemRead(Host, Address, &ReadBack) != 0) {
        SdhciMarkHex("WiFi: mem wr readback FAIL addr=", Address, 8);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkHex("WiFi: mem wr readback=", ReadBack, 8);
    if (ReadBack == Value) {
        SdhciMark(" (match)\n");
        return 0;
    }
    SdhciMarkHex(" (MISMATCH, wrote ", Value, 8);
    SdhciMark(")\n");
    return -1;
}

/*
 * `wififw ver`：发 MM_VERSION_REQ（id=0x0004, dest=TASK_MM=0, plen=0），等 MM_VERSION_CFM(0x0005)。
 *
 * 为什么它是"固件到底跑没跑"最干净的判据：
 *   DBG 任务（mem read/write、start_app）**ROM 里就有**，所以 ROM 阶段也能应答；
 *   MM 任务只有**下载进去的固件**才实现 —— 收到 version cfm 就证明 fmacfw 已经在跑。
 *
 * 只读：不发写消息、不改任何内存、不动 patch 状态。
 */
static int WifiFwVersionProbe(SDHCI_HOST *Host) {
    UINT8 Cfm[64];
    UINT32 CfmLen = 0;
    UINT32 k;

    SdhciMarkHex("WiFi: msg tx id=", AIC_MM_VERSION_REQ, 4);
    SdhciMarkHex(" dest=", AIC_TASK_MM, 4);
    SdhciMarkHex(" src=", AIC_DRV_TASK_ID, 4);
    SdhciMark(" plen=0\n");
    if (AicSendMsgEx(Host, (UINT16)AIC_MM_VERSION_REQ, (UINT16)AIC_TASK_MM, 0, 0u) != 0) {
        SdhciMark("WiFi: ver: msg send FAILED\n");
        return -1;
    }
    /* 2 s 预算（4 × 500 ms），期间轮询 fn1 pending */
    if (AicRecvMsgTimeout(Host, (UINT16)AIC_MM_VERSION_CFM, Cfm, sizeof(Cfm), &CfmLen, 4u,
                          AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: ver: no cfm in 2000ms  (firmware may not be running)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: rx cfm id=", AIC_MM_VERSION_CFM, 4);
    SdhciMarkHex(" plen=", CfmLen, 4);
    SdhciMark("\n");
    AicDumpBytes(Cfm, CfmLen);
    /* struct mm_version_cfm 前 4 个 u32（lmac_msg.h:711-730） */
    SdhciMark("WiFi: ver");
    for (k = 0; k + 4u <= CfmLen && k < 20u; k += 4u) {
        UINT32 V = (UINT32)Cfm[k] | ((UINT32)Cfm[k + 1] << 8) | ((UINT32)Cfm[k + 2] << 16) |
                   ((UINT32)Cfm[k + 3] << 24);
        if (k == 0u) {
            SdhciMarkHex(" lmac=", V, 8);
        } else if (k == 4u) {
            SdhciMarkHex(" machw1=", V, 8);
        } else if (k == 8u) {
            SdhciMarkHex(" machw2=", V, 8);
        } else if (k == 12u) {
            SdhciMarkHex(" phy1=", V, 8);
        } else if (k == 16u) {
            SdhciMarkHex(" phy2=", V, 8);
        }
    }
    SdhciMark("\n");
    if (CfmLen < 16u) {
        SdhciMarkHex("WiFi: ver: cfm plen<16, only ", CfmLen, 4);
        SdhciMark(" bytes available\n");
    }
    return 0;
}

int SdhciWifiFwVersion(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);

    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: ver needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: ver: vendor prepare failed\n");
        return -1;
    }
    return WifiFwVersionProbe(Host);
}

/*
 * `wififw mmreset`：MM_RESET_REQ(0x0000) → MM_RESET_CFM(0x0001)，复位后自动再查一次版本。
 * 复位后固件应当仍在跑（MM 任务属于固件），版本不变即可确认"复位没把它打死"。
 */
int SdhciWifiFwMmReset(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT8 Cfm[16];
    UINT32 CfmLen = 0;

    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: mmreset needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: mmreset: vendor prepare failed\n");
        return -1;
    }
    SdhciMarkHex("WiFi: msg tx id=", AIC_MM_RESET_REQ, 4);
    SdhciMarkHex(" dest=", AIC_TASK_MM, 4);
    SdhciMarkHex(" src=", AIC_DRV_TASK_ID, 4);
    SdhciMark(" plen=0\n");
    if (AicSendMsgEx(Host, (UINT16)AIC_MM_RESET_REQ, (UINT16)AIC_TASK_MM, 0, 0u) != 0) {
        SdhciMark("WiFi: mm reset: msg send FAILED\n");
        return -1;
    }
    if (AicRecvMsgTimeout(Host, (UINT16)AIC_MM_RESET_CFM, Cfm, sizeof(Cfm), &CfmLen, 4u,
                          AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: mm reset: no cfm in 2000ms\n");
        return -1;
    }
    SdhciMarkHex("WiFi: rx cfm id=", AIC_MM_RESET_CFM, 4);
    SdhciMarkHex(" plen=", CfmLen, 4);
    SdhciMark("\n");
    SdhciMark("WiFi: mm reset ok\n");
    /* 复位后必须再验一次：CFM 只证明消息通路，版本才证明固件还活着 */
    if (WifiFwVersionProbe(Host) != 0) {
        SdhciMark("WiFi: mm reset: post-reset ver FAILED\n");
        return -1;
    }
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

    if ((Mode & SDHCI_WIFI_FW_PATCH_DRY) != 0u) {
        /* dry：只读文件 + 解析，不碰芯片（不写寄存器、不发消息） */
        if (AicPatchDry() != 0) {
            SdhciMark("WiFi: patch dry end FAILED\n");
            return -1;
        }
        SdhciMark("WiFi: patch dry end ok\n");
        return 0;
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
            SdhciMark("WiFi: write refused: patch stage not armed for this build\n");
            SdhciMark("WiFi: need: successful 'wififw patch dry' + user approval (gAicPatchReady)\n");
            SdhciMark("WiFi: see Archive/Duo-WiFi-patch-B2 review (P0-1..P0-3)\n");
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
