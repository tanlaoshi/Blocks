/*
 * OsTargetResolve.c — ROOT.CFG / OsTarget 钉死与重绑（fs-msc-defer）
 *
 * 自 MountOs.c 纯搬。启动盘优先；勿把旁路 BLOCKS 误钉成 OsTarget。
 */
#include "FileSystemPrivate.h"
#include "BootHandoff.h"
#include "SerialLog.h"

static int VolumeIsEspName(const FS_VOLUME *Volume) {
    return Volume && Volume->Name[0] == 'E' && Volume->Name[1] == 'S' &&
           Volume->Name[2] == 'P';
}

static int VolumeOnBootDisk(const FS_VOLUME *Volume) {
    const BOOT_HANDOFF *Info = BootHandoffGet();

    if (!Volume || !Volume->DiskGuidOk || !Info || !Info->BootDiskGuidOk) {
        return 0;
    }
    return GuidEqual16(Volume->DiskGuid, Info->BootDiskGuid);
}

static int FindVolumeByDiskPart(const UINT8 Disk[16], const UINT8 Part[16]) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].DiskGuidOk || !gVols[i].PartGuidOk) {
            continue;
        }
        if (GuidEqual16(gVols[i].DiskGuid, Disk) &&
            GuidEqual16(gVols[i].PartGuid, Part)) {
            return i;
        }
    }
    return -1;
}

static int FindBlocksOnDisk(const UINT8 Disk[16]) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].HasBlocksId || !gVols[i].DiskGuidOk) {
            continue;
        }
        if (GuidEqual16(gVols[i].DiskGuid, Disk)) {
            return i;
        }
    }
    return -1;
}

static void PinOsTarget(const UINT8 Disk[16], const UINT8 Part[16], int HavePart) {
    const BOOT_HANDOFF *Info = BootHandoffGet();

    CopyGuid16(gOsDiskGuid, Disk);
    gOsDiskOk = 1;
    if (HavePart) {
        CopyGuid16(gOsPartGuid, Part);
        gOsPartOk = 1;
    } else {
        gOsPartOk = 0;
    }
    gOsTargetPinned = 1;
    /*
     * usb-boot-unplug：OsTarget 钉在加载 EFI 的盘上即视为「见过启动盘」。
     * 勿只靠卷表 DiskGuidOk——GUID 未填时 UpdateBootMedium 永不置 Seen，拔棒不 halt。
     */
    if (Info && Info->BootDiskGuidOk && GuidEqual16(Disk, Info->BootDiskGuid)) {
        gBootDiskSeen = 1;
    }
}

static void RebindPinnedOsTarget(int *BlocksVol) {
    gOsVolIdx = -1;
    if (!gOsDiskOk) {
        return;
    }
    if (gOsPartOk) {
        gOsVolIdx = FindVolumeByDiskPart(gOsDiskGuid, gOsPartGuid);
    }
    if (gOsVolIdx < 0) {
        gOsVolIdx = FindBlocksOnDisk(gOsDiskGuid);
    }
    if (gOsVolIdx >= 0 && gVols[gOsVolIdx].HasBlocksId && BlocksVol) {
        *BlocksVol = gOsVolIdx;
    }
    if (gOsVolIdx >= 0) {
        LogFs("FS: OsTarget rebind ok\n");
    } else {
        LogFs("FS: OsTarget pinned (volume unplugged)\n");
    }
}

static int AnyVolumeOnBootDisk(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (VolumeOnBootDisk(&gVols[i])) {
            return 1;
        }
    }
    return 0;
}

