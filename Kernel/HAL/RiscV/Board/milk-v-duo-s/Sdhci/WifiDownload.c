/*
 * WifiDownload.c — 固件下载：分块读、阶段检查、1 KB 块写、start_app、启动轮询
 *
 * 从 WifiFw.c 原样搬出（纯搬家，函数体逐字节不动），行为零变化。
 */
#include "WifiPrivate.h"

/* 读 patch 表、解析、把 adid/patch 落点改成表里的值；任何一步不过就失败 */
int AicFwLoadPatchTable(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 BufferSize, UINT32 *OutLength) {
    FAT_FILE_STAT Stat;
    UINT32 Got = 0;
    AIC_PATCH_INFO Info;
    int Rc;

    (void)Host;
    if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) != FAT_OK) {
        SdhciMark("WiFi: fw table missing " AIC_FW_PATCH_TABLE "\n");
        return -1;
    }
    if (Stat.Size == 0u || Stat.Size > BufferSize) {
        SdhciMarkDecimal("WiFi: fw table size out of range ", Stat.Size);
        SdhciMark("\n");
        return -1;
    }
    if (AicFwReadChunk(AIC_FW_PATCH_TABLE, 0u, Buffer, Stat.Size, &Got) != 0 || Got != Stat.Size) {
        SdhciMark("WiFi: fw table read failed\n");
        return -1;
    }
    Rc = AicPatchTableParse(Buffer, Got, &Info);
    if (Rc != 0) {
        SdhciMark("WiFi: fw table parse FAILED (refuse to guess)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: fw table: tag ok info_len=", Info.InfoLength, 4);
    SdhciMarkHex(" adid=", Info.AddressAdid, 8);
    SdhciMarkHex(" patch=", Info.AddressPatch, 8);
    SdhciMarkHex(" base=", Info.AdidAddressInfo, 8);
    SdhciMark("\n");
    /* B2 要用的两个按键：reset 与 adid_flag（评审 P2-2 的判据 0x40500150==1 就在这里） */
    SdhciMarkHex("WiFi: fw table: reset=", Info.ResetAddress, 8);
    SdhciMarkHex(":", Info.ResetValue, 8);
    SdhciMarkHex(" adid_flag=", Info.AdidFlagAddress, 8);
    SdhciMarkHex(":", Info.AdidFlag, 8);
    SdhciMark("\n");
    /* 表值优先；表里没有（0）才用兜底，并且明说是兜底 */
    if (Info.AddressAdid != 0u) {
        gAicStages[0].Address = Info.AddressAdid;
    } else {
        SdhciMarkHex("WiFi: fw table: adid addr missing, fallback=", gAicStages[0].Address, 8);
        SdhciMark(" (fallback, table missing)\n");
    }
    if (Info.AddressPatch != 0u) {
        gAicStages[1].Address = Info.AddressPatch;
    } else {
        SdhciMarkHex("WiFi: fw table: patch addr missing, fallback=", gAicStages[1].Address, 8);
        SdhciMark(" (fallback, table missing)\n");
    }
    *OutLength = Got;
    return 0;
}

/*
 * 先算后写：每个文件先 stat、比对预期长度，任何不一致立刻停，
 * 绝不允许"写到一半才发现文件不对"。
 */
