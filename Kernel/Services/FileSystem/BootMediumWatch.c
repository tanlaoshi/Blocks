/*
 * BootMediumWatch.c — MSC 闩启动盘 / 拔棒失根（usb-boot-unplug）
 *
 * 自 MountOs.c 纯搬。Latch 须在仍挂着时调用；Gone 后 Clock 大窗关机。
 */
#include "FileSystemPrivate.h"
#include "BootHandoff.h"
#include "Block.h"
#include "BlockMux.h"
#include "Gpt.h"
#include "SerialLog.h"
#include "Desktop.h"
#include "HalSerial.h"

/*
 * MSC 已在 Mux 上：读其 GPT DiskGUID，与 BootDiskGuid 比较并闩住。
 * 必须在「还挂着」时调用；拔后再读会失败。
 */
void FsLatchMscBootDisk(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();
    UINT32 Drive;
    UINT8 Guid[16];

    gMscDiskGuidOk = 0;
    gMscIsBootDisk = 0;
    for (Drive = 0; Drive < BLOCK_MAX_DRIVES; Drive++) {
        if (!BlockDriveIsMsc(Drive)) {
            continue;
        }
        if (!BlockSelect(Drive)) {
            continue;
        }
        if (!GptCurrentDiskGuid(Guid)) {
            LogFs("FS: msc latch gpt fail drive=");
            LogFsHex32(Drive);
            LogFs("\n");
            continue;
        }
        CopyGuid16(gMscDiskGuid, Guid);
        gMscDiskGuidOk = 1;
        if (Info && Info->BootDiskGuidOk &&
            GuidEqual16(Guid, Info->BootDiskGuid)) {
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
    if (gMscDiskGuidOk && GuidEqual16(gMscDiskGuid, Info->BootDiskGuid)) {
        gBootMediumGone = 1;
        LogFs("FS: boot medium removed (msc guid=BootDiskGuid)\n");
        return;
    }
    if (gOsDiskOk && GuidEqual16(gOsDiskGuid, Info->BootDiskGuid) &&
        gMscDiskGuidOk && GuidEqual16(gMscDiskGuid, gOsDiskGuid)) {
        gBootMediumGone = 1;
        LogFs("FS: boot medium removed (msc held OsTarget)\n");
        return;
    }
    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].DiskGuidOk) {
            continue;
        }
        if (!GuidEqual16(gVols[i].DiskGuid, Info->BootDiskGuid)) {
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
        if (GuidEqual16(gVols[i].DiskGuid, Info->BootDiskGuid)) {
            Present = 1;
            break;
        }
    }
    if (Present) {
        gBootDiskSeen = 1;
        return;
    }
    if (gOsDiskOk && GuidEqual16(gOsDiskGuid, Info->BootDiskGuid)) {
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
    /*
     * 1103 Pi：仅靠 Clock 下一拍会漏窗（串口已见 sdhost unplug，无 boot-gone）。
     * 闩住后当场进大窗关机（不返回）；X64 拔棒同形。
     */
    HalSerialWriteShell("\nFS: notify → boot-gone\n");
    DesktopBootMediumGoneShutdown();
}
