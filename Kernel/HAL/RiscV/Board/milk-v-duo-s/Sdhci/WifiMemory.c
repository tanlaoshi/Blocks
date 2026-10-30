/*
 * WifiMemory.c — AIC8800 寄存器与内存：fn1 寄存器、dbg mem read/write/block write
 *
 * 从 WifiFw.c 原样搬出（纯搬家，函数体逐字节不动），行为零变化。
 */
#include "WifiPrivate.h"

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
void AicMarkR5(UINT32 R5) {
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
int AicWifiVendorPrepare(SDHCI_HOST *Host) {
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
void AicMarkXferParams(SDHCI_HOST *Host, const char *Tag, UINT32 Bytes, UINT32 Blocks) {
    SdhciMark(Tag);
    SdhciMarkHex(" bytes=", Bytes, 4);
    SdhciMarkHex(" blocks=", Blocks, 2);
    SdhciMarkHex(" blksz=", SdhciRead16(Host, SDHCI_BLOCK_SIZE), 4);
    SdhciMarkHex(" blkcnt=", SdhciRead16(Host, SDHCI_BLOCK_COUNT), 4);
    SdhciMarkHex(" tmode=", SdhciRead16(Host, SDHCI_TRANSFER_MODE), 4);
    SdhciMarkHex(" pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
    SdhciMark("\n");
}

/* DBG_MEM_READ_REQ：读一个 32 位字（顺带证明 fn2 消息通道通） */
int AicMemRead(SDHCI_HOST *Host, UINT32 Address, UINT32 *Value) {
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
int AicMemBlockWrite(SDHCI_HOST *Host, UINT32 Address, UINT32 Size, const UINT8 *Data) {
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


int AicMemWrite32(SDHCI_HOST *Host, UINT32 Address, UINT32 Value) {
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
