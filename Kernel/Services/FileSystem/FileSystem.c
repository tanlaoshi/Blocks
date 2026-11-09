/*
 * FileSystem.c — 卷激活与开机挂卷编排（story-s3 / PR-S-filesystem-1）
 *
 * 稳态 S3：开机挂卷在此；拔出 Remount 在 Mount.c（Worker→HotPoll 仅拔出）。
 * 桌面不自动 Claim；晚插 U 盘用 Shell `msc hot`。
 * 路径读写：Operations.c；挂卷：Mount*；前缀解析：PathResolve.c；
 * 卷查询：VolumeQuery.c；MSC auto：MassStorageAutoPolicy.c。
 *
 * 开机（fs-msc-defer）：
 *   1. HalBlockInitialize → MountAllVolumes（读 ROOT.CFG → OsTarget）
 *   2. MassStorageAutoApplyPolicy：MSC.OFF/msc=0 → 有 BLOCKS 才关 auto
 *   3–4. 启动盘已有 BLOCKS 则 skip MSC；启动盘未挂（U 盘起）必 claim
 *   5. 桌面后 FileSystemEnsureSecondaryVolumes → SecondaryReady（旁路第二盘）
 *
 * 多份 BLOCKS.ID：皆名 BLOCKS；默认卷跟 OsTarget（ROOT.CFG 可跨盘）。
 * `BLOCKS:` 前缀也必须解析到 OsTarget，禁止卷表里「第一个叫 BLOCKS 的」
 * （U 盘起时常是先挂上的 NVMe）——否则 Store 装到 SSD、BLOCKS.DB 却写 U 盘。
 * 返回：0 成功（可无 BLOCKS.ID，仅 WARN）；-1 注册失败或最终仍无任何卷。
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "VirtualFileSystem.h"
#include "SerialLog.h"
#include "Atomic.h"
#include "Hal.h"
#include "CoreOps.h"
#include "HalDevices.h"
#include "HalDriverFile.h"
#include "LogFile.h"
#include "Desktop.h"

FS_VOLUME gVols[FS_MAX_VOLUMES];
int gVolCount;
int gDefaultVol;
int gActiveVol = -1;
UINT32 gActiveDrive = 0xFFFFFFFFu;
UINT32 gActiveLba = 0xFFFFFFFFu;
const FS_OPS *gActiveOps;

int FileSystemActivate(int VolIdx) {
    const FS_OPS *Ops;

    if (VolIdx < 0 || VolIdx >= gVolCount) {
        return FAT_ERR_INVAL;
    }
    Ops = gVols[VolIdx].Ops ? gVols[VolIdx].Ops : FatFsOps();
    if (gActiveVol == VolIdx &&
        gActiveOps == Ops &&
        gActiveDrive == gVols[VolIdx].Drive &&
        gActiveLba == gVols[VolIdx].StartLba) {
        return FAT_OK;
    }
    if (gVirtualFileSystem.Select(Ops) != 0) {
        return FAT_ERR_IO;
    }
    if (!Ops->Synthetic) {
        if (!BlockSelect(gVols[VolIdx].Drive)) {
            gActiveVol = -1;
            gActiveOps = 0;
            return FAT_ERR_IO;
        }
    }
    if (gVirtualFileSystem.Mount(gVols[VolIdx].StartLba) != FAT_OK) {
        gActiveVol = -1;
        gActiveOps = 0;
        return FAT_ERR_IO;
    }
    gActiveVol = VolIdx;
    gActiveOps = Ops;
    gActiveDrive = gVols[VolIdx].Drive;
    gActiveLba = gVols[VolIdx].StartLba;
    return FAT_OK;
}

static int (*gPathBlocked)(const char *Path);

void FileSystemRegisterPathGuard(int (*Blocked)(const char *Path)) {
    gPathBlocked = Blocked;
}

int FileSystemPathBlocked(const char *Path) {
    if (!gPathBlocked) {
        return 0;
    }
    return gPathBlocked(Path);
}

int FileSystemInitialize(void) {
    VFS_SERVICE_OPS Svc;
    int Auto;
    int HaveVols = 0;
    int MuxOk = 0;
    int HasBlocks = 0;

    if (gVirtualFileSystem.Register(FatFsOps()) != 0) {
        LogFs("FS: Register Fat Failed\n");
        return -1;
    }
    if (gVirtualFileSystem.Register(ResourceFileSystemOps()) != 0) {
        LogFs("FS: Register Res Failed\n");
        return -1;
    }
    /* 驱动固件/配置经 HalDriverFileRead，不直接 #include FileSystem.h */
    HalDriverFileReadRegister(FileSystemReadFile);

    /*
     * 1：内置盘先挂；2：仅首轮成功才读盘上关；3–4：Force/Skip（fs-msc-order）。
     * 主盘只有 ESP、尚无 BLOCKS.ID 时不得因 msc=0 关 auto。
     */
    gSecondaryReady = 0;
    Auto = HalUsbMscAutoEnabled();
    if (HalBlockInitialize() > 0 && MountAllVolumes()) {
        HaveVols = 1;
        HasBlocks = AnyVolumeHasBlocksId();
        Auto = MassStorageAutoApplyPolicy(Auto, HasBlocks);
    } else {
        LogFs("FS: No Primary Volumes Yet (Live USB Path Ok)\n");
    }
    Auto = MassStorageAutoResolveClaim(Auto, HasBlocks);

    if (Auto) {
        int MscRc;

        /* MSC claim 可能很长：先落到 79，口扫描里再 bump 到 91 */
        DesktopBootLogoSet(79);
        LogBoot("FS: msc/2 boot-claim begin\n");
        /*
         * 启动盘尚无 BLOCKS（0）或旧 Boot 无 GUID（-1）且仍走 claim：
         * Arm 盯口，claim 失败后拔棒靠 PORTSC.CCS → 大窗。
         */
        {
            int BootHas = FsBootDiskHasBlocksId();

            if (BootHas == 0 || BootHas < 0) {
                HalUsbMscArmBootWatch(1);
                LogBoot("FS: msc boot-watch arm\n");
            }
        }
        MscRc = HalUsbMscAutoBeforeFs();

        if (MscRc == 0) {
            MuxOk = 1;
            LogBoot("FS: msc/2 boot-claim ok (mux)\n");
            if (BlockInitialize() <= 0) {
                LogFs("FS: BlockInitialize After Msc Auto Failed\n");
            }
            if (!MountAllVolumes()) {
                LogFs("FS: Mount After Msc Auto Failed\n");
                if (!HaveVols) {
                    return -1;
                }
            } else {
                HaveVols = 1;
                HasBlocks = AnyVolumeHasBlocksId();
            }
        } else if (MscRc > 0 && !FsOsVolumeMounted()) {
            /*
             * claim 空：勿整轮 Force 再扫（会 reset 根口、鼠间歇卡）。
             * 桌面 EnsureSecondary / msc hot 再试。
             */
            LogBoot("FS: msc/2 boot-claim empty (desktop secondary/hotpoll will retry)\n");
        } else if (MscRc < 0) {
            LogBoot("FS: msc/2 boot-claim abort\n");
        }
        /*
         * btx：勿在此 HalBluetoothInitialize——挡进桌面。
         * Worker Reveal 后再探（仍须晚于 MSC claim；claim 前 Address 会假 Gone）。
         */
    } else {
        LogBoot("FS: msc/2 boot-claim skipped\n");
    }

    /*
     * boot-usbuart-demote：勿在此 UsbUartInitialize；串口 App 按需 Claim。
     * PR-BOOT-fast-3 / boot-net-defer：勿 HalIwlClaim / Probe NET（Worker）。
     * 勿 HalWifiClaim（USB 8188EU）：会踩已认 MSC hub。
     */

    if (!HaveVols && !MuxOk) {
        if (HalBlockInitialize() <= 0) {
            LogFs("FS: No Block Device (Try RES Only)\n");
        }
        if (!MountAllVolumes()) {
            LogFs("FS: No Volumes Mounted\n");
            return -1;
        }
        HaveVols = 1;
    } else if (!HaveVols) {
        LogFs("FS: No Volumes Mounted\n");
        return -1;
    }

    Svc.ReadFile = FileSystemReadFile;
    Svc.WriteFile = FileSystemWriteFile;
    Svc.ListEntries = FileSystemListEntries;
    Svc.FileStat = FileSystemFileStat;
    Svc.ReadFileAt = FileSystemReadFileAt;
    Svc.WriteFileAt = FileSystemWriteFileAt;
    Svc.DeleteFile = FileSystemDeleteFile;
    Svc.RemoveDirectory = FileSystemRemoveDirectory;
    Svc.Rename = FileSystemRename;
    VfsServiceOpsRegister(&Svc);
    LogFs("FS: Ready\n");
    /*
     * ksend-rollback：Duo BootStub 每启递增 BLOCKS:boot.try；超限改载 .bak。
     * 能走到 FS Ready 即视为试跑成功，清零计数（缺文件/他板无 stub 无害）。
     */
    {
        UINT8 Zero = 0;
        (void)FileSystemWriteFile("B:boot.try", &Zero, 1);
    }
    if (!AnyVolumeHasBlocksId()) {
        LogFs("FS: WARN No BLOCKS.ID On Any Volume\n");
    }
    BootLogFlush();
    return 0;
}

