/*
 * Transfer.c — ksend 协议主循环（READY → 收块 → 收尾 → 校验 → OK/FAIL）
 *
 * 路径/选卷见 TransferPaths.c。顺序固定，不能换：
 *   备份 → READY+旧 CRC 表 → 收差量块 → flush → 读回校验 → OK（可选复位）。
 * 校验不过绝不复位；--no-backup 失败不回滚。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

static int PrepareBackup(KSEND_RUN *Run) {
    UINT32 Block = 0;
    UINT32 Want = 0;
    UINT32 Got = 0;
    int Error;

    if (Run->Backup == 0) {
        KsendNote("backup: skipped (--no-backup)");
    }
    Error = KsendFileSize(Run->TargetPath, &Run->OldSize);
    if (Error == FAT_ERR_NOENT) {
        Run->OldSize = 0;
        return FAT_OK;
    }
    if (Error != FAT_OK) {
        return Error;
    }
    if (Run->OldSize == 0u) {
        return FAT_OK;
    }
    if (Run->OldSize > KSEND_MAX_BYTES) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    if (Run->Backup == 0) {
        Error = KsendCacheLoad(Run->TargetPath, Run->OldSize, &gKsendOldImage);
        if (Error == FAT_OK) {
            KsendNote("cache: hit (old block table from .crc)");
            return FAT_OK;
        }
        KsendNote("cache: miss (scanning old file)");
        return KsendFileScan(Run->TargetPath, Run->OldSize, &gKsendOldImage);
    }
    Error = KsendFileCopy(Run->TargetPath, Run->BackupPath, Run->OldSize,
                        &gKsendOldImage);
    if (Error != FAT_OK) {
        return Error;
    }
    if (KsendFileVerify(Run->BackupPath, Run->OldSize, &gKsendOldImage,
                        gKsendOldImage.Crc, &Block, &Want, &Got) != 1) {
        return FAT_ERR_IO;
    }
    KsendNote("backup: verified");
    return FAT_OK;
}

static void CacheStoreAfterVerify(KSEND_RUN *Run) {
    UINT32 Size = 0;

    if (KsendFileSize(Run->TargetPath, &Size) != FAT_OK) {
        if (KsendCacheDrop(Run->TargetPath) == FAT_OK) {
            KsendNote("cache: dropped (stat failed)");
        } else {
            KsendNote("cache: drop failed after stat error");
        }
        return;
    }
    if (Size != Run->NewSize) {
        if (KsendCacheDrop(Run->TargetPath) == FAT_OK) {
            KsendNote("cache: dropped (target keeps old size)");
        } else {
            KsendNote("cache: drop failed (target keeps old size)");
        }
        return;
    }
    gKsendNewImage.Size = Run->NewSize;
    gKsendNewImage.BlockCount =
        (Run->NewSize + KSEND_BLOCK_BYTES - 1u) / KSEND_BLOCK_BYTES;
    gKsendNewImage.Crc = Run->TrailerCrc;
    if (KsendCacheSave(Run->TargetPath, &gKsendNewImage) != FAT_OK) {
        KsendNote("cache: write failed (next transfer scans)");
        return;
    }
    KsendNote("cache: updated");
}

static void CacheDropAfterFailure(const KSEND_RUN *Run) {
    if (Run->Dirty == 0) {
        return;
    }
    if (KsendCacheDrop(Run->TargetPath) == FAT_OK) {
        KsendNote("cache: dropped (failed write; next transfer scans)");
    } else {
        KsendNote("cache: drop failed after write failure");
    }
}

static int FinishImage(KSEND_RUN *Run) {
    UINT32 Block = 0;
    UINT32 Want = 0;
    UINT32 Got = 0;
    int Error;
    int Rc;

    if (Run->NewSize == 0u) {
        Run->Failure = KSEND_FAIL_EMPTY;
        return -1;
    }
    if (Run->NewSize > KSEND_MAX_BYTES) {
        Run->Failure = KSEND_FAIL_FILE_STATUS;
        return -1;
    }
    if (Run->NewSize < Run->OldSize) {
        KsendNote("note: FAT has no truncate; tail zeroed, file keeps old size");
        Error = KsendFileZeroRange(Run->TargetPath, Run->NewSize,
                                 Run->OldSize - Run->NewSize);
        if (Error != FAT_OK) {
            Run->Error = Error;
            Run->Failure = KSEND_FAIL_SHRINK;
            return -1;
        }
    }
    Error = FileSystemFileSync(Run->TargetPath);
    if (Error != FAT_OK) {
        Run->Error = Error;
        Run->Failure = KSEND_FAIL_FLUSH;
        return -1;
    }
    Rc = KsendFileVerify(Run->TargetPath, Run->NewSize, &gKsendNewImage,
                         Run->TrailerCrc, &Block, &Want, &Got);
    if (Rc != 1) {
        Run->Failure = KSEND_FAIL_VERIFY;
        Run->FailBlock = Block;
        Run->FailWant = Want;
        Run->FailGot = Got;
        return -1;
    }
    CacheStoreAfterVerify(Run);
    return 0;
}

static int TransferRun(KSEND_RUN *Run) {
    int Error;

    Error = PrepareBackup(Run);
    if (Error == FAT_ERR_FILE_TOO_BIG) {
        Run->Failure = KSEND_FAIL_FILE_STATUS;
        Run->Error = Error;
        return -1;
    }
    if (Error != FAT_OK) {
        Run->Failure = KSEND_FAIL_BACKUP;
        Run->Error = Error;
        return -1;
    }
    KsendImageCopy(&gKsendNewImage, &gKsendOldImage);
    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MILLISECONDS, KSEND_DRAIN_LIMIT_MILLISECONDS);
    KsendReadyLine(&gKsendOldImage);
    KsendBlockTable(&gKsendOldImage);
    if (KsendReceiveBlocks(Run) != 0) {
        return -1;
    }
    return FinishImage(Run);
}

static void RollBack(const KSEND_RUN *Run) {
    int Error;

    if (Run->Dirty == 0) {
        return;
    }
    if (Run->Backup == 0) {
        KsendNote("rollback: skipped (--no-backup); target may be partial");
        return;
    }
    if (Run->OldSize == 0u) {
        KsendNote("no backup: target did not exist before this run");
        return;
    }
    Error = KsendFileRestore(Run->BackupPath, Run->TargetPath, &gKsendOldImage);
    if (Error == FAT_OK) {
        KsendRestoredLine(&gKsendOldImage);
    } else {
        KsendPrintReason("KSEND RESTORE FAIL", "restore", Error);
    }
}

int KsendTransfer(const char *TargetPath, int Reboot, int Backup) {
    KSEND_RUN Run;
    int Rc;
    int Error;

    KsendRunReset(&Run);
    Run.Reboot = Reboot ? 1 : 0;
    Run.Backup = Backup ? 1 : 0;
    Error = KsendPathsPrepare(TargetPath, &Run);
    if (Error != FAT_OK) {
        Run.Failure = KSEND_FAIL_FILE_STATUS;
        Run.Error = Error;
        KsendFailLine(Run.Failure, Run.FailBlock, Run.FailWant, Run.FailGot,
                      Run.Error);
        return 1;
    }
    KsendPathsNote(&Run);
    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MILLISECONDS, KSEND_DRAIN_LIMIT_MILLISECONDS);
    Rc = TransferRun(&Run);
    if (Rc == 0) {
        KsendVolumeRelease(&Run);
        KsendSuccessLine(Run.TrailerCrc, Run.Written, Run.NewSize);
        if (Run.Reboot == 0) {
            return 0;
        }
        KsendSerialDrainTransmitter();
        KsendReboot();
        return 0;
    }
    if (Run.Failure == KSEND_FAIL_ABORT) {
        KsendAbortLine(Run.Written);
    } else {
        KsendFailLine(Run.Failure, Run.FailBlock, Run.FailWant, Run.FailGot,
                      Run.Error);
    }
    RollBack(&Run);
    CacheDropAfterFailure(&Run);
    KsendVolumeRelease(&Run);
    KsendSerialDrainInput(KSEND_FAIL_DRAIN_QUIET_MILLISECONDS, KSEND_FAIL_DRAIN_LIMIT_MILLISECONDS);
    return 1;
}
