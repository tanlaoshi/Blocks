/*
 * WifiFw.c — Duo S AIC8800D80 固件下载（PR-30 S5b）入口与状态机
 *
 * 本文件只留：全局状态定义 + 阶段失败打印 + 7 个对外入口
 *   wififw stat / id / rd / wr / ver / mmreset / write [confirm]
 * 具体实现按职责拆到同目录几个文件（纯搬家，行为零变化）：
 *   WifiMessage.c    消息通道（FIFO/帧头/crc/收发行）
 *   WifiMemory.c     寄存器与 dbg 内存读写
 *   WifiPatchTable.c patch 表解析与写计划
 *   WifiPatchApply.c patch 落地（cfg 14 op + 100 写 + 哨兵）
 *   WifiDownload.c   分块读 + 块写 + start_app
 *   WifiProbe.c      MM_VERSION / MM_RESET 探针
 * 帧格式、寄存器、地址、顺序全部照厂商 aic8800_bsp，不自创。
 */
#include "WifiPrivate.h"

UINT8 gAicMessage[AIC_MSG_MAX];
UINT8 gAicRx[512];
UINT8 gAicFwBuffer[AIC_FW_CHUNK];

/*
 * 落点：adid/patch 两个地址**运行期从 patch 表里取**（见 AicPatchTableParse），
 * 这里的 0x0020B43C 只是"表里没有该字段"时的兜底，用兜底会打 `(fallback, ...)`。
 * 厂商也是这个顺序：先给硬编码默认值，再用表值覆盖（aic_bsp_driver.c:1417-1418）。
 */
/*
 * 评审 P0-1：patch 表落地不再是独立命令，而是 write confirm 的第 4 个阶段。
 * 顺序：adid → patch → fmacfw → patchtbl(100 写 + cfg) → start_app。
 */
AIC_FW_STAGE gAicStages[AIC_FW_STAGE_MAX] = {
    {"adid", AIC_FW_ADID, AIC_D80_U02_ADID_ADDR, 1708u, 1, AIC_STAGE_FILE},
    {"patch", AIC_FW_PATCH, AIC_D80_U02_PATCH_ADDR, 30040u, 1, AIC_STAGE_FILE},
    {"fmacfw", AIC_FW_FMAC, AIC_RAM_FMAC_FW_ADDR, 331348u, 1, AIC_STAGE_FILE},
    {"patchtbl", 0, 0u, 0u, 1, AIC_STAGE_PATCHTBL},
    {"start", 0, AIC_RAM_FMAC_FW_ADDR, 0u, 1, AIC_STAGE_START},
};

/* 失败态：0=未跑 1=全部成功 2=失败（失败时 gWifiFwStageIndex 指向停在哪一阶段） */
UINT32 gWifiFwState;
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
volatile UINT32 gAicPatchReady = 1u;
UINT32 gAicPatchState;
UINT32 gWifiFwStageIndex;
UINT32 gWifiFwSent;


AIC_PLAN_ITEM gAicPlan[AIC_PLAN_MAX];
/* MODE 那 9 个 pair 的最终值（pair 下标 0..8），自检用 */
UINT32 gAicModeValue[9];

void AicFwFail(const char *Stage, UINT32 Sent, const char *What) {
    gWifiFwState = 2u;
    SdhciMark("WiFi: stage=");
    SdhciMark(Stage);
    SdhciMark(" FAIL ");
    SdhciMark(What);
    SdhciMarkHex(" sent=", Sent, 8);
    SdhciMark("\n");
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

    UINT32 TableLength = 0;

    (void)Host;
    SdhciMarkDecimal("WiFi: fw check: buf=", AIC_FW_CHUNK);
    SdhciMark(" blk=1024 dir=" AIC_FW_DIR "\n");
    SdhciMark("WiFi: fw check: read-only (files only, no register write, no msg)\n");
    /*
     * 只读文件：把 patch 表读进来解析，好让下面 stage 行显示**表里的真实落点**。
     * 这一步不碰芯片（不发消息、不写寄存器），符合 P0-3 的"纯读检查"。
     */
    if (AicFwLoadPatchTable(Host, gAicFwBuffer, AIC_FW_CHUNK, &TableLength) != 0) {
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
            SdhciMarkHex(" bootaddr=", Stage->Address, 8);
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
        SdhciMarkHex(" -> ", Stage->Address, 8);
        SdhciMark(Stage->Required != 0 ? " (required)\n" : " (optional)\n");
    }
    SdhciMark("WiFi: fw check done, NOTHING WRITTEN (no reg write, no msg)\n");
    SdhciMark("WiFi: fw check: channel not probed here, run 'wififw id' for that\n");
    return 0;
}

