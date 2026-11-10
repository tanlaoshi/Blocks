/*
 * FatFreeChain.c — 释链（分段两趟 + 同扇区合并写，减 MSC FAT 税）
 *
 * 旧路径每簇 FatSet → 双份 FAT 各 Load+Store，删 1.9MB .bak 可占 install 数秒。
 * 先 FatNext 收齐一段簇号，再按扇区脏缓冲写回（两份 FAT 镜像）。
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

#define FAT_FREE_WALK_MAX 4096u

static UINT32 sFreeWalk[FAT_FREE_WALK_MAX];

static int FreeWalkStoreSector(UINT32 RelativeSector, UINT8 *Sector) {
    UINT32 FatIndex;

    for (FatIndex = 0; FatIndex < gNumFats; FatIndex++) {
        UINT32 FatLba = gFatStart + FatIndex * gSectorsPerFat + RelativeSector;

        if (!BlockWriteSectors(FatLba, 1, Sector)) {
            return 0;
        }
    }
    return 1;
}

static int FreeWalkApply(UINT32 Count) {
    UINT8 Sector[SECTOR];
    UINT32 DirtyRelative = FAT_CLUSTER_INVALID;
    int Dirty = 0;
    UINT32 Index;

    for (Index = 0; Index < Count; Index++) {
        UINT32 Cluster = sFreeWalk[Index];
        UINT32 Relative;
        UINT32 Off;
        UINT32 RelativeSector;

        if (Cluster < 2u || Cluster > gMaxCluster) {
            continue;
        }
        if (gFatType == 32) {
            Relative = Cluster * 4u;
            Off = Relative % SECTOR;
            RelativeSector = Relative / SECTOR;
        } else {
            Relative = Cluster * 2u;
            Off = Relative % SECTOR;
            RelativeSector = Relative / SECTOR;
        }
        if (Dirty && RelativeSector != DirtyRelative) {
            if (!FreeWalkStoreSector(DirtyRelative, Sector)) {
                return 0;
            }
            Dirty = 0;
        }
        if (!Dirty) {
            if (!BlockReadSectors(gFatStart + RelativeSector, 1, Sector)) {
                return 0;
            }
            DirtyRelative = RelativeSector;
            Dirty = 1;
        }
        if (gFatType == 32) {
            Write32(Sector + Off, 0);
        } else {
            Write16(Sector + Off, 0);
        }
    }
    if (Dirty) {
        if (!FreeWalkStoreSector(DirtyRelative, Sector)) {
            return 0;
        }
    }
    return 1;
}

int FatFreeChain(UINT32 Cluster) {
    UINT32 Cursor = Cluster;

    FatSequentialWriteInvalidate();
    while (!ClusterEnd(Cursor) && Cursor >= 2u && Cursor <= gMaxCluster) {
        UINT32 Count = 0;

        while (Count < FAT_FREE_WALK_MAX && !ClusterEnd(Cursor) && Cursor >= 2u &&
               Cursor <= gMaxCluster) {
            UINT32 Next = FatNext(Cursor);

            if (Next == FAT_CLUSTER_INVALID) {
                return 0;
            }
            sFreeWalk[Count++] = Cursor;
            Cursor = Next;
        }
        if (Count == 0u) {
            break;
        }
        if (!FreeWalkApply(Count)) {
            return 0;
        }
    }
    return 1;
}
