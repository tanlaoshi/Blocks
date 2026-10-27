/*
 * MountOs.c — OsTarget / SecondaryReady / 失根（usb-map / fs-msc-defer）
 *
 * ```
 * 开机 ResolveOsTarget：启动盘 ESP ROOT.CFG → 钉 Disk/Part
 *   例外：曾误钉旁路且启动盘现已挂上 → unpin 重认
 * usb-boot-unplug：
 *   挂卷后 FsLatchMscBootDisk：读 MSC 盘 GPT GUID，==BootDiskGuid 则闩 gMscIsBootDisk
 *   HotPoll 拔前 Notify：闩住则 Gone（不依赖此时 BlockDriveIsMsc/卷表）
 *   Clock：Gone → 大窗 5s → Shutdown（勿 Remount 扶旁路）
 * ```
 * Files：SecondaryReady=0 只展 OsDisk。钉死后热拔只 rebind 下标，勿改认旁路。
 */
#include "FileSystemPrivate.h"
#include "BootHandoff.h"
#include "Block.h"
#include "BlockMux.h"
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
/* MSC 盘 GPT GUID（挂上时闩）；与 BootDiskGuid 相同 → 拔 MSC=拔启动盘 */
static UINT8 gMscDiskGuid[16];
static int gMscDiskGuidOk;
static int gMscIsBootDisk;

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
    const BOOT_HANDOFF *Info = BootHandoffGet();

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
    if (Info && Info->BootDiskGuidOk && GuidEq16(Disk, Info->BootDiskGuid)) {
        gBootDiskSeen = 1;
    }
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
        LogFs("FS: OsTarget rebind ok\n");
    } else {
        LogFs("FS: OsTarget pinned (volume unplugged)\n");
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
    const BOOT_HANDOFF *Info = BootHandoffGet();
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
            if (!VolIsEspName(&gVols[i]) || !VolOnBootDisk(&gVols[i])) {
                continue;
            }
            if (TryRootCfgFromVol(i, Disk, Part)) {
                FromCfg = 1;
                LogFs("FS: ROOT.CFG loaded (boot disk ESP)\n");
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
                LogFs("FS: ROOT.CFG loaded\n");
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
 * MSC 已在 Mux 上：读其 GPT DiskGUID，与 BootDiskGuid 比较并闩住。
 * 必须在「还挂着」时调用；拔后再读会失败。
 */
void FsLatchMscBootDisk(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();
    UINT32 d;
    UINT8 Guid[16];

    gMscDiskGuidOk = 0;
    gMscIsBootDisk = 0;
    for (d = 0; d < BLOCK_MAX_DRIVES; d++) {
        if (!BlockDriveIsMsc(d)) {
            continue;
        }
        if (!BlockSelect(d)) {
            continue;
        }
        if (!GptCurrentDiskGuid(Guid)) {
            LogFs("FS: msc latch gpt fail drive=");
            LogFsHex32(d);
            LogFs("\n");
            continue;
        }
        CopyGuid16(gMscDiskGuid, Guid);
        gMscDiskGuidOk = 1;
        if (Info && Info->BootDiskGuidOk && GuidEq16(Guid, Info->BootDiskGuid)) {
            gMscIsBootDisk = 1;
            gBootDiskSeen = 1;
            LogFs("FS: msc latched BOOT disk\n");
        } else {
            LogFs("FS: msc latched (bypass)\n");
        }
        return;
    }
}

/*
 * MSC Release 前调用。主判据：挂载时已闩 gMscIsBootDisk。
 * 补判：卷表里 BootDiskGuid 仍落在 MSC 盘号（闩失败时）。
 */
void FileSystemNotifyMscUnplug(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();
    int i;

    if (gBootMediumGone) {
        return;
    }

    LogFs("FS: msc unplug notify isBoot=");
    LogFsHex32((UINT32)gMscIsBootDisk);
    LogFs(" guidOk=");
    LogFsHex32((UINT32)gMscDiskGuidOk);
    LogFs("\n");

    if (gMscIsBootDisk) {
        gBootMediumGone = 1;
        LogFs("FS: boot medium removed (msc was boot disk)\n");
        return;
    }

    if (!Info || !Info->BootDiskGuidOk) {
        return;
    }
    /* 闩未立时：OsTarget 已钉在 BootDisk 且 MSC 盘 GUID==BootDisk */
    if (gMscDiskGuidOk && GuidEq16(gMscDiskGuid, Info->BootDiskGuid)) {
        gBootMediumGone = 1;
        LogFs("FS: boot medium removed (msc guid=BootDiskGuid)\n");
        return;
    }
    if (gOsDiskOk && GuidEq16(gOsDiskGuid, Info->BootDiskGuid) && gMscDiskGuidOk &&
        GuidEq16(gMscDiskGuid, gOsDiskGuid)) {
        gBootMediumGone = 1;
        LogFs("FS: boot medium removed (msc held OsTarget)\n");
        return;
    }
    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].DiskGuidOk) {
            continue;
        }
        if (!GuidEq16(gVols[i].DiskGuid, Info->BootDiskGuid)) {
            continue;
        }
        if (!BlockDriveIsMsc(gVols[i].Drive)) {
            continue;
        }
        gBootDiskSeen = 1;
        gBootMediumGone = 1;
        LogFs("FS: boot medium removed (vol on msc)\n");
        return;
    }
}

/*
 * Remount 后：闩 MSC；补判失根。
 */
void FsUpdateBootMediumState(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();
    int i;
    int Present = 0;
    int OsWasBootDisk = 0;

    if (gBootMediumGone) {
        return;
    }

    FsLatchMscBootDisk();
    /*
     * MSC 仍在 Mux 且 GPT==BootDiskGuid：棒还在。
     * 卷表暂时缺该 GUID（claim 后首轮 FAT 未齐 / Activate 失败）≠ 拔出；
     * 旧逻辑此处钉 Gone → 开机直接「启动盘已拔出」。
     */
    if (gMscIsBootDisk) {
        gBootDiskSeen = 1;
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
    if (gOsDiskOk && GuidEq16(gOsDiskGuid, Info->BootDiskGuid)) {
        OsWasBootDisk = 1;
    }
    if (!gBootDiskSeen && !OsWasBootDisk && !gMscIsBootDisk) {
        return;
    }
    /*
     * 卷表已无 BootDisk，且本会话 MSC 曾是启动盘 / Seen / Os 在启动盘 → Gone。
     * 注意：仅 Seen（NUC 起）且 MSC 不是启动盘时，Present 仍应为 1（NUC 卷）；
     * 此处 Present=0 只在启动盘卷全消失时成立。
     */
    if (!gMscIsBootDisk && !OsWasBootDisk && gBootDiskSeen) {
        /* NUC 起却 Present=0：异常；仍标 Gone 以免空根 Continue */
    }
    gBootMediumGone = 1;
    LogFs("FS: boot medium removed\n");
}

int FileSystemBootMediumGone(void) {
    return gBootMediumGone ? 1 : 0;
}

/*
 * U 盘起 claim 失败后盯口 CCS 掉线。
 * 仅 HalUsbMscArmBootWatch(1) 后才会走到这里——门闩已在 FS claim 前拉上，
 * 勿再要求 BootDiskGuidOk/卷表（缺 GUID 时旧逻辑直接 return → 拔棒无大窗）。
 */
void FileSystemNotifyBootWatchUnplug(void) {
    if (gBootMediumGone) {
        return;
    }
    gBootMediumGone = 1;
    LogFs("FS: boot medium removed (watch port)\n");
}
