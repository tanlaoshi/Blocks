/*
 * KsendTransfer.c — ksend 协议主循环（READY → 收块 → 收尾 → 校验 → OK/FAIL）
 *
 * 顺序固定，不能换：
 *   1. 先备份并校验 <目标>.bak（旧块 CRC 顺手在复制时算出来）；
 *   2. 才发 READY + 旧块 CRC 表（PC 据此只发不同的块）；
 *   3. PC 每块等 'A'；'N' 就重发同一块（PC 侧最多 3 次）；
 *   4. 收尾帧带新文件整体 CRC；文件变小要补零（FAT 无 truncate）；
 *   5. flush → 逐块读回比对 → 最后才 KSEND OK 并（默认）复位。
 * 校验不过绝不复位：先把 .bak 写回去（KSEND RESTORED）再回 shell。
 *
 * v2：目标不再固定 Kernel.elf —— `ksend <路径>` 传什么写什么，备份名由目标
 * 全名追加 .bak（Kernel.elf → Kernel.elf.bak，boot.sd → boot.sd.bak）。
 * v3：BOOT: 前缀写启动分区；写前 SelectVolume 选中目标卷（boot 分区默认只读），
 * 成功/失败都在返回前归还选择；--no-reboot 时校验通过后留在 shell。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

/* 本次传输实际用的两个路径。Ksend 是同步命令，同一时刻只有一次传输。 */
static char sKsendTargetPath[KSEND_PATH_BYTES];
static char sKsendBackupPath[KSEND_PATH_BYTES];

static void RunReset(KSEND_RUN *Run) {
    Run->TargetPath = 0;
    Run->BackupPath = 0;
    Run->OldSize = 0;
    Run->NewSize = 0;
    Run->TrailerCrc = 0;
    Run->Written = 0;
    Run->FailBlock = 0;
    Run->FailWant = 0;
    Run->FailGot = 0;
    Run->VolIndex = -1;
    Run->Reboot = 1;
    Run->Dirty = 0;
    Run->Failure = KSEND_FAIL_NONE;
    Run->Err = FAT_OK;
}

/* 文件名起点：跳过 "BLOCKS:" 和目录部分（只借指针，不改入参） */
static const char *PathNameStart(const char *Path) {
    const char *Start = Path;
    const char *Walk;

    for (Walk = Path; *Walk != 0; Walk++) {
        if (*Walk == ':' || *Walk == '/') {
            Start = Walk + 1;
        }
    }
    return Start;
}

/* 原样拷贝，同时挡住空名/超长（板端缓冲 KSEND_PATH_BYTES） */
static int PathCopy(const char *Source, char *Destination) {
    UINT32 Index = 0;

    if (Source == 0 || Source[0] == 0) {
        return FAT_ERR_INVAL;
    }
    while (Source[Index] != 0) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        Destination[Index] = Source[Index];
        Index++;
    }
    Destination[Index] = 0;
    if (PathNameStart(Destination)[0] == 0) {
        return FAT_ERR_INVAL;
    }
    return FAT_OK;
}

/*
 * 目标全名 + .bak：boot.sd → boot.sd.bak，Kernel.elf → Kernel.elf.bak。
 * 同卷同目录、保留原大小写；FAT 名字比较本来就忽略大小写。
 */
static int BackupPathBuild(const char *Target, char *Backup) {
    const char *Walk;
    UINT32 Index = 0;

    if (Target == 0 || Target[0] == 0) {
        return FAT_ERR_INVAL;
    }
    while (Target[Index] != 0) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        Backup[Index] = Target[Index];
        Index++;
    }
    for (Walk = KSEND_BACKUP_EXTENSION; *Walk != 0; Walk++) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        Backup[Index++] = *Walk;
    }
    Backup[Index] = 0;
    return FAT_OK;
}

/*
 * 选中目标卷再写（PR-FS-vol）：boot 分区挂载时被策略标成只读，
 * FileSystemSelectVolume() 是唯一放行入口。选卷是进程级全局状态，
 * Ksend 只在 shell 单线程里跑；返回前 VolumeRelease() 归还。
 */
static int VolumePrepare(const char *Target, KSEND_RUN *Run) {
    const char *Rel = 0;
    int Vol = -1;
    int Err = FileSystemResolve(Target, &Vol, &Rel);

    (void)Rel;
    if (Err != FAT_OK) {
        return Err;
    }
    Err = FileSystemSelectVolume(Vol);
    if (Err != FAT_OK) {
        return Err;
    }
    Run->VolIndex = Vol;
    return FAT_OK;
}

