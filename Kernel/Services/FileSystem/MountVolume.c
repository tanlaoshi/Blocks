/*
 * MountVolume.c — 单 FAT 分区挂载 + RES（PR-F-fs-1）
 * 核心：Mount.c
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "BlockMux.h"
#include "Gpt.h"
#include "BootInfo.h"
#include "VirtualFileSystem.h"
#include "Debug.h"
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
    const BOOT_INFO *Info = BootInfoGet();

    if (!Info || !Info->BootDiskGuidOk) {
        return -1;
    }
    if (Drive >= BOOT_GUID_CACHE) {
        return 0;
    }
    if (sDriveGuidOk[Drive] == 0) {
        if (!GptCurrentDiskGuid(sDriveGuid[Drive])) {
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

static void NameEspSlot(FS_VOLUME *V, int BeforeCount, int SkipIdx) {
    int EspN = 0;
    int j;

    for (j = 0; j < BeforeCount; j++) {
        if (j == SkipIdx) {
            continue;
        }
        if (gVols[j].Name[0] == 'E' && gVols[j].Name[1] == 'S' &&
            gVols[j].Name[2] == 'P') {
            EspN++;
        }
    }
    if (EspN == 0) {
        StringCopy(V->Name, FS_VOL_NAME_MAX, "ESP");
    } else {
        V->Name[0] = 'E';
        V->Name[1] = 'S';
        V->Name[2] = 'P';
        V->Name[3] = (char)('0' + (EspN + 1 > 9 ? 9 : EspN + 1));
        V->Name[4] = 0;
    }
    V->ReadOnly = 1;
}

static void NameToyIdVol(FS_VOLUME *V, UINT32 Drive, int Idx, int *ToyVol) {
    int Msc = BlockDriveIsMsc(Drive);
    int Take = 0;
    int NewMatch;
    int OldMatch = 0;

    V->HasToyId = 1;
    /*
     * 多份 BLOCKS.ID：默认卷跟加载 EFI 的那块盘，分辨率和设置写回同一处。
     * 旧 Boot 没有 Disk GUID 时，仍跟内置盘。另一份改名 USB:。
     */
    NewMatch = CacheBootDiskMatch(Drive);
    if (*ToyVol >= 0 && gVols[*ToyVol].Drive < BOOT_GUID_CACHE &&
        sDriveGuidOk[gVols[*ToyVol].Drive] == 1) {
        const BOOT_INFO *Info = BootInfoGet();
        OldMatch = Info && Info->BootDiskGuidOk &&
                   GuidEq16(sDriveGuid[gVols[*ToyVol].Drive], Info->BootDiskGuid);
    }
    if (*ToyVol < 0) {
        Take = 1;
    } else if (NewMatch == 1 && !OldMatch) {
        StringCopy(gVols[*ToyVol].Name, FS_VOL_NAME_MAX, "USB");
        Take = 1;
        LogFs("Fs: Prefer Boot Disk BLOCKS\n");
    } else if (NewMatch == 0 && OldMatch) {
        StringCopy(V->Name, FS_VOL_NAME_MAX, "USB");
        LogFs("Fs: Other BLOCKS Kept As USB:\n");
    } else if (NewMatch < 0 && !Msc && BlockDriveIsMsc(gVols[*ToyVol].Drive)) {
        StringCopy(gVols[*ToyVol].Name, FS_VOL_NAME_MAX, "USB");
        Take = 1;
        LogFs("Fs: Prefer Internal BLOCKS Over USB\n");
    } else if (NewMatch < 0 && Msc && !BlockDriveIsMsc(gVols[*ToyVol].Drive)) {
        StringCopy(V->Name, FS_VOL_NAME_MAX, "USB");
        LogFs("Fs: USB BLOCKS Kept As USB:\n");
    } else {
        Take = 1;
    }
    if (Take) {
        StringCopy(V->Name, FS_VOL_NAME_MAX, "BLOCKS");
        *ToyVol = Idx;
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
            if (gVols[j].HasToyId) {
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

/* 挂上 Drive 上一个 FAT 分区；成功记入 gVols，并可能更新 *ToyVol */
void FsMountOneFatPart(UINT32 Drive, const GPT_FAT_PART *Part, int *ToyVol) {
    FS_VOLUME *V;
    int Idx;
    int IsEsp;
    UINT32 Start;
    int HasId = 0;
    UINT8 Tmp[64];
    UINTN Sz;

    if (!Part || !ToyVol) {
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
    V->HasToyId = 0;
    V->Ops = FatFsOps();
    V->Name[0] = V->Letter;
    V->Name[1] = 0;

    if (HasId) {
        NameToyIdVol(V, Drive, Idx, ToyVol);
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

    DebugWrite("Fs: vol ");
    DebugWrite(V->Name);
    DebugWrite(" letter=");
    DebugHex32((UINT32)(UINT8)V->Letter);
    DebugWrite(" drive=");
    DebugHex32(Drive);
    DebugWrite(" lba=");
    DebugHex32(Start);
    DebugWrite("\n");
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
    V->HasToyId = 0;
    V->Ops = ResourceFileSystemOps();
    StringCopy(V->Name, FS_VOL_NAME_MAX, "RES");
    if (gVirtualFileSystem.Select(V->Ops) == 0 && gVirtualFileSystem.Mount(0) == FAT_OK) {
        gVolCount++;
        gActiveVol = Idx;
        gActiveOps = V->Ops;
        gActiveDrive = V->Drive;
        gActiveLba = 0;
        DebugWrite("Fs: vol RES (resfs)\n");
        LogFs("Fs: RES Volume RES:\n");
    }
}
