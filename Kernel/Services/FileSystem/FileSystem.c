/*
 * FileSystem.c — 卷激活、路径解析和开机挂卷（story-s3 / PR-S-filesystem-1）
 *
 * 稳态 S3：开机挂卷在此；拔出 Remount 在 Mount.c（Worker→HotPoll 仅拔出）。
 * 桌面不自动 Claim；晚插 U 盘用 Shell `msc hot`。路径：Operations.c；挂卷：Mount*。
 *
 * 开机（fs-msc-defer）：
 *   1. HalBlockInitialize → MountAllVolumes（读 ROOT.CFG → OsTarget）
 *   2. MscAutoApplyPolicy：MSC.OFF/msc=0 → 有 BLOCKS 才关 auto
 *   3–4. 启动盘已有 BLOCKS 则 skip MSC；启动盘未挂（U 盘起）必 claim
 *   5. 桌面后 FileSystemEnsureSecondaryVolumes → SecondaryReady（旁路第二盘）
 *
 * 多份 BLOCKS.ID：皆名 BLOCKS；默认卷跟 OsTarget（ROOT.CFG 可跨盘）。
 * 返回：0 成功（可无 BLOCKS.ID，仅 WARN）；-1 注册失败或最终仍无任何卷。
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "Gpt.h"
#include "BootHandoff.h"
#include "VirtualFileSystem.h"
#include "SerialLog.h"
#include "CoreOps.h"
#include "HalDevices.h"
#include "HalBluetooth.h"
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

/*
 * 解析前缀：
 *   BLOCKS:path / ESP:path / A:path / B:path
 *   0:path / 1:path（卷下标）
 * 无冒号 → 默认卷，整串为相对路径。
 */
int FileSystemResolve(const char *Path, int *OutVol, const char **OutRel) {
    const char *Colon;
    char Pref[FS_VOL_NAME_MAX];
    int PrefLen;
    int i;
    int Vol;

    if (!Path || !OutVol || !OutRel) {
        return FAT_ERR_INVAL;
    }
    if (gVolCount <= 0) {
        return FAT_ERR_IO;
    }

    Colon = Path;
    while (*Colon && *Colon != ':') {
        Colon++;
    }
    if (*Colon != ':') {
        *OutVol = gDefaultVol;
        *OutRel = Path;
        return FAT_OK;
    }

    PrefLen = (int)(Colon - Path);
    if (PrefLen <= 0 || PrefLen >= FS_VOL_NAME_MAX) {
        return FAT_ERR_INVAL;
    }
    for (i = 0; i < PrefLen; i++) {
        Pref[i] = Path[i];
    }
    Pref[PrefLen] = 0;

    Vol = -1;
    if (PrefLen == 1 && Pref[0] >= '0' && Pref[0] <= '9') {
        Vol = Pref[0] - '0';
    } else if (PrefLen == 1 &&
               ((Pref[0] >= 'A' && Pref[0] <= 'Z') ||
                (Pref[0] >= 'a' && Pref[0] <= 'z'))) {
        char L = Pref[0];
        if (L >= 'a') {
            L = (char)(L - 'a' + 'A');
        }
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].Letter == L) {
                Vol = i;
                break;
            }
        }
    } else {
        for (i = 0; i < gVolCount; i++) {
            if (StrEqIgnoreCase(Pref, gVols[i].Name)) {
                Vol = i;
                break;
            }
        }
    }
    if (Vol < 0 || Vol >= gVolCount) {
        return FAT_ERR_NOENT;
    }

    *OutVol = Vol;
    *OutRel = Colon + 1;
    if (**OutRel == '/' || **OutRel == '\\') {
        (*OutRel)++;
    }
    return FAT_OK;
}

int FileSystemVolCount(void) {
    return gVolCount;
}

int FileSystemDefaultVol(void) {
    return gDefaultVol;
}

int FileSystemVolInfo(int Idx, char *Name, int NameMax, UINT32 *Drive,
                      UINT32 *StartLba, int *ReadOnly) {
    if (Idx < 0 || Idx >= gVolCount) {
        return -1;
    }
    if (Name && NameMax > 0) {
        StringCopy(Name, NameMax, gVols[Idx].Name);
    }
    if (Drive) {
        *Drive = gVols[Idx].Drive;
    }
    if (StartLba) {
        *StartLba = gVols[Idx].StartLba;
    }
    if (ReadOnly) {
        *ReadOnly = gVols[Idx].ReadOnly;
    }
    return 0;
}

int FileSystemVolDiskGuidStr(int Idx, char *Buf, int BufMax) {
    char Uuid[37];

    if (Idx < 0 || Idx >= gVolCount || !Buf || BufMax < 37) {
        return -1;
    }
    if (!gVols[Idx].DiskGuidOk) {
        return -1;
    }
    GptFormatGuidUuid(gVols[Idx].DiskGuid, Uuid);
    StringCopy(Buf, BufMax, Uuid);
    return 0;
}

