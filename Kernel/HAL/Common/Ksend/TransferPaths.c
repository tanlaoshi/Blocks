/*
 * TransferPaths.c — 目标/备份路径、选卷、旧图拷贝
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

static char sKsendTargetPath[KSEND_PATH_BYTES];
static char sKsendBackupPath[KSEND_PATH_BYTES];
static char sKsendWritePath[KSEND_PATH_BYTES];

void KsendRunReset(KSEND_RUN *Run) {
    Run->TargetPath = 0;
    Run->BackupPath = 0;
    Run->WritePath = 0;
    Run->OldSize = 0;
    Run->NewSize = 0;
    Run->ExpectedSize = 0;
    Run->TrailerCrc = 0;
    Run->Written = 0;
    Run->FailBlock = 0;
    Run->FailWant = 0;
    Run->FailGot = 0;
    Run->VolumeIndex = -1;
    Run->Reboot = 1;
    Run->Backup = 1;
    Run->DeferredFreeCluster = 0;
    Run->Dirty = 0;
    Run->Failure = KSEND_FAIL_NONE;
    Run->Error = FAT_OK;
    Run->PrepareMilliseconds = 0;
    Run->ReadyTableMilliseconds = 0;
    Run->ReceiveMilliseconds = 0;
    Run->NetMilliseconds = 0;
    Run->FlushMilliseconds = 0;
    Run->FinishMilliseconds = 0;
    Run->VerifyMilliseconds = 0;
    Run->InstallMilliseconds = 0;
    Run->InstallUnlinkMilliseconds = 0;
    Run->InstallRenameBakMilliseconds = 0;
    Run->InstallRenameNewMilliseconds = 0;
    Run->TotalMilliseconds = 0;
}

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

/* 同目录换叶名：D:Kernel.elf + KSEND.NEW → D:KSEND.NEW */
static int PathReplaceLeaf(const char *Target, const char *Leaf, char *Out) {
    const char *NameStart;
    UINT32 PrefixLength;
    UINT32 Index;
    UINT32 LeafIndex;

    if (Target == 0 || Target[0] == 0 || Leaf == 0 || Leaf[0] == 0) {
        return FAT_ERR_INVAL;
    }
    NameStart = PathNameStart(Target);
    PrefixLength = (UINT32)(NameStart - Target);
    for (Index = 0; Index < PrefixLength; Index++) {
        Out[Index] = Target[Index];
    }
    for (LeafIndex = 0; Leaf[LeafIndex] != 0; LeafIndex++) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        Out[Index++] = Leaf[LeafIndex];
    }
    Out[Index] = 0;
    return FAT_OK;
}

static int PathAppendExtension(const char *Target, const char *Extension,
                               char *Out) {
    const char *Walk;
    UINT32 Index = 0;

    if (Target == 0 || Target[0] == 0 || Extension == 0) {
        return FAT_ERR_INVAL;
    }
    while (Target[Index] != 0) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        Out[Index] = Target[Index];
        Index++;
    }
    for (Walk = Extension; *Walk != 0; Walk++) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        Out[Index++] = *Walk;
    }
    Out[Index] = 0;
    return FAT_OK;
}

static int BackupPathBuild(const char *Target, char *Backup) {
    return PathReplaceLeaf(Target, KSEND_BACKUP_LEAF_NAME, Backup);
}

static int StagingPathBuild(const char *Target, char *Staging) {
    return PathReplaceLeaf(Target, KSEND_STAGING_LEAF_NAME, Staging);
}

/* 删遗留 <目标>.new / <目标>.bak，收目录槽；不存在则忽略 */
static void LegacySiblingDelete(const char *Target, const char *Extension) {
    char LegacyPath[KSEND_PATH_BYTES];

    if (PathAppendExtension(Target, Extension, LegacyPath) != FAT_OK) {
        return;
    }
    (void)KsendFileDelete(LegacyPath);
}

static int VolumePrepare(const char *Target, KSEND_RUN *Run) {
    const char *Rel = 0;
    int Vol = -1;
    int Error = FileSystemResolve(Target, &Vol, &Rel);

    (void)Rel;
    if (Error != FAT_OK) {
        return Error;
    }
    Error = FileSystemSelectVolume(Vol);
    if (Error != FAT_OK) {
        return Error;
    }
    Run->VolumeIndex = Vol;
    return FAT_OK;
}

void KsendVolumeRelease(KSEND_RUN *Run) {
    if (Run->VolumeIndex >= 0) {
        (void)FileSystemSelectVolume(-1);
        Run->VolumeIndex = -1;
    }
}

int KsendPathsPrepare(const char *Requested, KSEND_RUN *Run) {
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
    Run->WritePath = sKsendTargetPath;
    return VolumePrepare(sKsendTargetPath, Run);
}

void KsendPathsNote(const KSEND_RUN *Run) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "target: ");
    KsendTextPut(&Out, Run->TargetPath);
    KsendTextPut(&Out, " write: ");
    KsendTextPut(&Out, Run->WritePath != 0 ? Run->WritePath : "-");
    KsendTextPut(&Out, " backup: ");
    KsendTextPut(&Out, Run->BackupPath);
    KsendNote(Line);
}

int KsendRunUsesStaging(const KSEND_RUN *Run) {
    if (Run == 0 || Run->WritePath == 0 || Run->TargetPath == 0) {
        return 0;
    }
    return Run->WritePath != Run->TargetPath ? 1 : 0;
}

int KsendStagingPrepare(KSEND_RUN *Run) {
    int Error;

    if (Run == 0 || Run->TargetPath == 0) {
        return FAT_ERR_INVAL;
    }
    if (Run->OldSize == 0u) {
        Run->WritePath = Run->TargetPath;
        return FAT_OK;
    }
    if (StagingPathBuild(Run->TargetPath, sKsendWritePath) != FAT_OK) {
        return FAT_ERR_INVAL;
    }
    LegacySiblingDelete(Run->TargetPath, KSEND_STAGING_EXTENSION);
    LegacySiblingDelete(Run->TargetPath, KSEND_BACKUP_EXTENSION);
    Error = KsendFileDelete(sKsendWritePath);
    if (Error != FAT_OK) {
        return Error;
    }
    Run->WritePath = sKsendWritePath;
    KsendNote("staging: write via KSEND.NEW (full new-file replace)");
    return FAT_OK;
}

void KsendImageCopy(KSEND_IMAGE *Destination, const KSEND_IMAGE *Source) {
    UINT32 Index;

    Destination->Size = Source->Size;
    Destination->BlockCount = Source->BlockCount;
    Destination->Crc = Source->Crc;
    for (Index = 0; Index < KSEND_MAX_BLOCKS; Index++) {
        Destination->BlockCrc[Index] = Source->BlockCrc[Index];
    }
}

void KsendImageClear(KSEND_IMAGE *Image) {
    UINT32 Index;

    if (Image == 0) {
        return;
    }
    Image->Size = 0;
    Image->BlockCount = 0;
    Image->Crc = KSEND_CRC_INITIAL;
    for (Index = 0; Index < KSEND_MAX_BLOCKS; Index++) {
        Image->BlockCrc[Index] = 0;
    }
}