void FsResolveOsTarget(int *BlocksVol) {
    int i;
    UINT8 Disk[16];
    UINT8 Part[16];
    int FromConfig = 0;
    const BOOT_HANDOFF *Info = BootHandoffGet();
    int BootGuidOk = (Info && Info->BootDiskGuidOk) ? 1 : 0;

    gOsVolIdx = -1;

    /*
     * 已钉死：通常只重绑。
     * 例外：U 盘起时首轮只挂到 NVMe，曾误把旁路 BLOCKS Pin 成 OsTarget——
     * 启动盘现已挂上则作废重认（否则拔 U 盘仍 rebind 到 NUC、永不 halt）。
     */
    if (gOsTargetPinned && gOsDiskOk) {
        if (BootGuidOk && !GuidEqual16(gOsDiskGuid, Info->BootDiskGuid) &&
            AnyVolumeOnBootDisk()) {
            LogFs("FS: OsTarget unpin (was not boot disk; boot disk now mounted)\n");
            gOsTargetPinned = 0;
            gOsDiskOk = 0;
            gOsPartOk = 0;
        } else {
            RebindPinnedOsTarget(BlocksVol);
            return;
        }
    }

    gOsDiskOk = 0;
    gOsPartOk = 0;

    /* 1) 只认启动盘上的 ESP ROOT.CFG（避免旁路盘指针抢 OsTarget） */
    if (BootGuidOk) {
        for (i = 0; i < gVolCount; i++) {
            if (!VolumeIsEspName(&gVols[i]) || !VolumeOnBootDisk(&gVols[i])) {
                continue;
            }
            if (TryRootConfigFromVolume(i, Disk, Part)) {
                FromConfig = 1;
                LogFs("FS: ROOT.CFG loaded (boot disk ESP)\n");
                break;
            }
        }
    }

    /* 2) 旧 Boot 无 GUID：才扫任意 ESP */
    if (!FromConfig && !BootGuidOk) {
        for (i = 0; i < gVolCount; i++) {
            if (!VolumeIsEspName(&gVols[i])) {
                continue;
            }
            if (TryRootConfigFromVolume(i, Disk, Part)) {
                FromConfig = 1;
                LogFs("FS: ROOT.CFG loaded\n");
                break;
            }
        }
    }

    if (FromConfig) {
        int Hit = FindVolumeByDiskPart(Disk, Part);

        PinOsTarget(Disk, Part, 1);
        if (Hit >= 0) {
            gOsVolIdx = Hit;
            if (gVols[Hit].HasBlocksId && BlocksVol) {
                *BlocksVol = Hit;
            }
            LogFs("FS: OsTarget via ROOT.CFG\n");
            return;
        }
        /*
         * 指针已钉（常为 U 盘），卷尚未进 Mux：禁止把已扫到的旁路 BLOCKS
         * 当地默认——否则 U 盘起装了 U 盘 Kernel，字体却读 NVMe 旧 CJK。
         */
        if (BlocksVol) {
            *BlocksVol = -1;
        }
        gOsVolIdx = -1;
        LogFs("FS: OsTarget ROOT.CFG not mounted yet\n");
        return;
    }

    /* 3) 无 ROOT.CFG：仅 Pin 启动盘上的 BLOCKS */
    if (BootGuidOk) {
        int OnBoot = FindBlocksOnDisk(Info->BootDiskGuid);

        if (OnBoot >= 0) {
            gOsVolIdx = OnBoot;
            if (BlocksVol) {
                *BlocksVol = OnBoot;
            }
            if (gVols[OnBoot].DiskGuidOk) {
                PinOsTarget(gVols[OnBoot].DiskGuid, gVols[OnBoot].PartGuid,
                            gVols[OnBoot].PartGuidOk);
            }
            LogFs("FS: OsTarget = boot disk BLOCKS\n");
            return;
        }
        /*
         * 启动盘尚未挂上（U 盘起、MSC 未 claim）：绝不把旁路 BLOCKS 当地默认，
         * 也不 Pin——否则字体/主题读 NVMe，且拔 U 盘永不 halt。
         * Files 旁路浏览等 Secondary / claim 后再挂。
         */
        if (BlocksVol) {
            *BlocksVol = -1;
        }
        gOsVolIdx = -1;
        LogFs("FS: OsTarget defer (boot disk not mounted; skip bypass BLOCKS)\n");
        return;
    }

    /* 4) 旧 Boot：旁路 BlocksVol / 首个 BLOCKS 并 Pin */
    if (BlocksVol && *BlocksVol >= 0) {
        gOsVolIdx = *BlocksVol;
    }
    if (gOsVolIdx < 0) {
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].HasBlocksId) {
                gOsVolIdx = i;
                break;
            }
        }
    }
    if (gOsVolIdx >= 0 && gVols[gOsVolIdx].DiskGuidOk) {
        PinOsTarget(gVols[gOsVolIdx].DiskGuid, gVols[gOsVolIdx].PartGuid,
                    gVols[gOsVolIdx].PartGuidOk);
    }
}