int FileSystemVolOnBootDisk(int Idx) {
    const BOOT_HANDOFF *Info;
    int i;

    if (Idx < 0 || Idx >= gVolCount || !gVols[Idx].DiskGuidOk) {
        return -1;
    }
    Info = BootHandoffGet();
    if (!Info || !Info->BootDiskGuidOk) {
        return -1;
    }
    for (i = 0; i < 16; i++) {
        if (gVols[Idx].DiskGuid[i] != Info->BootDiskGuid[i]) {
            return 0;
        }
    }
    return 1;
}

int FileSystemVolHasBlocksId(int Idx) {
    if (Idx < 0 || Idx >= gVolCount) {
        return 0;
    }
    return gVols[Idx].HasBlocksId ? 1 : 0;
}

/* PR-F3：后端名（如 fat / res）；Out 可 NULL */
int FileSystemVolBackend(int Idx, const char **OutName) {
    const FS_OPS *Ops;
    if (Idx < 0 || Idx >= gVolCount) {
        return -1;
    }
    Ops = gVols[Idx].Ops ? gVols[Idx].Ops : FatFsOps();
    if (OutName) {
        *OutName = Ops->Name ? Ops->Name : "?";
    }
    return 0;
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

/* PR-H-msc-7b：默认卷上 MSC.OFF 或 THEME.CFG 含 msc=0 → 倾向关 auto */
static int MscPolicySaysOff(void) {
    UINT8 Buf[512];
    UINTN Sz = 0;
    UINTN i;

    if (FileSystemReadFile("MSC.OFF", Buf, 1, &Sz) == FAT_OK) {
        return 1;
    }
    Sz = 0;
    if (FileSystemReadFile("THEME.CFG", Buf, sizeof(Buf) - 1, &Sz) != FAT_OK) {
        return 0;
    }
    Buf[Sz] = 0;
    for (i = 0; i + 5 <= Sz; i++) {
        if (Buf[i] == 'm' && Buf[i + 1] == 's' && Buf[i + 2] == 'c' &&
            Buf[i + 3] == '=' && Buf[i + 4] == '0') {
            return 1;
        }
    }
    return 0;
}

static int AnyVolumeHasBlocksId(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].HasBlocksId) {
            return 1;
        }
    }
    return 0;
}

/*
 * 第一轮挂卷成功后：读盘上 MSC.OFF / msc=0（序 2）。
 * 无第一轮卷时不读盘（与旧分支一致）。
 */
static int MscAutoApplyPolicy(int Auto, int HasBlocks) {
    if (!Auto || !MscPolicySaysOff()) {
        return Auto;
    }
    if (HasBlocks) {
        HalUsbMscAutoSet(0);
        LogBoot("Boot: MSC Auto Off (MSC=0/MSC.OFF)\n");
        return 0;
    }
    LogBoot("Boot: MSC Auto Keep (No BLOCKS.ID; Ignore MSC=0)\n");
    return Auto;
}

/*
 * 序 3–4：仅当「启动盘上」已有 Os/BLOCKS 才 skip MSC。
 * 坑：U 盘起时 NVMe 常先挂上 BLOCKS/ROOT.CFG → 旧逻辑当 Os 已挂而跳过 claim，
 * 启动棒永不进 Mux，Files 只剩 NUC 分区；旁路依赖桌面 Hot 也易空。
 * 启动盘尚无卷（BootDiskHasBlocksId==0）→ 必须 claim。旁路第二只盘仍桌面 Secondary。
 */
static int MscAutoResolveClaim(int Auto, int HasBlocks) {
    int BootHas;

    if (!HasBlocks && !Auto) {
        HalUsbMscAutoSet(1);
        LogBoot("Boot: MSC Auto Force (Need BLOCKS)\n");
        return 1;
    }
    if (!Auto) {
        return 0;
    }
    BootHas = FsBootDiskHasBlocksId();
    /* 序码供 BOOT.LOG 对表：1=策略 2=开机claim 3=桌面secondary 4=热插拔 */
    if (BootHas == 1) {
        LogBoot("Fs: msc/1 skip (boot disk has BLOCKS — expect NUC/SSD path)\n");
        return 0;
    }
    if (BootHas == 0) {
        LogBoot("Fs: msc/1 claim (boot disk not mounted — expect USB-stick path)\n");
        return 1;
    }
    /* 旧 Boot 无 DiskGUID：回退「任意 Os 已挂则 skip」 */
    if (FsOsVolumeMounted()) {
        LogBoot("Fs: msc/1 skip (OS volume mounted, no BootDiskGuid)\n");
        return 0;
    }
    LogBoot("Fs: msc/1 claim (need OS volume)\n");
    return 1;
}

