/*
 * FatSlot.c — 根目录槽位扫描 / 写目录项
 *
 * 【初学者】
 * - 分层：Core/FileSystem；对标现网 FatDirSlot
 * - 对外（夹内）：FatDirectorySlotFind / FatDirectoryEntryPut / FatDirectoryEntryFill
 * - 不做：子目录遍历、LFN、释放簇链
 */
#include "FatPrivate.h"
#include "FatFile.h"
#include "Volume.h"
#include "HalBlock.h"

/*
 * FatActiveVolumeOpen — 打开当前活动卷的 FAT_VOLUME
 *
 * 做什么：先试 Volume 已挂载句柄，失败再 FatVolumeOpen。
 * 谁调用：FatWrite / FatMakeDirectory / FatDelete / FatRename（写路径入口）。
 * 前后文：前 — VolumeMountAll / VolumeResolve；后 — FatDirectorySlotFind。
 * 返回：0 成功；非 0 失败
 */
int FatActiveVolumeOpen(FAT_VOLUME *Volume) {
    if (VolumeOpenActive(Volume) == 0) {
        return 0;
    }
    return FatVolumeOpen(Volume);
}

/*
 * FatActiveVolumeReadOnly — 活动卷是否只读
 *
 * 做什么：查 Volume 表 ReadOnly 标志（如 ESP）。
 * 谁调用：各 Fat* 写入口，只读则直接失败。
 * 前后文：兄弟 — FatActiveVolumeOpen。
 * 返回：1 只读；0 可写或无卷
 */
int FatActiveVolumeReadOnly(void) {
    const VOLUME *Volume = VolumeGet(VolumeActiveIndex());
    return (Volume != 0 && Volume->ReadOnly) ? 1 : 0;
}

/*
 * FatPathResolve83 — 路径 → 11 字节 8.3 名
 *
 * 做什么：剥 VOL: 前缀后 FatPathTo83；空相对路径失败。
 * 谁调用：FatWrite / FatMakeDirectory / FatDelete；Rename 自解析两路径。
 * 前后文：前 — VolumeResolve；后 — FatDirectorySlotFind。
 * 返回：0 成功；非 0 失败
 */
int FatPathResolve83(const char *Path, char Name83[11]) {
    const char *Relative = Path;
    if (Path == 0) {
        return -1;
    }
    (void)VolumeResolve(Path, &Relative);
    if (Relative == 0 || Relative[0] == 0) {
        return -1;
    }
    return FatPathTo83(Relative, Name83);
}

/*
 * MemoryEqual — 比较 Name83 与目录项名区
 *
 * 做什么：逐字节比 N 长；仅本文件 ScanSector 用。
 * 谁调用：仅 ScanSector。
 * 返回：1 相等；0 不等
 */
static int MemoryEqual(const UINT8 *A, const char *B, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        if (A[i] != (UINT8)B[i]) {
            return 0;
        }
    }
    return 1;
}

/*
 * FatMemoryZero — 清零缓冲区
 *
 * 做什么：写 0 到 P[0..N)。
 * 谁调用：FatDirectoryEntryFill；FatMakeDirectory（清点扇区）。
 * 返回：void
 */
void FatMemoryZero(UINT8 *P, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        P[i] = 0;
    }
}

/*
 * ScanSector — 扫一个目录扇：记命中项或首个空闲槽
 *
 * 做什么：遇 0x00 目录尾；0xE5/空为 Free；匹配 Want83 为 Have。
 * 谁调用：仅 FatDirectorySlotFind。
 * 返回：1 可停（命中或目录尾）；0 继续下一扇
 */
static int ScanSector(UINT8 *Sec, UINT32 Lba, FAT_SLOT *Slot) {
    UINTN Off;
    for (Off = 0; Off < FAT_SECTOR; Off += 32u) {
        if (Sec[Off] == 0x00u) {
            if (!Slot->Free) {
                Slot->Free = 1;
                Slot->FreeLba = Lba;
                Slot->FreeOff = (UINT32)Off;
            }
            return 1; /* 目录尾 */
        }
        if (Sec[Off] == 0xE5u) {
            if (!Slot->Free) {
                Slot->Free = 1;
                Slot->FreeLba = Lba;
                Slot->FreeOff = (UINT32)Off;
            }
            continue;
        }
        if ((Sec[Off + 11] & 0x08u) != 0 || (Sec[Off + 11] & 0x0Fu) == 0x0Fu) {
            continue;
        }
        if (MemoryEqual(Sec + Off, Slot->Want83, 11)) {
            Slot->Have = 1;
            Slot->EntLba = Lba;
            Slot->EntOff = (UINT32)Off;
            {
                UINTN i;
                for (i = 0; i < 32; i++) {
                    Slot->Ent[i] = Sec[Off + i];
                }
            }
            return 1;
        }
    }
    return 0;
}