void FileSystemEnsureSecondaryVolumes(void) {
    int HotRc;

    if (gSecondaryReady) {
        return;
    }
    LogFs("FS: msc/3 secondary begin (desktop once)\n");
    /*
     * 已认领 MSC（U 盘起常见）：勿 HalUsbMscHot——其首句 Unfreeze，
     * 会解开 port-enum 冻、再 Address 抢 xHCI，露光标后鼠死。
     * claim 空同样 freeze（keep mouse）：再 Hot 会把 boot-watch 口 CCS
     * 打毛刺 → 进桌面假「启动盘已拔出」。只 Remount 旁路卷；插盘用 msc hot。
     */
    if (HalUsbMscReady()) {
        LogFs("FS: msc/3 secondary skip hot (msc ready)\n");
    } else if (HalUsbPortEnumerateIsFrozen()) {
        LogFs("FS: msc/3 secondary skip hot (port enum freeze, keep mouse)\n");
    } else {
        HotRc = HalUsbMscHot();
        if (HotRc != 0) {
            LogFs("FS: msc/3 secondary hot rc=");
            LogFsHex32((UINT32)HotRc);
            LogFs(" (0=ok/already,1=empty,else fail)\n");
        } else {
            LogFs("FS: msc/3 secondary hot ok\n");
        }
    }
    (void)FileSystemRemountVolumes();
    gSecondaryReady = 1;
    LogFs("FS: msc/3 secondary ready (Files show all vols)\n");
}

/*
 * PR-F5：FS 互斥。
 * FAT 层保留一个「当前卷」指针 + 每卷缓冲；两个线程（前台 shell/GUI 与
 * 后台 Desktop Worker 的 Remount）同时进出会切走当前卷、写脏目录项。
 * 这里一把粗锁把整段「解析→激活→读写」串起来；只在 FS 调用路径上持有，
 * 不用 cli（持锁期间可能等 MSC 传输，cli 会让中断长时间关闭）。
 */
static volatile UINT32 gFsLock;

void FileSystemLock(void) {
    while (BLOCKS_ATOMIC_TAS(&gFsLock, 1u)) {
        HalCpuRelax();
    }
}

void FileSystemUnlock(void) {
    gFsLock = 0;
}