int FileSystemInitialize(void) {
    VFS_SERVICE_OPS Svc;
    int Auto;
    int HaveVols = 0;
    int MuxOk = 0;
    int HasBlocks = 0;

    if (gVirtualFileSystem.Register(FatFsOps()) != 0) {
        LogFs("Fs: Register Fat Failed\n");
        return -1;
    }
    if (gVirtualFileSystem.Register(ResourceFileSystemOps()) != 0) {
        LogFs("Fs: Register Res Failed\n");
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
        Auto = MscAutoApplyPolicy(Auto, HasBlocks);
    } else {
        LogFs("Fs: No Primary Volumes Yet (Live USB Path Ok)\n");
    }
    Auto = MscAutoResolveClaim(Auto, HasBlocks);

    if (Auto) {
        int MscRc;

        /* MSC claim 可能很长：先落到 79，口扫描里再 bump 到 91 */
        DesktopBootLogoSet(79);
        LogBoot("Fs: msc/2 boot-claim begin\n");
        /*
         * 启动盘尚无 BLOCKS（0）或旧 Boot 无 GUID（-1）且仍走 claim：
         * Arm 盯口，claim 失败后拔棒靠 PORTSC.CCS → 大窗。
         */
        {
            int BootHas = FsBootDiskHasBlocksId();

            if (BootHas == 0 || BootHas < 0) {
                HalUsbMscArmBootWatch(1);
                LogBoot("Fs: msc boot-watch arm\n");
            }
        }
        MscRc = HalUsbMscAutoBeforeFs();

        if (MscRc == 0) {
            MuxOk = 1;
            LogBoot("Fs: msc/2 boot-claim ok (mux)\n");
            if (BlockInitialize() <= 0) {
                LogFs("Fs: BlockInitialize After Msc Auto Failed\n");
            }
            if (!MountAllVolumes()) {
                LogFs("Fs: Mount After Msc Auto Failed\n");
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
            LogBoot("Fs: msc/2 boot-claim empty (desktop secondary/hotpoll will retry)\n");
        } else if (MscRc < 0) {
            LogBoot("Fs: msc/2 boot-claim abort\n");
        }
        /*
         * btx-1：MSC claim / freeze 后再软探 HCI。
         * 放键鼠后、claim 前会 Address 邻口 → 假 Gone / 鼠键失灵（已踩过）。
         */
        (void)HalBluetoothInitialize();
    } else {
        LogBoot("Fs: msc/2 boot-claim skipped\n");
        (void)HalBluetoothInitialize();
    }

    /*
     * boot-usbuart-demote：勿在此 UsbUartInitialize；串口 App 按需 Claim。
     * PR-BOOT-fast-3 / boot-net-defer：勿 HalIwlClaim / Probe NET（Worker）。
     * 勿 HalWifiClaim（USB 8188EU）：会踩已认 MSC hub。
     */

    if (!HaveVols && !MuxOk) {
        if (HalBlockInitialize() <= 0) {
            LogFs("Fs: No Block Device (Try RES Only)\n");
        }
        if (!MountAllVolumes()) {
            LogFs("Fs: No Volumes Mounted\n");
            return -1;
        }
        HaveVols = 1;
    } else if (!HaveVols) {
        LogFs("Fs: No Volumes Mounted\n");
        return -1;
    }

    Svc.ReadFile = FileSystemReadFile;
    Svc.WriteFile = FileSystemWriteFile;
    Svc.ListEntries = FileSystemListEntries;
    Svc.FileStat = FileSystemFileStat;
    Svc.ReadFileAt = FileSystemReadFileAt;
    Svc.WriteFileAt = FileSystemWriteFileAt;
    VfsServiceOpsRegister(&Svc);
    LogFs("Fs: Ready\n");
    if (!AnyVolumeHasBlocksId()) {
        LogFs("Fs: WARN No BLOCKS.ID On Any Volume\n");
    }
    BootLogFlush();
    return 0;
}

void FileSystemEnsureSecondaryVolumes(void) {
    int HotRc;

    if (gSecondaryReady) {
        return;
    }
    LogFs("Fs: msc/3 secondary begin (desktop once)\n");
    /*
     * 已认领 MSC（U 盘起常见）：勿 HalUsbMscHot——其首句 Unfreeze，
     * 会解开 port-enum 冻、再 Address 抢 xHCI，露光标后鼠死。
     * claim 空同样 freeze（keep mouse）：再 Hot 会把 boot-watch 口 CCS
     * 打毛刺 → 进桌面假「启动盘已拔出」。只 Remount 旁路卷；插盘用 msc hot。
     */
    if (HalUsbMscReady()) {
        LogFs("Fs: msc/3 secondary skip hot (msc ready)\n");
    } else if (HalUsbPortEnumIsFrozen()) {
        LogFs("Fs: msc/3 secondary skip hot (port enum freeze, keep mouse)\n");
    } else {
        HotRc = HalUsbMscHot();
        if (HotRc != 0) {
            LogFs("Fs: msc/3 secondary hot rc=");
            LogFsHex32((UINT32)HotRc);
            LogFs(" (0=ok/already,1=empty,else fail)\n");
        } else {
            LogFs("Fs: msc/3 secondary hot ok\n");
        }
    }
    (void)FileSystemRemountVolumes();
    gSecondaryReady = 1;
    LogFs("Fs: msc/3 secondary ready (Files show all vols)\n");
}

