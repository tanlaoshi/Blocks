/*
 * FileSystem.c — 卷激活、路径解析和开机挂卷（PR-S-filesystem-1）
 * 路径操作：Operations.c；挂卷：Mount.c。
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "Gpt.h"
#include "VirtualFileSystem.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "CoreOps.h"
#include "HalDevices.h"
#include "HalConsole.h"
#include "KernelModulesPrivate.h"
#include "HalDriverFile.h"
#include "LogFile.h"

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
 *   TOYOS:path / ESP:path / A:path / B:path
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

/* PR-H-msc-7b：卷上 MSC.OFF 或 THEME.CFG 含 msc=0 → 关 auto */
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

static int AnyVolumeHasToyId(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].HasToyId) {
            return 1;
        }
    }
    return 0;
}

int FileSystemInitialize(void) {
    VFS_SERVICE_OPS Svc;
    int Auto;
    int HaveVols = 0;
    int MuxOk = 0;
    int HasToy = 0;

    if (gVirtualFileSystem.Register(FatFsOps()) != 0) {
        DebugWrite("FS: gVirtualFileSystem.Register(fat) failed\n");
        return 0;
    }
    if (gVirtualFileSystem.Register(ResFsOps()) != 0) {
        DebugWrite("FS: gVirtualFileSystem.Register(res) failed\n");
        return 0;
    }
    /* 驱动固件/配置经 HalDriverFileRead，不直接 #include FileSystem.h */
    HalDriverFileReadRegister(FileSystemReadFile);

    /*
     * PR-H-msc-7b：Live 默认 FS 前 auto（claim→Mux），再一次 MountAllVolumes。
     * 主盘已有 TOYOS 且 MSC.OFF / msc=0 → 可关 auto。
     * 主盘只有 ESP、尚无 TOYOS.ID 时不得关 auto（NUC Live：否则永远读不到 U 盘 TOYOS）。
     */
    Auto = HalUsbMscAutoEnabled();
    if (HalBlockInitialize() > 0 && MountAllVolumes()) {
        HaveVols = 1;
        HasToy = AnyVolumeHasToyId();
        if (Auto && MscPolicySaysOff()) {
            if (HasToy) {
                Auto = 0;
                HalUsbMscAutoSet(0);
                ToyLogBoot("Boot: MSC Auto Off (MSC=0/MSC.OFF)\n");
            } else {
                ToyLogBoot(
                    "Boot: MSC Auto Keep (No BLOCKS.ID; Ignore MSC=0)\n");
            }
        }
    } else {
        DebugWrite("FS: no primary volumes yet (Live USB path ok)\n");
    }

    if (!HasToy && !Auto) {
        Auto = 1;
        HalUsbMscAutoSet(1);
        ToyLogBoot("Boot: MSC Auto Force (Need BLOCKS)\n");
    }

    if (Auto) {
        if (HalUsbMscAutoBeforeFs() == 0) {
            MuxOk = 1;
            if (BlockInitialize() <= 0) {
                DebugWrite("FS: BlockInitialize after msc auto failed\n");
            }
            if (!MountAllVolumes()) {
                DebugWrite("FS: mount after msc auto failed\n");
                if (!HaveVols) {
                    (void)InitializeUsbUart();
                    return 0;
                }
            } else {
                HaveVols = 1;
                HasToy = AnyVolumeHasToyId();
            }
        } else if (!HasToy) {
            /*
             * Live U 盘上电慢：首轮 claim 空 → 只见 NVMe ESP。
             * PR-BOOT-fast-2：仅再试一轮（claim 内 Force/hub 等待已收紧）。
             * 仍失败则软退：有 ESP/RES 可进桌面（WARN no TOYOS.ID）。
             */
            ToyLogBoot("Boot: MSC Auto Retry (No BLOCKS)\n");
            if (HalUsbMscAutoBeforeFs() == 0) {
                MuxOk = 1;
                if (BlockInitialize() > 0 && MountAllVolumes()) {
                    HaveVols = 1;
                    HasToy = AnyVolumeHasToyId();
                }
            }
        }
    }

    /* 两次 U 盘扫描都结束再认。扫盘中途配好会被下一轮复位冲掉，棒上就没有字节。 */
    (void)InitializeUsbUart();

    /*
     * PR-BOOT-fast-3：勿在此 HalIwlClaim / Probe NET。
     * iwl BAR+FW/关联由 Worker（图标之后）HalIwlClaim → BgPump；
     * 有线 NIC 仍在 Network 模块 NetInit Probe。
     * 勿在此调 HalWifiClaim（USB 8188EU）：会踩已认 MSC hub。
     */

    if (!HaveVols && !MuxOk) {
        if (HalBlockInitialize() <= 0) {
            DebugWrite("FS: no block device (RES-only possible)\n");
        }
        if (!MountAllVolumes()) {
            DebugWrite("FS: no volumes mounted\n");
            return 0;
        }
    } else if (!HaveVols) {
        DebugWrite("FS: no volumes mounted\n");
        return 0;
    }

    Svc.ReadFile = FileSystemReadFile;
    Svc.WriteFile = FileSystemWriteFile;
    Svc.ListEntries = FileSystemListEntries;
    Svc.FileStat = FileSystemFileStat;
    Svc.ReadFileAt = FileSystemReadFileAt;
    Svc.WriteFileAt = FileSystemWriteFileAt;
    VfsServiceOpsRegister(&Svc);
    DebugWrite("FS ready (ls, cat, write, wrbig, dirstress, rm, mkdir, rmdir, mv, vols, filestat, filesync)\n");
    if (!AnyVolumeHasToyId()) {
        HalConsoleWriteSerial("Fs: WARN no BLOCKS.ID on any volume\n");
    }
    BootLogFlush();
    return 0;
}

int InitializeFileSystem(void) {
    return FileSystemInitialize();
}