/*
 * FatDirectorySlotFind — 在根目录找 Name83 或空闲槽
 *
 * 做什么：FAT12/16 扫 RootLba；FAT32 沿 RootClus 链扫（有 Guard）。
 * 谁调用：FatFileWritePath / FatMakeDirectory / FatDeleteFile / FatRenamePath。
 * 前后文：前 — FatPathResolve83 + FatActiveVolumeOpen；后 — Put/Fill 或标删除。
 * 返回：0 扫描完成（看 Slot->Have/Free）；-1 IO 失败
 */
int FatDirectorySlotFind(FAT_VOLUME *Volume, const char Name83[11], FAT_SLOT *Slot) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 i;
    UINT32 Guard;

    Slot->Want83 = Name83;
    Slot->Have = 0;
    Slot->Free = 0;
    if (Volume->FatBits != 32) {
        for (i = 0; i < Volume->RootSecs && i < 128u; i++) {
            if (HalBlockRead(Volume->RootLba + i, Sec, 1) != 0) {
                return -1;
            }
            if (ScanSector(Sec, Volume->RootLba + i, Slot)) {
                return 0;
            }
        }
        return 0;
    }
    {
        UINT32 Cluster = Volume->RootClus;
        for (Guard = 0; Guard < 64u && Cluster >= 2u; Guard++) {
            UINT32 Lba = FatVolumeClusterLba(Volume, Cluster);
            UINT8 SectorInCluster;
            for (SectorInCluster = 0; SectorInCluster < Volume->Spc; SectorInCluster++) {
                if (HalBlockRead(Lba + SectorInCluster, Sec, 1) != 0) {
                    return -1;
                }
                if (ScanSector(Sec, Lba + SectorInCluster, Slot)) {
                    return 0;
                }
            }
            Cluster = FatVolumeNext(Volume, Cluster);
            if (Cluster < 2u || Cluster >= 0x0FFFFFF8u) {
                break;
            }
        }
    }
    return 0;
}

/*
 * FatDirectoryEntryPut — 把 32 字节目录项写回磁盘扇区
 *
 * 做什么：读扇 → 覆盖 Off 起 32 字节 → 写扇。
 * 谁调用：FatWrite / FatMakeDirectory / FatRename（更新或新建项）。
 * 前后文：前 — FatDirectoryEntryFill 或改名拷贝；后 — 调用方返回。
 * 返回：0 成功；非 0 IO 失败
 */
int FatDirectoryEntryPut(UINT32 Lba, UINT32 Off, const UINT8 Ent[32]) {
    UINT8 Sec[FAT_SECTOR];
    UINT32 i;
    if (HalBlockRead(Lba, Sec, 1) != 0) {
        return -1;
    }
    for (i = 0; i < 32u; i++) {
        Sec[Off + i] = Ent[i];
    }
    return HalBlockWrite(Lba, Sec, 1);
}

/*
 * FatDirectoryEntryFill — 组装 32 字节短名目录项
 *
 * 做什么：填 8.3、属性、簇号、大小；其余清零。
 * 谁调用：FatFileWritePath / FatMakeDirectory。
 * 前后文：后 — FatDirectoryEntryPut。
 * 返回：void
 */
void FatDirectoryEntryFill(UINT8 Ent[32], const char Name83[11], UINT8 Attr,
                           UINT32 Cluster, UINT32 Size) {
    UINT32 i;
    FatMemoryZero(Ent, 32);
    for (i = 0; i < 11u; i++) {
        Ent[i] = (UINT8)Name83[i];
    }
    Ent[11] = Attr;
    FatWr16(Ent + 20, (UINT16)((Cluster >> 16) & 0xFFFFu));
    FatWr16(Ent + 26, (UINT16)(Cluster & 0xFFFFu));
    FatWr32(Ent + 28, Size);
}
