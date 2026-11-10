/*
 * TransferFinish.c — 校验 / 安装 .new / 失败回滚与 .crc 收尾
 *
 * PR-KSEND-PHASE-FLUSH：VerifyMilliseconds = sync+盘验；
 * InstallMilliseconds = staging 改名 + .crc。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

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

void KsendCacheDropAfterFailure(const KSEND_RUN *Run) {
    if (Run->Dirty == 0) {
        return;
    }
    /* 仍停在 .new：Target 未改，保留 .crc */
    if (KsendRunUsesStaging(Run)) {
        return;
    }
    if (KsendCacheDrop(Run->TargetPath) == FAT_OK) {
        KsendNote("cache: dropped (failed write; next transfer scans)");
    } else {
        KsendNote("cache: drop failed after write failure");
    }
}

int KsendFinishImage(KSEND_RUN *Run) {
    const char *Path = Run->WritePath;
    UINT32 Block = 0;
    UINT32 Want = 0;
    UINT32 Got = 0;
    UINT32 VerifyStart;
    UINT32 InstallStart;
    int Error;
    int Rc;

    if (Path == 0) {
        Run->Failure = KSEND_FAIL_FILE_STATUS;
        return -1;
    }
    if (Run->NewSize == 0u) {
        Run->Failure = KSEND_FAIL_EMPTY;
        return -1;
    }
    if (Run->NewSize > KSEND_MAX_BYTES) {
        Run->Failure = KSEND_FAIL_FILE_STATUS;
        return -1;
    }
    VerifyStart = KsendMilliseconds();
    /* .new 是空起的新建文件，无旧尾巴；只对直接覆写 Target 补零 */
    if (!KsendRunUsesStaging(Run) && Run->NewSize < Run->OldSize) {
        KsendNote("note: FAT has no truncate; tail zeroed, file keeps old size");
        Error = KsendFileZeroRange(Path, Run->NewSize,
                                 Run->OldSize - Run->NewSize);
        if (Error != FAT_OK) {
            Run->VerifyMilliseconds = KsendMilliseconds() - VerifyStart;
            Run->Error = Error;
            Run->Failure = KSEND_FAIL_SHRINK;
            return -1;
        }
    }
    Error = FileSystemFileSync(Path);
    if (Error != FAT_OK) {
        Run->VerifyMilliseconds = KsendMilliseconds() - VerifyStart;
        Run->Error = Error;
        Run->Failure = KSEND_FAIL_FLUSH;
        return -1;
    }
    Rc = KsendFileVerify(Path, Run->NewSize, &gKsendNewImage,
                         Run->TrailerCrc, &Block, &Want, &Got);
    Run->VerifyMilliseconds = KsendMilliseconds() - VerifyStart;
    if (Rc != 1) {
        Run->Failure = KSEND_FAIL_VERIFY;
        Run->FailBlock = Block;
        Run->FailWant = Want;
        Run->FailGot = Got;
        return -1;
    }
    InstallStart = KsendMilliseconds();
    Error = KsendStagingInstall(Run);
    if (Error != FAT_OK) {
        Run->InstallMilliseconds = KsendMilliseconds() - InstallStart;
        Run->Error = Error;
        Run->Failure = KSEND_FAIL_FILE_STATUS;
        return -1;
    }
    CacheStoreAfterVerify(Run);
    Run->InstallMilliseconds = KsendMilliseconds() - InstallStart;
    return 0;
}

void KsendRollBack(const KSEND_RUN *Run) {
    UINT32 TargetSize = 0;
    int Error;
    int Staging = KsendRunUsesStaging(Run);

    if (Run->Dirty == 0 && !Staging) {
        return;
    }
    KsendStagingDiscard(Run);
    if (Staging) {
        /* 收块只动 .new；Install 半截才可能弄丢 Target */
        if (KsendFileSize(Run->TargetPath, &TargetSize) == FAT_OK) {
            KsendNote("rollback: target untouched (staging)");
            return;
        }
        /* SESSION-TAX：Install 已 Target→.bak 时优先改名拉回 */
        if (Run->Backup != 0 && Run->OldSize != 0u &&
            FileSystemRename(Run->BackupPath, Run->TargetPath) == FAT_OK) {
            KsendNote("rollback: restored target from .bak rename");
            KsendRestoredLine(&gKsendOldImage);
            return;
        }
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