int AicFwCheckStage(AIC_FW_STAGE *Stage, UINT32 *OutSize) {
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
int AicFwReadChunk(const char *Path, UINT32 Offset, UINT8 *Buffer, UINT32 Want,
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
int AicFwPushStage(SDHCI_HOST *Host, AIC_FW_STAGE *Stage, UINT32 Size) {
    UINT32 Off = 0;
    UINT32 Got = 0;
    UINT32 InChunk;
    UINT32 BlockIndex = 0;
    UINT32 TotalBlock = (Size + AIC_BLK_WRITE - 1u) / AIC_BLK_WRITE;
    UINT32 Word;
    UINT32 Want;

    SdhciMark("WiFi: stage=");
    SdhciMark(Stage->Name);
    SdhciMarkHex(" len=", Size, 4);
    SdhciMarkHex(" addr=", Stage->Address, 8);
    SdhciMarkHex(" blocks=", TotalBlock, 4);
    SdhciMark("\n");
    while (Off < Size) {
        InChunk = Size - Off;
        if (InChunk > AIC_FW_CHUNK) {
            InChunk = AIC_FW_CHUNK;
        }
        if (AicFwReadChunk(Stage->Path, Off, gAicFwBuffer, InChunk, &Got) != 0) {
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
        if (AicMemBlockWrite(Host, Stage->Address + Off, Got, gAicFwBuffer) != 0) {
            AicFwFail(Stage->Name, gWifiFwSent, "write");
            return -1;
        }
        /* 抽样校验：这块的首字读回来对一下（每 8 KB 一次，够抓地址/长度错） */
        Want = (UINT32)gAicFwBuffer[0] | ((UINT32)gAicFwBuffer[1] << 8) |
               ((UINT32)gAicFwBuffer[2] << 16) | ((UINT32)gAicFwBuffer[3] << 24);
        if (AicMemRead(Host, Stage->Address + Off, &Word) != 0) {
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
        BlockIndex = (Off + AIC_BLK_WRITE - 1u) / AIC_BLK_WRITE;
        if ((BlockIndex % 32u) == 0u || Off == Size) {
            SdhciMark("WiFi: stage=");
            SdhciMark(Stage->Name);
            SdhciMarkHex(" blk=", BlockIndex, 4);
            SdhciMarkHex("/", TotalBlock, 4);
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
int AicFwStartApp(SDHCI_HOST *Host, UINT32 BootAddress) {
    UINT8 Param[8];
    UINT8 Cfm[8];
    UINT32 CfmLength = 0;

    /*
     * 评审 W4：start_app 之前必须确认 patch 已应用，否则就是"固件写进去但起不来"。
     * patch 管线（patch_table 的 46 条写 + patch_config_8800d80）未实现，这里恒为 0。
     */
    if (gAicPatchState != AIC_PATCH_DONE) {
        SdhciMark("WiFi: start_app refused: patch not applied cleanly (review W4)\n");
        AicFwFail("start", 0u, "patch-not-applied");
        return -1;
    }

    AicPut16(&Param[0], (UINT16)(BootAddress & 0xFFFFu));
    AicPut16(&Param[2], (UINT16)((BootAddress >> 16) & 0xFFFFu));
    AicPut16(&Param[4], (UINT16)AIC_START_APP_AUTO);
    AicPut16(&Param[6], 0u);
    if (AicSendMessage(Host, AIC_DBG_START_APP_REQ, Param, 8u) != 0) {
        AicFwFail("start", 0u, "send");
        return -1;
    }
    if (AicRecvMessage(Host, AIC_DBG_START_APP_CFM, Cfm, 8u, &CfmLength) != 0) {
        AicFwFail("start", 0u, "cfm-timeout");
        return -1;
    }
    /* dbg_start_app_cfm { u32 bootstatus; } */
    SdhciMark("WiFi: stage=start");
    SdhciMarkHex(" bootaddr=", BootAddress, 8);
    SdhciMarkHex(" bootstatus=", (UINT32)(Cfm[0] | (Cfm[1] << 8) | (Cfm[2] << 16) | (Cfm[3] << 24)),
                 8);
    SdhciMark("\n");
    return 0;
}

/* 起跑后等第一条消息：只读、有上限，看不到也不算失败（可能只走 fn2/中断） */
void AicFwWaitBootMessage(SDHCI_HOST *Host) {
    UINT32 Fn = 0;
    UINT32 Length = 0;

    if (AicWaitMessagePending(Host, 2000u, &Fn, &Length) != 0) {
        SdhciMark("WiFi: boot poll: no msg in 2000ms\n");
        return;
    }
    SdhciMarkHex("WiFi: boot poll fn=", Fn, 2);
    SdhciMarkHex(" len=", Length, 4);
    SdhciMark("\n");
    if (Length > 0u && Length <= 512u) {
        if (AicFifoRead(Host, Fn, gAicRx, Length) == 0) {
            SdhciMarkHex("WiFi: boot msg type=", (UINT32)(gAicRx[2] & 0x7Fu), 2);
            SdhciMarkHex(" id=", AicGet16(&gAicRx[4]), 4);
            SdhciMark("\n");
            AicDumpBytes(gAicRx, Length);
        }
    }
}
