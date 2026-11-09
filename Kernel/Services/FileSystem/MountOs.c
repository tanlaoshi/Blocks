/*
 * MountOs.c — OsTarget / SecondaryReady 状态与查询（usb-map / fs-msc-defer）
 *
 * Resolve：OsTargetResolve.c；失根：BootMediumWatch.c。
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

int gSecondaryReady;
int gOsVolIdx = -1;
UINT8 gOsDiskGuid[16];
int gOsDiskOk;

UINT8 gOsPartGuid[16];
int gOsPartOk;
/* 1=已为本会话钉过 OsTarget；Remount 不得再用旁路 ROOT.CFG/BLOCKS 改写 */
int gOsTargetPinned;
/* 本会话曾见过 BootDisk 上的卷；其后 BootDisk 空且 OsTarget 在该盘 → 失根 */
int gBootDiskSeen;
int gBootMediumGone;
/* MSC 盘 GPT GUID（挂上时闩）；与 BootDiskGuid 相同 → 拔 MSC=拔启动盘 */
UINT8 gMscDiskGuid[16];
int gMscDiskGuidOk;
int gMscIsBootDisk;

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
    return GuidEqual16(gVols[Idx].DiskGuid, gOsDiskGuid);
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
