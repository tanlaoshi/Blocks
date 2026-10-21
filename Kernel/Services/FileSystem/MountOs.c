/*
 * MountOs.c — OsTarget / SecondaryReady / 失根（usb-map / fs-msc-defer）
 *
 * ```
 * 开机 ResolveOsTarget：启动盘 ESP ROOT.CFG → 钉 Disk/Part
 *   例外：曾误钉旁路且启动盘现已挂上 → unpin 重认
 * Remount 后 FsUpdateBootMediumState：
 *   曾见 BootDisk 卷、现无 → gBootMediumGone（拔加载 EFI 的盘）
 * Clock HotPoll→Remount→FileSystemBootMediumGone→Park（usb-boot-unplug 待可靠）
 * ```
 * Files：SecondaryReady=0 只展 OsDisk。钉死后热拔只 rebind 下标，勿改认旁路。
 */
#include "FileSystemPrivate.h"
#include "BootInfo.h"
#include "Gpt.h"
#include "VirtualFileSystem.h"
#include "SerialLog.h"

int gSecondaryReady;
int gOsVolIdx = -1;
UINT8 gOsDiskGuid[16];
int gOsDiskOk;

static UINT8 gOsPartGuid[16];
static int gOsPartOk;
/* 1=已为本会话钉过 OsTarget；Remount 不得再用旁路 ROOT.CFG/BLOCKS 改写 */
static int gOsTargetPinned;
/* 本会话曾见过 BootDisk 上的卷；其后 BootDisk 空且 OsTarget 在该盘 → 失根 */
static int gBootDiskSeen;
static int gBootMediumGone;

