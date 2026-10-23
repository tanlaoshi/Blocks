/*
 * MountVolume.c — 单 FAT 分区挂载 + RES（story-s3 / PR-F-fs-1）
 *
 * MountAllVolumes 每分区落到这里；编排在 Mount.c。
 * BLOCKS.ID → 名 BLOCKS；ESP 皆名 ESP（同名靠 DiskGUID / 启动盘标记）。
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "BlockMux.h"
#include "Gpt.h"
#include "BootHandoff.h"
#include "VirtualFileSystem.h"
#include "SerialLog.h"

#define BOOT_GUID_CACHE 16
static UINT8 sDriveGuid[BOOT_GUID_CACHE][16];
static UINT8 sDriveGuidOk[BOOT_GUID_CACHE];

static int GuidEq16(const UINT8 *A, const UINT8 *B) {
    int i;

    for (i = 0; i < 16; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

/* 当前已选中的盘是否就是加载 EFI 的那块。-1 = 旧 Boot，未交代。 */
static int CacheBootDiskMatch(UINT32 Drive) {
    const BOOT_HANDOFF *Info = BootHandoffGet();

    if (!Info || !Info->BootDiskGuidOk) {
        return -1;
    }
    if (Drive >= BOOT_GUID_CACHE) {
        return 0;
    }
    if (sDriveGuidOk[Drive] == 0) {
        if (!BlockSelect(Drive) || !GptCurrentDiskGuid(sDriveGuid[Drive])) {
            sDriveGuidOk[Drive] = 2;
            return 0;
        }
        sDriveGuidOk[Drive] = 1;
    }
    if (sDriveGuidOk[Drive] != 1) {
        return 0;
    }
    return GuidEq16(sDriveGuid[Drive], Info->BootDiskGuid);
}

/*
 * U 盘启动时本机 NVMe 也可能有 BLOCKS.ID；MSC skip 必须看「启动盘」是否已有，
 * 不能只看「任意盘」。1=启动盘已有；0=启动盘尚无（别处可能有）；-1=旧 Boot 无 GUID。
 */
int FsBootDiskHasBlocksId(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();
    int i;

    if (!Info || !Info->BootDiskGuidOk) {
        return -1;
    }
    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].HasBlocksId) {
            continue;
        }
        if (CacheBootDiskMatch(gVols[i].Drive) == 1) {
            return 1;
        }
    }
    return 0;
}

static void NameEspSlot(FS_VOLUME *V, int BeforeCount, int SkipIdx) {
    (void)BeforeCount;
    (void)SkipIdx;
    StringCopy(V->Name, FS_VOL_NAME_MAX, "ESP");
    V->ReadOnly = 1;
}

static void NameBlocksIdVol(FS_VOLUME *V, UINT32 Drive, int Idx, int *BlocksVol) {
    int Msc = BlockDriveIsMsc(Drive);
    int Take = 0;
    int NewMatch;

    V->HasBlocksId = 1;
    /*
     * 分区名保留 BLOCKS（与 GPT 名 / 卷身份一致）；多份并存不改名 USB。
     * 默认卷用 BootDiskGuid（DiskGUID）区分；旧 Boot 无 GUID 时偏内置盘。
     * Files 侧栏撞名时靠盘号 dN / 字母路径区分。
     */
    StringCopy(V->Name, FS_VOL_NAME_MAX, "BLOCKS");
    NewMatch = CacheBootDiskMatch(Drive);
    if (*BlocksVol < 0) {
        Take = 1;
    } else if (NewMatch == 1) {
        Take = 1;
        LogFs("Fs: Default BLOCKS = Boot Disk\n");
    } else if (NewMatch == 0) {
        LogFs("Fs: Other Disk Also BLOCKS\n");
    } else if (!Msc && BlockDriveIsMsc(gVols[*BlocksVol].Drive)) {
        Take = 1;
        LogFs("Fs: Prefer Internal BLOCKS As Default\n");
    } else if (Msc && !BlockDriveIsMsc(gVols[*BlocksVol].Drive)) {
        LogFs("Fs: USB BLOCKS Not Default\n");
    }
    /* 其余歧义：保留先挂上的为默认 */
    if (Take) {
        *BlocksVol = Idx;
    }
}