static void VolumeRelease(KSEND_RUN *Run) {
    if (Run->VolIndex >= 0) {
        (void)FileSystemSelectVolume(-1);
        Run->VolIndex = -1;
    }
}

/* 选定本次的目标/备份路径并选卷；不合法/卷不存在都在写卡前挡下 */
static int PathsPrepare(const char *Requested, KSEND_RUN *Run) {
    const char *Target = Requested;

    if (Target == 0 || Target[0] == 0) {
        Target = KSEND_KERNEL_PATH;
    }
    if (PathCopy(Target, sKsendTargetPath) != FAT_OK) {
        return FAT_ERR_INVAL;
    }
    if (BackupPathBuild(sKsendTargetPath, sKsendBackupPath) != FAT_OK) {
        return FAT_ERR_INVAL;
    }
    Run->TargetPath = sKsendTargetPath;
    Run->BackupPath = sKsendBackupPath;
    return VolumePrepare(sKsendTargetPath, Run);
}

static void NotePaths(const KSEND_RUN *Run) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "target: ");
    KsendTextPut(&Out, Run->TargetPath);
    KsendTextPut(&Out, " backup: ");
    KsendTextPut(&Out, Run->BackupPath);
    KsendNote(Line);
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

/* 备份现有目标：复制 → 回读校验 → 得到 gKsendOldImage */
static int PrepareBackup(KSEND_RUN *Run) {
    UINT32 Block = 0;
    UINT32 Want = 0;
    UINT32 Got = 0;
    int Err;

    Err = KsendFileSize(Run->TargetPath, &Run->OldSize);
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
    Err = KsendFileCopy(Run->TargetPath, Run->BackupPath, Run->OldSize,
                        &gKsendOldImage);
    if (Err != FAT_OK) {
        return Err;
    }
    if (KsendFileVerify(Run->BackupPath, Run->OldSize, &gKsendOldImage,
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
        Err = KsendFileZeroRange(Run->TargetPath, Run->NewSize,
                                 Run->OldSize - Run->NewSize);
        if (Err != FAT_OK) {
            Run->Err = Err;
            Run->Failure = KSEND_FAIL_SHRINK;
            return -1;
        }
    }
    Err = FileSystemFileSync(Run->TargetPath);
    if (Err != FAT_OK) {
        Run->Err = Err;
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

/* 只有尝试过写卡才回滚；备份失败/dry-run 时目标文件原封未动 */
static void RollBack(const KSEND_RUN *Run) {
    int Err;

    if (Run->Dirty == 0) {
        return;
    }
    if (Run->OldSize == 0u) {
        KsendNote("no backup: target did not exist before this run");
        return;
    }
    Err = KsendFileRestore(Run->BackupPath, Run->TargetPath, &gKsendOldImage);
    if (Err == FAT_OK) {
        KsendRestoredLine(&gKsendOldImage);
    } else {
        KsendPrintReason("KSEND RESTORE FAIL", "restore", Err);
    }
}

int KsendTransfer(const char *TargetPath, int Reboot) {
    KSEND_RUN Run;
    int Rc;
    int Err;

    RunReset(&Run);
    Run.Reboot = Reboot ? 1 : 0;
    Err = PathsPrepare(TargetPath, &Run);
    if (Err != FAT_OK) {
        Run.Failure = KSEND_FAIL_STAT;
        Run.Err = Err;
        KsendFailLine(Run.Failure, Run.FailBlock, Run.FailWant, Run.FailGot,
                      Run.Err);
        return 1;
    }
    NotePaths(&Run);
    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MS, KSEND_DRAIN_LIMIT_MS);
    Rc = TransferRun(&Run);
    if (Rc == 0) {
        /* 校验已过；先归还维护卷，再打成功行，别把可写窗口拖到串口之后 */
        VolumeRelease(&Run);
        KsendOkLine(Run.TrailerCrc, Run.Written, Run.NewSize);
        if (Run.Reboot == 0) {
            return 0; /* deploy.sh：复位留给操作者 */
        }
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
    /* 回滚要用选中的卷（boot 分区只读），必须排在 VolumeRelease 之前 */
    RollBack(&Run);
    VolumeRelease(&Run);
    /* 失败后 PC 可能还在发 abort/重发块：吃掉，别让它漏进 shell 行 */
    KsendSerialDrainInput(KSEND_FAIL_DRAIN_QUIET_MS, KSEND_FAIL_DRAIN_LIMIT_MS);
    return 1;
}
