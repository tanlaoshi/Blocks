/*
 * TransferStaging.c — .new 安装 / 延后释链 / 丢弃
 *
 * install：目录改名 + 摘旧项；FatFreeChain 延到 OK 后（KsendDeferredFreeClusters）。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"
#include "Fat.h"

static int StagingUnlinkDeferFree(const char *Path, UINT32 *OutCluster) {
    FAT_FILE_STAT Stat;
    const char *Relative = 0;
    int Volume = -1;
    int Error;

    *OutCluster = 0;
    Error = FileSystemFileStat(Path, &Stat);
    if (Error == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    if (Error != FAT_OK) {
        return Error;
    }
    if (Stat.Attr & FAT_ATTR_DIR) {
        return FAT_ERR_ISDIR;
    }
    Error = FileSystemResolve(Path, &Volume, &Relative);
    if (Error != FAT_OK || Relative == 0 || Relative[0] == 0) {
        return Error != FAT_OK ? Error : FAT_ERR_INVAL;
    }
    Error = FatUnlinkKeepClusters(Relative);
    if (Error != FAT_OK) {
        return Error;
    }
    if (Stat.Cluster >= 2u) {
        *OutCluster = Stat.Cluster;
    }
    return FAT_OK;
}

int KsendStagingInstall(KSEND_RUN *Run) {
    UINT32 DeferredCluster = 0;
    int Error;

    if (!KsendRunUsesStaging(Run)) {
        return FAT_OK;
    }
    /*
     * SESSION-TAX：Backup 时 Target→.bak，再 .new→Target。
     * 旧 .bak / --no-backup 的 Target：只摘项，释链见 DeferredFree。
     */
    if (Run->Backup != 0) {
        Error = StagingUnlinkDeferFree(Run->BackupPath, &DeferredCluster);
        if (Error != FAT_OK) {
            return Error;
        }
        Error = FileSystemRename(Run->TargetPath, Run->BackupPath);
        if (Error != FAT_OK) {
            if (DeferredCluster >= 2u) {
                (void)FatFreeChain(DeferredCluster);
            }
            return Error;
        }
        KsendNote("staging: target renamed to .bak");
    } else {
        Error = StagingUnlinkDeferFree(Run->TargetPath, &DeferredCluster);
        if (Error != FAT_OK) {
            return Error;
        }
        KsendNote("staging: target unlinked (--no-backup)");
    }
    Error = FileSystemRename(Run->WritePath, Run->TargetPath);
    if (Error != FAT_OK) {
        if (DeferredCluster >= 2u) {
            (void)FatFreeChain(DeferredCluster);
        }
        return Error;
    }
    Run->WritePath = Run->TargetPath;
    Run->DeferredFreeCluster = DeferredCluster;
    KsendNote("staging: installed .new over target");
    return FAT_OK;
}

void KsendDeferredFreeClusters(KSEND_RUN *Run) {
    UINT32 Cluster;

    if (Run == 0 || Run->DeferredFreeCluster < 2u) {
        return;
    }
    Cluster = Run->DeferredFreeCluster;
    Run->DeferredFreeCluster = 0;
    if (FatFreeChain(Cluster)) {
        KsendNote("staging: deferred free done");
    } else {
        KsendNote("staging: deferred free failed");
    }
}

void KsendStagingDiscard(const KSEND_RUN *Run) {
    if (!KsendRunUsesStaging(Run)) {
        return;
    }
    if (KsendFileDelete(Run->WritePath) == FAT_OK) {
        KsendNote("staging: discarded .new");
    } else {
        KsendNote("staging: discard .new failed");
    }
}
