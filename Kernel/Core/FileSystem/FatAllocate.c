/*
 * FatAllocate.c — FAT 表链、分配簇、写簇数据
 *
 * 【初学者】
 * - 分层：Core/FileSystem；FatVolume 几何见 FatVolume.c
 * - 对外：FatVolumeSetNext / FatVolumeAllocate / FatVolumeClusterLba /
 *         FatVolumeWriteCluster
 * - 不做：读文件、目录项、释放簇链
 */
#include "FatVolume.h"
#include "HalBlock.h"

/*
 * EndOfChain — FAT16/32 簇链结束标记
 *
 * 做什么：按卷 FatBits 返回 EOC 值。
 * 谁调用：仅 FatVolumeAllocate。
 * 返回：EOC 常量
 */
static UINT32 EndOfChain(const FAT_VOLUME *Volume) {
    return (Volume->FatBits == 32) ? 0x0FFFFFFFu : 0xFFFFu;
}

/*
 * FatVolumeSetNext — 写 FAT 表项（所有 FAT 副本同步）
 *
 * 做什么：把 Cluster 的下一簇设为 Next；32 位保留高 4 位。
 * 谁调用：FatVolumeAllocate（标记新簇为 EOC）。
 * 前后文：兄弟 — FatVolumeNext（读链）。
 * 返回：0 成功；非 0 IO 失败
 */
int FatVolumeSetNext(FAT_VOLUME *Volume, UINT32 Cluster, UINT32 Next) {
    UINT8 Sector[FAT_SECTOR];
    UINT32 EntriesPerSector;
    UINT32 FatSectorRel;
    UINT32 Offset;
    UINT8 FatCopy;

    if (Volume == 0 || Cluster < 2u) {
        return -1;
    }
    if (Volume->FatBits == 32) {
        EntriesPerSector = FAT_SECTOR / 4u;
        FatSectorRel = Cluster / EntriesPerSector;
        Offset = (Cluster % EntriesPerSector) * 4u;
    } else {
        EntriesPerSector = FAT_SECTOR / 2u;
        FatSectorRel = Cluster / EntriesPerSector;
        Offset = (Cluster % EntriesPerSector) * 2u;
    }
    if (HalBlockRead(Volume->FatLba + FatSectorRel, Sector, 1) != 0) {
        return -1;
    }
    if (Volume->FatBits == 32) {
        FatWr32(Sector + Offset, Next & 0x0FFFFFFFu);
    } else {
        FatWr16(Sector + Offset, (UINT16)Next);
    }
    for (FatCopy = 0; FatCopy < Volume->Nfats; FatCopy++) {
        if (HalBlockWrite(Volume->FatLba + (UINT32)FatCopy * Volume->FatSz + FatSectorRel,
                          Sector, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * FatVolumeAllocate — 从 FAT 表找第一个空闲簇并标 EOC
 *
 * 做什么：扫描簇 2..Max；Next==0 视为空闲；写入 EOC。
 * 谁调用：FatFileWritePath / FatMakeDirectory（新建或扩簇）。
 * 前后文：前 — FatVolumeOpen；后 — FatVolumeWriteCluster。
 * 返回：簇号 ≥2；0 表示失败或无空闲
 */
UINT32 FatVolumeAllocate(FAT_VOLUME *Volume) {
    UINT32 Max;
    UINT32 Cluster;

    if (Volume == 0) {
        return 0;
    }
    if (Volume->FatBits == 32) {
        Max = Volume->FatSz * (FAT_SECTOR / 4u);
    } else {
        Max = Volume->FatSz * (FAT_SECTOR / 2u);
    }
    if (Max > 8192u) {
        Max = 8192u;
    }
    for (Cluster = 2u; Cluster < Max; Cluster++) {
        if (FatVolumeNext(Volume, Cluster) == 0) {
            if (FatVolumeSetNext(Volume, Cluster, EndOfChain(Volume)) == 0) {
                return Cluster;
            }
            return 0;
        }
    }
    return 0;
}

/*
 * FatVolumeClusterLba — 数据区某簇首扇区 LBA
 *
 * 做什么：DataLba + (Cluster-2)*Spc。
 * 谁调用：FatVolumeWriteCluster；FatDirectorySlotFind（FAT32 根）。
 * 返回：LBA；0 表示非法参数
 */
UINT32 FatVolumeClusterLba(const FAT_VOLUME *Volume, UINT32 Cluster) {
    if (Volume == 0 || Cluster < 2u) {
        return 0;
    }
    return Volume->DataLba + (Cluster - 2u) * (UINT32)Volume->Spc;
}

/*
 * FatVolumeWriteCluster — 把 Len 字节写入单簇（不足扇区补零）
 *
 * 做什么：按 Spc 写满整簇；Data 可短于簇大小。
 * 谁调用：FatFileWritePath / FatMakeDirectory。
 * 前后文：前 — FatVolumeAllocate；后 — FatDirectoryEntryFill。
 * 返回：0 成功；非 0 IO 失败
 */
int FatVolumeWriteCluster(FAT_VOLUME *Volume, UINT32 Cluster, const UINT8 *Data,
                          UINT32 Length) {
    UINT8 Sector[FAT_SECTOR];
    UINT32 LogicalBlock;
    UINT8 SectorInCluster;
    UINT32 Offset = 0;
    UINT32 Index;

    if (Volume == 0 || Cluster < 2u) {
        return -1;
    }
    LogicalBlock = FatVolumeClusterLba(Volume, Cluster);
    if (LogicalBlock == 0) {
        return -1;
    }
    for (SectorInCluster = 0; SectorInCluster < Volume->Spc; SectorInCluster++) {
        for (Index = 0; Index < FAT_SECTOR; Index++) {
            Sector[Index] = 0;
        }
        if (Data != 0 && Offset < Length) {
            UINT32 Chunk = Length - Offset;
            if (Chunk > FAT_SECTOR) {
                Chunk = FAT_SECTOR;
            }
            for (Index = 0; Index < Chunk; Index++) {
                Sector[Index] = Data[Offset + Index];
            }
            Offset += Chunk;
        }
        if (HalBlockWrite(LogicalBlock + SectorInCluster, Sector, 1) != 0) {
            return -1;
        }
    }
    return 0;
}