/* 真写：检查 → 逐阶段写 → 启动 → 等首条消息，任一步失败即停 */
/*
 * MM_SET_STACK_START_REQ(0x7B)：告诉固件"主机的协议栈起来了"。
 * 厂商把它放在固件下载完之后的第一条（rwnx_main.c:6947），早于 MM_RESET；
 * 我们此前一条都不发 —— 真机现象是关联成功却只上送 beacon、EAPOL 不上送，
 * 与"固件按 BSP 模式（认为没有主机协议栈）行事"的画像一致。
 * 补发是实验性的：CFM 拿不到只打一行，不影响后续流程。
 */
static void AicFwSetStackStart(SDHCI_HOST *Host) {
    UINT8 Param[4];
    UINT8 Cfm[16];
    UINT32 CfmLength = 0;

    Param[0] = (UINT8)AIC_MM_STACK_START_ON;
    Param[1] = (UINT8)AIC_MM_EFUSE_INVALID;
    Param[2] = (UINT8)AIC_MM_VENDOR_INFO_5G_BIT;
    Param[3] = (UINT8)AIC_MM_FWTRACE_KEEP;
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_SET_STACK_START_REQ, (UINT16)AIC_TASK_MM, Param,
                         (UINT16)sizeof(Param)) != 0) {
        SdhciMark("WiFi: stack start: msg send FAILED\n");
        return;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_SET_STACK_START_CFM, Cfm, sizeof(Cfm),
                              &CfmLength, 4u, AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: stack start: no cfm (continue anyway)\n");
        return;
    }
    SdhciMarkHex("WiFi: stack start ok (5g=", (UINT32)Cfm[0], 2);
    SdhciMarkHex(" vendor=", (UINT32)(CfmLength > 1u ? Cfm[1] : 0u), 2);
    SdhciMark(")\n");
}

static int WifiFwWrite(SDHCI_HOST *Host) {
    UINT32 i;
    UINT32 Size = 0;
    int Rc;

    gWifiFwState = 0u;
    gWifiFwStageIndex = 0u;
    gWifiFwSent = 0u;
    SdhciMarkDecimal("WiFi: fw write CONFIRM buf=", AIC_FW_CHUNK);
    SdhciMark("\n");

    /* patch 表是必需件（D80 强制 info_len != 0）：先解析它，落点全部以表为准 */
    {
        UINT32 TableLength = 0;
        if (AicFwLoadPatchTable(Host, gAicFwBuffer, AIC_FW_CHUNK, &TableLength) != 0) {
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
            gWifiFwStageIndex = i;
            SdhciMark("WiFi: fw precheck failed, nothing written\n");
            return -1;
        }
    }
    for (i = 0; i < AIC_FW_STAGE_MAX; i++) {
        AIC_FW_STAGE *Stage = &gAicStages[i];
        gWifiFwStageIndex = i;
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
            UINT32 TableLength = 0;
            FAT_FILE_STAT Stat;
            if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) != FAT_OK || Stat.Size == 0u ||
                Stat.Size > AIC_FW_CHUNK ||
                AicFwReadChunk(AIC_FW_PATCH_TABLE, 0u, gAicFwBuffer, Stat.Size, &TableLength) != 0 ||
                TableLength != Stat.Size) {
                AicFwFail("patchtbl", 0u, "table-read");
                return -1;
            }
            if (AicPatchApply(Host, gAicFwBuffer, TableLength) != 0) {
                AicFwFail("patchtbl", 0u, "apply");
                return -1;
            }
            continue;
        }
        if (Stage->Kind == AIC_STAGE_START) {
            if (AicFwStartApp(Host, Stage->Address) != 0) {
                return -1;
            }
            AicFwWaitBootMessage(Host);
            continue;
        }
        if (AicFwPushStage(Host, Stage, Size) != 0) {
            return -1;
        }
    }
    gWifiFwState = 1u;
    SdhciMark("WiFi: fw state=done (all stages ok)\n");
    AicFwSetStackStart(Host);
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
            SdhciMarkDecimal("WiFi: fw failed at stage ", gWifiFwStageIndex + 1u);
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