static int PickVolSlot(int HasId) {
    if (gVolCount < FS_MAX_VOLUMES) {
        return gVolCount;
    }
    /*
     * 卷表满：仍须留下 BLOCKS.ID。挤掉最后一个非系统卷、非 RES 槽；
     * 否则 NVMe 上多个 ESP/OEM FAT 会先占满，USB 系统卷永远挂不上。
     */
    if (!HasId) {
        LogFs("Fs: Skip Fat (Table Full)\n");
        return -1;
    }
    {
        int j;
        for (j = gVolCount - 1; j >= 0; j--) {
            if (gVols[j].HasBlocksId) {
                continue;
            }
            if (gVols[j].Ops && gVols[j].Ops->Synthetic) {
                continue;
            }
            LogFs("Fs: Displace Vol For BLOCKS.ID\n");
            return j;
        }
    }
    LogFs("Fs: Skip BLOCKS (No Slot To Displace)\n");
    return -1;
}

/* 挂上 Drive 上一个 FAT 分区；成功记入 gVols，并可能更新 *BlocksVol */
void FsMountOneFatPart(UINT32 Drive, const GPT_FAT_PART *Part, int *BlocksVol) {
    FS_VOLUME *V;
    int Idx;
    int IsEsp;
    UINT32 Start;
    int HasId = 0;
    UINT8 Tmp[64];
    UINTN Sz;

    if (!Part || !BlocksVol) {
        return;
    }
    IsEsp = Part->IsEsp;
    Start = Part->StartLba;

    if (gVirtualFileSystem.Select(FatFsOps()) != 0) {
        return;
    }
    if (!BlockSelect(Drive) || gVirtualFileSystem.Mount(Start) != FAT_OK) {
        return;
    }
    if (gVirtualFileSystem.ReadFile("BLOCKS.ID", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
        HasId = 1;
    }

    Idx = PickVolSlot(HasId);
    if (Idx < 0) {
        return;
    }

    V = &gVols[Idx];
    V->Drive = Drive;
    V->StartLba = Start;
    V->Letter = (char)('A' + Idx);
    V->ReadOnly = IsEsp ? 1 : 0;
    V->HasBlocksId = 0;
    V->DiskGuidOk = 0;
    V->PartGuidOk = 0;
    V->Ops = FatFsOps();
    V->Name[0] = V->Letter;
    V->Name[1] = 0;
    /* 缓存盘/分区 GUID（Block 已 Select）；供 ROOT.CFG / Files */
    if (Drive < BOOT_GUID_CACHE) {
        if (sDriveGuidOk[Drive] == 0) {
            if (GptCurrentDiskGuid(sDriveGuid[Drive])) {
                sDriveGuidOk[Drive] = 1;
            } else {
                sDriveGuidOk[Drive] = 2;
            }
        }
        if (sDriveGuidOk[Drive] == 1) {
            int g;
            for (g = 0; g < 16; g++) {
                V->DiskGuid[g] = sDriveGuid[Drive][g];
            }
            V->DiskGuidOk = 1;
        }
    } else if (GptCurrentDiskGuid(V->DiskGuid)) {
        V->DiskGuidOk = 1;
    }
    if (Part->PartGuidOk) {
        int g;
        for (g = 0; g < 16; g++) {
            V->PartGuid[g] = Part->PartGuid[g];
        }
        V->PartGuidOk = 1;
    }

    if (HasId) {
        NameBlocksIdVol(V, Drive, Idx, BlocksVol);
    } else if (IsEsp) {
        NameEspSlot(V, gVolCount, Idx);
    }

    if (Idx == gVolCount) {
        gVolCount++;
    }
    gActiveVol = Idx;
    gActiveOps = V->Ops;
    gActiveDrive = Drive;
    gActiveLba = Start;

    LogFs("Fs: Vol ");
    LogFs(V->Name);
    LogFs(" Letter=");
    LogFsHex32((UINT32)(UINT8)V->Letter);
    LogFs(" Drive=");
    LogFsHex32(Drive);
    LogFs(" Lba=");
    LogFsHex32(Start);
    LogFs("\n");
}

void FsMountAddResVolume(void) {
    FS_VOLUME *V;
    int Idx;

    if (gVolCount >= FS_MAX_VOLUMES || !ResourceFileSystemOps()) {
        return;
    }
    V = &gVols[gVolCount];
    Idx = gVolCount;
    V->Drive = 0xFFFFFFFEu;
    V->StartLba = 0;
    V->Letter = (char)('A' + Idx);
    V->ReadOnly = 1;
    V->HasBlocksId = 0;
    V->DiskGuidOk = 0;
    V->PartGuidOk = 0;
    V->Ops = ResourceFileSystemOps();
    StringCopy(V->Name, FS_VOL_NAME_MAX, "RES");
    if (gVirtualFileSystem.Select(V->Ops) == 0 && gVirtualFileSystem.Mount(0) == FAT_OK) {
        gVolCount++;
        gActiveVol = Idx;
        gActiveOps = V->Ops;
        gActiveDrive = V->Drive;
        gActiveLba = 0;
        LogFs("Fs: Vol RES (Resfs)\n");
    }
}
