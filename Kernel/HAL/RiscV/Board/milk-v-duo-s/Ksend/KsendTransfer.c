/*
 * KsendTransfer.c — ksend 协议主循环（READY → 收块 → 收尾 → 校验 → OK/FAIL）
 *
 * 顺序固定，不能换：
 *   1. 先备份并校验 KERNEL.BAK（旧块 CRC 顺手在复制时算出来）；
 *   2. 才发 READY + 旧块 CRC 表（PC 据此只发不同的块）；
 *   3. PC 每块等 'A'；'N' 就重发同一块（PC 侧最多 3 次）；
 *   4. 收尾帧带新文件整体 CRC；文件变小要补零（FAT 无 truncate）；
 *   5. flush → 逐块读回比对 → 最后才 KSEND OK 并复位。
 * 校验不过绝不复位：先把 BAK 写回去（KSEND RESTORED）再回 shell。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

static void RunReset(KSEND_RUN *Run) {
    Run->OldSize = 0;
    Run->NewSize = 0;
    Run->TrailerCrc = 0;
    Run->Written = 0;
    Run->FailBlock = 0;
    Run->FailWant = 0;
    Run->FailGot = 0;
    Run->Dirty = 0;
    Run->Failure = KSEND_FAIL_NONE;
    Run->Err = FAT_OK;
}

/* 期望的新内容以旧图打底：没被 PC 重发的块，新内容就等于旧内容 */
static void ImageCopy(KSEND_IMAGE *Destination, const KSEND_IMAGE *Source) {
    UINT32 Index;

    Destination->Size = Source->Size;
    Destination->BlockCount = Source->BlockCount;
    Destination->Crc = Source->Crc;
    for (Index = 0; Index < KSEND_MAX_BLOCKS; Index++) {
        Destination->BlockCrc[Index] = Source->BlockCrc[Index];
    }
}

/* 备份现有 Kernel.elf：复制 → 回读校验 → 得到 gKsendOldImage */
static int PrepareBackup(KSEND_RUN *Run) {
    UINT32 Block = 0;
    UINT32 Want = 0;
    UINT32 Got = 0;
    int Err;

    Err = KsendFileSize(KSEND_KERNEL_PATH, &Run->OldSize);
    if (Err == FAT_ERR_NOENT) {
        Run->OldSize = 0;
        return FAT_OK;
    }
    if (Err != FAT_OK) {
        return Err;
    }
    if (Run->OldSize == 0u) {
        return FAT_OK;
    }
    if (Run->OldSize > KSEND_MAX_BYTES) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    KsendNote("backup: Kernel.elf -> KERNEL.BAK");
    Err = KsendFileCopy(KSEND_KERNEL_PATH, KSEND_BACKUP_PATH, Run->OldSize,
                        &gKsendOldImage);
    if (Err != FAT_OK) {
        return Err;
    }
    if (KsendFileVerify(KSEND_BACKUP_PATH, Run->OldSize, &gKsendOldImage,
                        gKsendOldImage.Crc, &Block, &Want, &Got) != 1) {
        return FAT_ERR_IO;
    }
    KsendNote("backup: verified");
    return FAT_OK;
}

static int FinishImage(KSEND_RUN *Run) {
    UINT32 Block = 0;
    UINT32 Want = 0;
    UINT32 Got = 0;
    int Err;
    int Rc;

    if (Run->NewSize == 0u) {
        Run->Failure = KSEND_FAIL_EMPTY;
        return -1;
    }
    if (Run->NewSize > KSEND_MAX_BYTES) {
        Run->Failure = KSEND_FAIL_STAT;
        return -1;
    }
    if (Run->NewSize < Run->OldSize) {
        /* FAT 层没有 truncate：写 0 到新末尾之后，目录项尺寸仍是旧值 */
        KsendNote("note: FAT has no truncate; tail zeroed, file keeps old size");
        Err = KsendFileZeroRange(KSEND_KERNEL_PATH, Run->NewSize,
                                 Run->OldSize - Run->NewSize);
        if (Err != FAT_OK) {
            Run->Err = Err;
            Run->Failure = KSEND_FAIL_SHRINK;
            return -1;
        }
    }
    Err = FileSystemFileSync(KSEND_KERNEL_PATH);
    if (Err != FAT_OK) {
        Run->Err = Err;
        Run->Failure = KSEND_FAIL_FLUSH;
        return -1;
    }
    Rc = KsendFileVerify(KSEND_KERNEL_PATH, Run->NewSize, &gKsendNewImage,
                         Run->TrailerCrc, &Block, &Want, &Got);
    if (Rc != 1) {
        Run->Failure = KSEND_FAIL_VERIFY;
        Run->FailBlock = Block;
        Run->FailWant = Want;
        Run->FailGot = Got;
        return -1;
    }
    return 0;
}

static int TransferRun(KSEND_RUN *Run) {
    int Err;

    Err = PrepareBackup(Run);
    if (Err == FAT_ERR_FILE_TOO_BIG) {
        Run->Failure = KSEND_FAIL_STAT;
        Run->Err = Err;
        return -1;
    }
    if (Err != FAT_OK) {
        Run->Failure = KSEND_FAIL_BACKUP;
        Run->Err = Err;
        return -1;
    }
    ImageCopy(&gKsendNewImage, &gKsendOldImage);
    /* 发 READY 前把命令行尾巴（多敲的 Enter 等）吃掉，别让 PC 的块表错位 */
    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MS, KSEND_DRAIN_LIMIT_MS);
    KsendReadyLine(&gKsendOldImage);
    KsendBlockTable(&gKsendOldImage);
    if (KsendReceiveBlocks(Run) != 0) {
        return -1;
    }
    return FinishImage(Run);
}

/* 只有尝试过写卡才回滚；备份失败/dry-run 时 Kernel.elf 原封未动 */
static void RollBack(const KSEND_RUN *Run) {
    int Err;

    if (Run->Dirty == 0) {
        return;
    }
    if (Run->OldSize == 0u) {
        KsendNote("no backup: Kernel.elf did not exist before this run");
        return;
    }
    Err = KsendFileRestore(KSEND_BACKUP_PATH, KSEND_KERNEL_PATH,
                           &gKsendOldImage);
    if (Err == FAT_OK) {
        KsendRestoredLine(&gKsendOldImage);
    } else {
        KsendPrintReason("KSEND RESTORE FAIL", "restore", Err);
    }
}

int KsendTransfer(void) {
    KSEND_RUN Run;
    int Rc;

    RunReset(&Run);
    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MS, KSEND_DRAIN_LIMIT_MS);
    Rc = TransferRun(&Run);
    if (Rc == 0) {
        KsendOkLine(Run.TrailerCrc, Run.Written, Run.NewSize);
        /* 复位前必须等发送器真的空了，否则 KSEND OK 可能被截断 */
        KsendSerialDrainTransmitter();
        KsendReboot();
        return 0;
    }
    if (Run.Failure == KSEND_FAIL_ABORT) {
        KsendAbortLine(Run.Written);
    } else {
        KsendFailLine(Run.Failure, Run.FailBlock, Run.FailWant, Run.FailGot,
                      Run.Err);
    }
    RollBack(&Run);
    /* 失败后 PC 可能还在发 abort/重发块：吃掉，别让它漏进 shell 行 */
    KsendSerialDrainInput(KSEND_FAIL_DRAIN_QUIET_MS, KSEND_FAIL_DRAIN_LIMIT_MS);
    return 1;
}
