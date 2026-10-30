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

/* 往 FIFO 写：按 512 B 分块，固定地址（OP_CODE=0），一块一块追加 */
static int AicFifoWrite(SDHCI_HOST *Host, const UINT8 *Data, UINT32 Len) {
    UINT32 Done = 0;
    UINT32 Step;

    while (Done < Len) {
        Step = Len - Done;
        if (Step > AIC_FIFO_CHUNK) {
            Step = AIC_FIFO_CHUNK;
        }
        if (SdhciIoRwExtended(Host, 1u, AIC_WR_FIFO_REG, 1, 0, 0, Step, (UINT8 *)(Data + Done)) !=
            0) {
            SdhciMarkHex("WiFi: aic fifo write fail at ", Done, 4);
            SdhciMark("\n");
            return -1;
        }
        Done += Step;
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

/* 芯片有没有消息：轮询 fn1 0x04 的 bit7（aicsdio.c:1506-1514） */
static int AicWaitPending(SDHCI_HOST *Host, UINT32 TimeoutMs) {
    UINT32 Waited = 0;
    UINT8 Status = 0;

    while (Waited < TimeoutMs) {
        if (SdhciIoRead8(Host, 1u, AIC_MISC_INT_REG, &Status) == 0 &&
            (Status & AIC_MISC_OTHER_INTR) != 0u) {
            return 0;
        }
        SdhciDelayMilliseconds(AIC_CFM_POLL_MS);
        Waited += AIC_CFM_POLL_MS;
    }
    return -1;
}

/*
 * 收一条确认：帧头 [0..1]=len [2]=type [3]=crc，正文是 ipc_e2a_msg
 * {u16 id, u16 dest, u16 src, u16 param_len, param…}。
 * 只认 type == 0x11/0x12 的配置帧，其它（比如 0x13 打印）跳过。
 */
static int AicRecvMsg(SDHCI_HOST *Host, UINT16 WantId, UINT8 *Param, UINT32 MaxParam,
                      UINT32 *OutParamLen) {
    UINT32 Tries;
    UINT32 Len;
    UINT32 ParamLen;
    UINT8 Header[4];

    for (Tries = 0; Tries < 4u; Tries++) {
        if (AicWaitPending(Host, AIC_CFM_TIMEOUT_MS) != 0) {
            return -1;
        }
        if (AicFifoRead(Host, Header, 4u) != 0) {
            return -1;
        }
        Len = (UINT32)(AicGet16(&Header[0]) & 0x0FFFu);
        if (Header[2] != AIC_TYPE_CFG_CMD_RSP && Header[2] != AIC_TYPE_CFG_DATA_CFM) {
            SdhciMarkHex("WiFi: aic rx skip type=", Header[2], 2);
            SdhciMarkHex(" len=", Len, 4);
            SdhciMark("\n");
            if (Len >= 4u && Len <= 500u) {
                (void)AicFifoRead(Host, gAicRx, Len); /* 丢掉这条，别让 FIFO 堵住 */
            }
            continue;
        }
        if (Len < 8u || Len > 500u) {
            SdhciMarkHex("WiFi: aic rx bad len=", Len, 4);
            SdhciMark("\n");
            return -1;
        }
        if (AicFifoRead(Host, gAicRx, Len) != 0) {
            return -1;
        }
        if (AicGet16(&gAicRx[0]) != WantId) {
            SdhciMarkHex("WiFi: aic rx id=", AicGet16(&gAicRx[0]), 4);
            SdhciMarkHex(" want=", WantId, 4);
            SdhciMark("\n");
            continue;
        }
        ParamLen = AicGet16(&gAicRx[6]);
        if (ParamLen > MaxParam) {
            ParamLen = MaxParam;
        }
        if (OutParamLen != 0) {
            *OutParamLen = ParamLen;
        }
        if (Param != 0 && ParamLen > 0u) {
            UINT32 i;
            for (i = 0; i < ParamLen && (8u + i) < Len; i++) {
                Param[i] = gAicRx[8u + i];
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