static int GuidEq16(const UINT8 *A, const UINT8 *B) {
    int i;

    for (i = 0; i < 16; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

static void CopyGuid16(UINT8 *Dst, const UINT8 *Src) {
    int i;

    for (i = 0; i < 16; i++) {
        Dst[i] = Src[i];
    }
}

static int VolIsEspName(const FS_VOLUME *V) {
    return V && V->Name[0] == 'E' && V->Name[1] == 'S' && V->Name[2] == 'P';
}

static int VolOnBootDisk(const FS_VOLUME *V) {
    const BOOT_INFO *Info = BootInfoGet();

    if (!V || !V->DiskGuidOk || !Info || !Info->BootDiskGuidOk) {
        return 0;
    }
    return GuidEq16(V->DiskGuid, Info->BootDiskGuid);
}

static int ParseRootCfg(const char *Buf, UINTN Len, UINT8 Disk[16], UINT8 Part[16]) {
    UINTN i = 0;
    int HaveDisk = 0;
    int HavePart = 0;

    while (i < Len) {
        char Key[8];
        char Val[40];
        UINTN k = 0;
        UINTN v = 0;

        while (i < Len && (Buf[i] == '\n' || Buf[i] == '\r' || Buf[i] == ' ')) {
            i++;
        }
        if (i >= Len) {
            break;
        }
        if (Buf[i] == '#') {
            while (i < Len && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        k = 0;
        while (i < Len && Buf[i] != '=' && Buf[i] != '\n' && k < sizeof(Key) - 1) {
            Key[k++] = Buf[i++];
        }
        Key[k] = 0;
        if (i >= Len || Buf[i] != '=') {
            while (i < Len && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        i++;
        v = 0;
        while (i < Len && Buf[i] != '\n' && Buf[i] != '\r' && v < sizeof(Val) - 1) {
            Val[v++] = Buf[i++];
        }
        Val[v] = 0;
        if (Key[0] == 'd' && Key[1] == 'i' && Key[2] == 's' && Key[3] == 'k' &&
            Key[4] == 0) {
            if (GptParseUuidStr(Val, Disk)) {
                HaveDisk = 1;
            }
        } else if (Key[0] == 'p' && Key[1] == 'a' && Key[2] == 'r' && Key[3] == 't' &&
                   Key[4] == 0) {
            if (GptParseUuidStr(Val, Part)) {
                HavePart = 1;
            }
        }
    }
    return HaveDisk && HavePart;
}

static int TryRootCfgFromVol(int VolIdx, UINT8 Disk[16], UINT8 Part[16]) {
    char Buf[256];
    UINTN Sz = 0;

    if (FileSystemActivate(VolIdx) != FAT_OK) {
        return 0;
    }
    if (gVirtualFileSystem.ReadFile("EFI\\Blocks\\ROOT.CFG", Buf, sizeof(Buf) - 1,
                                    &Sz) != FAT_OK) {
        Sz = 0;
        if (gVirtualFileSystem.ReadFile("EFI/Blocks/ROOT.CFG", Buf, sizeof(Buf) - 1,
                                        &Sz) != FAT_OK) {
            return 0;
        }
    }
    Buf[Sz] = 0;
    return ParseRootCfg(Buf, Sz, Disk, Part);
}

static int FindVolByDiskPart(const UINT8 Disk[16], const UINT8 Part[16]) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].DiskGuidOk || !gVols[i].PartGuidOk) {
            continue;
        }
        if (GuidEq16(gVols[i].DiskGuid, Disk) && GuidEq16(gVols[i].PartGuid, Part)) {
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
        if (GuidEq16(gVols[i].DiskGuid, Disk)) {
            return i;
        }
    }
    return -1;
}

static void PinOsTarget(const UINT8 Disk[16], const UINT8 Part[16], int HavePart) {
    CopyGuid16(gOsDiskGuid, Disk);
    gOsDiskOk = 1;
    if (HavePart) {
        CopyGuid16(gOsPartGuid, Part);
        gOsPartOk = 1;
    } else {
        gOsPartOk = 0;
    }
    gOsTargetPinned = 1;
}

static void RebindPinnedOsTarget(int *BlocksVol) {
    gOsVolIdx = -1;
    if (!gOsDiskOk) {
        return;
    }
    if (gOsPartOk) {
        gOsVolIdx = FindVolByDiskPart(gOsDiskGuid, gOsPartGuid);
    }
    if (gOsVolIdx < 0) {
        gOsVolIdx = FindBlocksOnDisk(gOsDiskGuid);
    }
    if (gOsVolIdx >= 0 && gVols[gOsVolIdx].HasBlocksId && BlocksVol) {
        *BlocksVol = gOsVolIdx;
    }
    if (gOsVolIdx >= 0) {
        LogFs("Fs: OsTarget rebind ok\n");
    } else {
        LogFs("Fs: OsTarget pinned (volume unplugged)\n");
    }
}

static int AnyVolOnBootDisk(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (VolOnBootDisk(&gVols[i])) {
            return 1;
        }
    }
    return 0;
}

void FsResolveOsTarget(int *BlocksVol) {
    int i;
    UINT8 Disk[16];
    UINT8 Part[16];
    int FromCfg = 0;
    const BOOT_INFO *Info = BootInfoGet();
    int BootGuidOk = (Info && Info->BootDiskGuidOk) ? 1 : 0;

    gOsVolIdx = -1;

    /*
     * 已钉死：通常只重绑。
     * 例外：U 盘起时首轮只挂到 NVMe，曾误把旁路 BLOCKS Pin 成 OsTarget——
     * 启动盘现已挂上则作废重认（否则拔 U 盘仍 rebind 到 NUC、永不 halt）。
     */
    if (gOsTargetPinned && gOsDiskOk) {
        if (BootGuidOk && !GuidEq16(gOsDiskGuid, Info->BootDiskGuid) &&
            AnyVolOnBootDisk()) {
            LogFs("Fs: OsTarget unpin (was not boot disk; boot disk now mounted)\n");
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
            if (!VolIsEspName(&gVols[i]) || !VolOnBootDisk(&gVols[i])) {
                continue;
            }
            if (TryRootCfgFromVol(i, Disk, Part)) {
                FromCfg = 1;
                LogFs("Fs: ROOT.CFG loaded (boot disk ESP)\n");
                break;
            }
        }
    }

    /* 2) 旧 Boot 无 GUID：才扫任意 ESP */
    if (!FromCfg && !BootGuidOk) {
        for (i = 0; i < gVolCount; i++) {
            if (!VolIsEspName(&gVols[i])) {
                continue;
            }
            if (TryRootCfgFromVol(i, Disk, Part)) {
                FromCfg = 1;
                LogFs("Fs: ROOT.CFG loaded\n");
                break;
            }
        }
    }

    if (FromCfg) {
        int Hit = FindVolByDiskPart(Disk, Part);

        PinOsTarget(Disk, Part, 1);
        if (Hit >= 0) {
            gOsVolIdx = Hit;
            if (gVols[Hit].HasBlocksId && BlocksVol) {
                *BlocksVol = Hit;
            }
            LogFs("Fs: OsTarget via ROOT.CFG\n");
            return;
        }
        LogFs("Fs: OsTarget ROOT.CFG not mounted yet\n");
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
            LogFs("Fs: OsTarget = boot disk BLOCKS\n");
            return;
        }
        /*
         * 启动盘尚未挂上（U 盘起、MSC 未 claim）：可暂用旁路卷浏览，
         * 但绝不 Pin——否则永远钉在 NUC，拔启动盘也不 halt。
         */
        if (BlocksVol && *BlocksVol >= 0) {
            gOsVolIdx = *BlocksVol;
        }
        LogFs("Fs: OsTarget defer (boot disk not mounted)\n");
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

int FsOsVolumeMounted(void) {
    return gOsVolIdx >= 0;
}

int FsVolOnOsDisk(int Idx) {
    if (Idx < 0 || Idx >= gVolCount) {
        return 0;
    }
    if (!gOsDiskOk || !gVols[Idx].DiskGuidOk) {
        return 1; /* 无 GUID：不过滤 */
    }
    return GuidEq16(gVols[Idx].DiskGuid, gOsDiskGuid);
}

int FileSystemSecondaryVolsReady(void) {
    return gSecondaryReady ? 1 : 0;
}

int FileSystemVolOnOsDisk(int Idx) {
    return FsVolOnOsDisk(Idx);
}

int FileSystemOsVol(void) {
    return gOsVolIdx;
}

/*
 * 挂卷后调用：区分「旁路 U 盘热拔」与「拔掉启动盘（根在其上）」。
 * 后者置位 BootMediumGone——Kernel 虽仍在 RAM，根文件系统已不在，不应继续当系统用。
 */
void FsUpdateBootMediumState(void) {
    const BOOT_INFO *Info = BootInfoGet();
    int i;
    int Present = 0;

    if (gBootMediumGone) {
        return;
    }
    if (!Info || !Info->BootDiskGuidOk) {
        return;
    }
    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].DiskGuidOk) {
            continue;
        }
        if (GuidEq16(gVols[i].DiskGuid, Info->BootDiskGuid)) {
            Present = 1;
            break;
        }
    }
    if (Present) {
        gBootDiskSeen = 1;
        return;
    }
    if (!gBootDiskSeen) {
        return; /* 启动盘尚未挂上（例如 U 盘起、MSC 尚未 claim） */
    }
    /*
     * 曾见过启动盘、现已无任何卷 = 拔掉加载 EFI 的那块盘。
     * 不依赖 OsTarget 是否曾误钉旁路（U 盘起首轮只见 NVMe 时的旧坑）。
     */
    gBootMediumGone = 1;
    LogFs("Fs: boot medium removed\n");
}

int FileSystemBootMediumGone(void) {
    return gBootMediumGone ? 1 : 0;
}
