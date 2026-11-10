/*
 * Transfer.c — ksend 协议主循环（READY → 收块 → 收尾 → 校验 → OK/FAIL）
 *
 * 路径/选卷见 TransferPaths.c；校验/安装/回滚见 TransferFinish.c。
 * 顺序固定，不能换：
 *   备份 →（已有目标则）落到 .new 空表 READY → 收块 → flush → 校验 → 改名 → OK。
 * 校验不过绝不复位；--no-backup 失败不回滚。
 */
#include "Ksend/Private.h"
#include "Hal/KsendFreeze.h"
#include "Hal.h"
#include "HalPlatform.h"
#include "FileSystem.h"
#include "Fat.h"
#include "LwIp.h"
#include "Scheduler.h"
#include "Ksend/TransportPrivate.h"

/*
 * 挂到 FatSetIoBreath：WriteAt/扩链清簇时泵 ETH，避免 Duo 浅 RX 环在落盘
 * 静默期被广播占满后 HotSend 下一块 data 超时（串口 2MiB 已证 FAT 本身 OK）。
 */
static void KsendFatIoBreath(void) {
    if (LwIpActive()) {
        LwIpService();
    }
    if (!HalCpuIsHypervisor() && HalPlatformUseVirtDesktop()) {
        SchedulerIoBreath();
    } else {
        HalCpuRelax();
    }
    (void)TransportPollUserAbort();
}

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
        KsendImageClear(&gKsendOldImage);
        return FAT_OK;
    }
    if (Error != FAT_OK) {
        return Error;
    }
    if (Run->OldSize == 0u) {
        KsendImageClear(&gKsendOldImage);
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

static int TransferRun(KSEND_RUN *Run) {
    const KSEND_IMAGE *ReadyImage;
    UINT32 TransferStart;
    UINT32 PhaseStart;
    int Error;

    TransferStart = KsendMilliseconds();
    PhaseStart = TransferStart;
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
    Error = KsendStagingPrepare(Run);
    if (Error != FAT_OK) {
        Run->Failure = KSEND_FAIL_FILE_STATUS;
        Run->Error = Error;
        return -1;
    }
    Run->PrepareMilliseconds = KsendMilliseconds() - PhaseStart;
    KsendPhaseNote("prepare", Run->PrepareMilliseconds);
    if (KsendRunUsesStaging(Run)) {
        /* 空表 → 主机全量发；落盘路径是新建 .new（与 TEST2M 同形） */
        KsendImageClear(&gKsendNewImage);
        ReadyImage = &gKsendNewImage;
    } else {
        KsendImageCopy(&gKsendNewImage, &gKsendOldImage);
        ReadyImage = &gKsendOldImage;
    }
    PhaseStart = KsendMilliseconds();
    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MILLISECONDS, KSEND_DRAIN_LIMIT_MILLISECONDS);
    KsendReadyLine(ReadyImage);
    /* READY：SerialWrite 已 WaitPeerAcknowledge */
    if (!KsendTransportSendHealthy()) {
        KsendNote("transport: stalled sending READY");
        Run->Failure = KSEND_FAIL_SEND;
        return -1;
    }
    KsendBlockTable(ReadyImage);
    /* 表末：Report 内 WaitPeerAcknowledge；失败 → SendBroken */
    if (!KsendTransportSendHealthy()) {
        KsendNote("transport: stalled sending block table");
        Run->Failure = KSEND_FAIL_SEND;
        return -1;
    }
    Run->ReadyTableMilliseconds = KsendMilliseconds() - PhaseStart;
    KsendPhaseNote("ready_table", Run->ReadyTableMilliseconds);
    /* 表 ACK 后先泵 RX：PC 可能已发首块，Wi‑Fi 帧还在 SDPCM */
    if (LwIpActive()) {
        LwIpService();
        /* Duo：勿 IoBreath→Worker Halt（见 ReceiveBatch BreathWhileWired） */
        if (!HalCpuIsHypervisor() && HalPlatformUseVirtDesktop()) {
            SchedulerIoBreath();
        }
        LwIpService();
    }
    PhaseStart = KsendMilliseconds();
    if (KsendReceiveBlocks(Run) != 0) {
        Run->ReceiveMilliseconds = KsendMilliseconds() - PhaseStart;
        if (KsendTransportUserAborted()) {
            Run->Failure = KSEND_FAIL_ABORT;
            KsendNote("transport: aborted (Ctrl-C)");
        }
        return -1;
    }
    Run->ReceiveMilliseconds = KsendMilliseconds() - PhaseStart;
    KsendPhaseNoteReceive(Run->ReceiveMilliseconds, Run->Written);
    PhaseStart = KsendMilliseconds();
    Error = KsendFinishImage(Run);
    Run->FinishMilliseconds = KsendMilliseconds() - PhaseStart;
    if (Error == 0) {
        KsendPhaseNote("finish", Run->FinishMilliseconds);
        Run->TotalMilliseconds = KsendMilliseconds() - TransferStart;
        KsendPhaseNote("total", Run->TotalMilliseconds);
    }
    return Error;
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
    KsendFreezeBegin();
    FatSetIoBreath(KsendFatIoBreath);
    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MILLISECONDS, KSEND_DRAIN_LIMIT_MILLISECONDS);
    Rc = TransferRun(&Run);
    FatSetIoBreath(0);
    if (Rc == 0) {
        KsendVolumeRelease(&Run);
        KsendSuccessLine(&Run);
        if (Run.Reboot == 0) {
            KsendFreezeEnd();
            return 0;
        }
        /* 复位不关窗：新核冷启会重画 */
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
    KsendRollBack(&Run);
    KsendCacheDropAfterFailure(&Run);
    KsendVolumeRelease(&Run);
    KsendSerialDrainInput(KSEND_FAIL_DRAIN_QUIET_MILLISECONDS, KSEND_FAIL_DRAIN_LIMIT_MILLISECONDS);
    KsendFreezeEnd();
    return 1;
}
