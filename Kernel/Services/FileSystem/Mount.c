/*
 * Mount.c — 扫描并挂上各卷（story-s3 / PR-F-fs-1）
 *
 * 稳态 S3 挂载编排：
 *   MountAllVolumes — Drive 扫 → GPT FAT → MountVolume → RES → 默认卷
 *   FileSystemRemountVolumes — 热拔后：BlockInitialize 重 Probe + MountAll
 *     勿 HalBlockInitialize（冲 BlockMux / MSC 盘号）
 * 开机两轮 / Policy：FileSystem.c。热拔触发：DesktopWorkerTick（Clock.c）。
 * 单卷实现：MountVolume.c；默认卷：MountPick.c。
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "BlockMux.h"
#include "Gpt.h"
#include "LibWrite.h"
#include "SerialLog.h"
#include "HalDevices.h"

/*
 * 扫描所有 Block 盘，挂上每个盘上的全部 FAT 分区；
 * 含 BLOCKS.ID 的皆名 BLOCKS；默认卷跟 OsTarget（ROOT.CFG / BootDiskGuid）。
 */
int MountAllVolumes(void) {
    UINT32 d;
    UINT8 Tmp[64];
    int BlocksVol = -1;

    gVolCount = 0;
    gDefaultVol = 0;
    gActiveVol = -1;
    gActiveOps = 0;
    gActiveDrive = 0xFFFFFFFFu;
    gActiveLba = 0xFFFFFFFFu;
    /* OsTarget 钉在 MountOs：此处只清卷表；勿清 gOsDiskOk（热拔会误认旁路盘） */
    gOsVolIdx = -1;

    for (d = 0; d < BLOCK_MAX_DRIVES && gVolCount < FS_MAX_VOLUMES; d++) {
        GPT_FAT_PART Parts[GPT_MAX_FAT_PARTS];
        int N;
        int p;

        if (!BlockSelect(d)) {
            continue;
        }
        N = GptFindAllFat(Parts, GPT_MAX_FAT_PARTS);
        if (N <= 0) {
            continue;
        }
        for (p = 0; p < N; p++) {
            FsMountOneFatPart(d, &Parts[p], &BlocksVol);
        }
    }

    FsMountAddResVolume();

    /*
     * usb-boot-unplug：即便 0 卷也要 UpdateBootMedium（空表 + 曾见/钉启动盘 → Gone）。
     * 旧逻辑在 gVolCount==0 时直接 return，拔光后永不 Park。
     */
    if (gVolCount <= 0) {
        FsUpdateBootMediumState();
        return 0;
    }
    FsResolveOsTarget(&BlocksVol);
    FsMountPickDefault(BlocksVol, Tmp, sizeof(Tmp));
    if (gOsVolIdx >= 0 && gOsVolIdx < gVolCount) {
        gDefaultVol = gOsVolIdx;
    }
    if (FileSystemActivate(gDefaultVol) != FAT_OK) {
        FsUpdateBootMediumState();
        return 0;
    }

    LogFs("Fs: Mounted ");
    {
        char Msg[8];
        Msg[0] = (char)('0' + (gVolCount > 9 ? 9 : gVolCount));
        Msg[1] = 0;
        LogFs(Msg);
    }
    LogFs(" volume(s), default=");
    LogFs(gVols[gDefaultVol].Name);
    LogFs("\n");
    FsUpdateBootMediumState();
    return 1;
}

int FileSystemRemountVolumes(void) {
    int Ok;

    /*
     * 勿再 HalBlockInitialize()/ProbeClass：会重绑 AHCI 冲掉已装的 BlockMux。
     * 后端已在时只 BlockInitialize 重 Probe（Mux 会挂上 MSC 盘号）。
     * Secondary/热拔在 Blocks> 之后：静音 block:/FS: FAT，免刷进提示符后。
     */
    LibWriteSetQuiet(1);
    if (!BlockBackendReady()) {
        if (HalBlockInitialize() <= 0) {
            LibWriteSetQuiet(0);
            LogFs("Fs: Remount No Block Backend\n");
            return 0;
        }
    } else if (BlockInitialize() <= 0) {
        LibWriteSetQuiet(0);
        LogFs("Fs: Remount BlockInitialize Found 0 Drives\n");
        return 0;
    }
    Ok = MountAllVolumes();
    LibWriteSetQuiet(0);
    if (!Ok) {
        LogFs("Fs: Remount No Volumes\n");
        /* MountAll 已 UpdateBootMedium；此处再调幂等，保 Gone 可见 */
        FsUpdateBootMediumState();
        return 0;
    }
    return 1;
}
