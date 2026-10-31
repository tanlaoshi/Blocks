/*
 * KsendTransferPaths.c — 目标/备份路径、选卷、旧图拷贝
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

static char sKsendTargetPath[KSEND_PATH_BYTES];
static char sKsendBackupPath[KSEND_PATH_BYTES];

void KsendRunReset(KSEND_RUN *Run) {
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
    Run->Backup = 1;
    Run->Dirty = 0;
    Run->Failure = KSEND_FAIL_NONE;
    Run->Err = FAT_OK;
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

void KsendVolumeRelease(KSEND_RUN *Run) {
    if (Run->VolIndex >= 0) {
        (void)FileSystemSelectVolume(-1);
        Run->VolIndex = -1;
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
    return VolumePrepare(sKsendTargetPath, Run);
}

void KsendPathsNote(const KSEND_RUN *Run) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "target: ");
    KsendTextPut(&Out, Run->TargetPath);
    KsendTextPut(&Out, " backup: ");
    KsendTextPut(&Out, Run->BackupPath);
    KsendNote(Line);
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
