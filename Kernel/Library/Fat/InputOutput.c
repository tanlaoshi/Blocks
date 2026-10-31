/*
 * InputOutput.c — FAT 扇区/簇 I/O、FAT 链
 * 卷几何初始化：Fat.c
 */
#include "Fat.h"
#include "SerialLog.h"
#include "FatPrivate.h"
#include "Block.h"

/* 每卷一套上下文（PR-F5）：gFatCur 指向当前操作的卷。
 * 多方挂载互不覆盖；切换只换指针，不再靠重新 Mount 重置全局量。 */
FAT_VOL gFatVols[FAT_VOL_MAX];
FAT_VOL *gFatCur = &gFatVols[0];

FAT_DIR_CTX FatRootCtx(void) {
    FAT_DIR_CTX C;
    if (gFatType == 16) {
        C.IsFat16Root = 1;
        C.Cluster = 0;
    } else {
        C.IsFat16Root = 0;
        C.Cluster = gRootCluster;
    }
    return C;
}

UINT16 Read16(const UINT8 *P) {
    return (UINT16)P[0] | ((UINT16)P[1] << 8);
}

UINT32 Read32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) |
           ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

void Write16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)(V & 0xFF);
    P[1] = (UINT8)((V >> 8) & 0xFF);
}

void Write32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)(V & 0xFF);
    P[1] = (UINT8)((V >> 8) & 0xFF);
    P[2] = (UINT8)((V >> 16) & 0xFF);
    P[3] = (UINT8)((V >> 24) & 0xFF);
}

int LoadSector(UINT32 Lba) {
    return BlockReadSectors(Lba, 1, gSector);
}

int StoreSector(UINT32 Lba) {
    return BlockWriteSectors(Lba, 1, gSector);
}

int LoadCluster(UINT32 Cluster) {
    UINT32 Lba = gDataStart + (Cluster - 2) * gSectorsPerCluster;
    return BlockReadSectors(Lba, gSectorsPerCluster, gCluster);
}

int StoreCluster(UINT32 Cluster) {
    UINT32 Lba = gDataStart + (Cluster - 2) * gSectorsPerCluster;
    return BlockWriteSectors(Lba, gSectorsPerCluster, gCluster);
}

UINT32 ClusterBytes(void) {
    return gBytesPerSector * gSectorsPerCluster;
}

UINT32 FatNext(UINT32 Cluster) {
    UINT32 FatLba;
    UINT32 Off;
    if (gFatType == 32) {
        FatLba = gFatStart + (Cluster * 4) / SECTOR;
        Off = (Cluster * 4) % SECTOR;
        if (!LoadSector(FatLba)) {
            return FAT_CLUSTER_INVALID;
        }
        return Read32(gSector + Off) & FAT32_ENTRY_MASK;
    }
    FatLba = gFatStart + (Cluster * 2) / SECTOR;
    Off = (Cluster * 2) % SECTOR;
    if (!LoadSector(FatLba)) {
        return FAT_CLUSTER_INVALID;
    }
    return Read16(gSector + Off);
}

int FatSet(UINT32 Cluster, UINT32 Value) {
    UINT32 i;
    UINT32 Off;
    UINT32 Rel;

    if (Cluster < 2 || Cluster > gMaxCluster) {
        return 0;
    }
    if (gFatType == 32) {
        Rel = Cluster * 4;
        Off = Rel % SECTOR;
        for (i = 0; i < gNumFats; i++) {
            UINT32 FatLba = gFatStart + i * gSectorsPerFat + Rel / SECTOR;
                if (!LoadSector(FatLba)) {
                return 0;
            }
            Write32(gSector + Off, Value & FAT32_ENTRY_MASK);
            if (!StoreSector(FatLba)) {
                return 0;
            }
        }
        return 1;
    }
    Rel = Cluster * 2;
    Off = Rel % SECTOR;
    for (i = 0; i < gNumFats; i++) {
        UINT32 FatLba = gFatStart + i * gSectorsPerFat + Rel / SECTOR;
        if (!LoadSector(FatLba)) {
            return 0;
        }
        Write16(gSector + Off, (UINT16)Value);
        if (!StoreSector(FatLba)) {
            return 0;
        }
    }
    return 1;
}

int ClusterEnd(UINT32 Cluster) {
    if (gFatType == 32) {
        return Cluster >= FAT32_EOC;
    }
    return Cluster >= FAT16_EOC;
}

UINT32 EocValue(void) {
    return gFatType == 32 ? FAT32_EOC : FAT16_EOC;
}

int FatFreeChain(UINT32 Cluster) {
    while (!ClusterEnd(Cluster) && Cluster >= 2 && Cluster <= gMaxCluster) {
        UINT32 Next = FatNext(Cluster);
        if (Next == FAT_CLUSTER_INVALID) {
            return 0;
        }
        if (!FatSet(Cluster, 0)) {
            return 0;
        }
        Cluster = Next;
    }
    return 1;
}

/*
 * 分配提示（hint / rover）：每卷记住「下次从这里开始找」。
 *
 * 不在删除时回收提示：绕回 2 号簇的整圈扫描保证任何空洞仍会被找到，
 * 最坏只是某次分配从提示点往后找，语义（找不到才算满）不变。
 * 提示按卷槽保存，槽换了盘/起始 LBA 时回落到 2 号簇；同一卷重挂载时
 * 沿用旧提示——即使提示落在已用簇上，整圈扫描也照样正确。
 */
#define FAT_FIRST_DATA_CLUSTER 2u
static UINT32 sFatAllocHint[FAT_VOL_MAX];
static UINT32 sFatAllocHintStart[FAT_VOL_MAX];
static UINT32 sFatAllocHintDrive[FAT_VOL_MAX];
static UINT8  sFatAllocHintValid[FAT_VOL_MAX];

static UINT32 FatVolumeSlot(void) {
    UINT32 Slot;
    for (Slot = 0; Slot < FAT_VOL_MAX; Slot++) {
        if (&gFatVols[Slot] == gFatCur) {
            return Slot;
        }
    }
    return 0;
}

static UINT32 FatAllocHintTake(UINT32 Slot) {
    if (!sFatAllocHintValid[Slot] ||
        sFatAllocHintStart[Slot] != gStartLba ||
        sFatAllocHintDrive[Slot] != gFatCur->Drive) {
        sFatAllocHintValid[Slot] = 1;
        sFatAllocHintStart[Slot] = gStartLba;
        sFatAllocHintDrive[Slot] = gFatCur->Drive;
        sFatAllocHint[Slot] = FAT_FIRST_DATA_CLUSTER;
    }
    if (sFatAllocHint[Slot] < FAT_FIRST_DATA_CLUSTER ||
        sFatAllocHint[Slot] > gMaxCluster) {
        sFatAllocHint[Slot] = FAT_FIRST_DATA_CLUSTER;
    }
    return sFatAllocHint[Slot];
}

UINT32 FatAllocCluster(void) {
    UINT32 Slot;
    UINT32 Start;
    UINT32 C;

    if (gMaxCluster < FAT_FIRST_DATA_CLUSTER) {
        return 0;
    }
    Slot = FatVolumeSlot();
    Start = FatAllocHintTake(Slot);
    C = Start;
    do {
        if (FatNext(C) == 0) {
            if (!FatSet(C, EocValue())) {
                return 0;
            }
            sFatAllocHint[Slot] = (C >= gMaxCluster)
                                      ? FAT_FIRST_DATA_CLUSTER : C + 1;
            return C;
        }
        C = (C >= gMaxCluster) ? FAT_FIRST_DATA_CLUSTER : C + 1;
    } while (C != Start);
    return 0;
}
const char *FatStrError(int Err) {
    switch (Err) {
    case FAT_OK:            return "ok";
    case FAT_ERR_IO:        return "I/O error";
    case FAT_ERR_NOENT:     return "not found";
    case FAT_ERR_NOSPC:     return "no space";
    case FAT_ERR_NOTDIR:    return "not a directory";
    case FAT_ERR_ISDIR:     return "is a directory";
    case FAT_ERR_NOTEMPTY:  return "directory not empty";
    case FAT_ERR_EXIST:     return "exists";
    case FAT_ERR_INVAL:     return "invalid";
    case FAT_ERR_NAMETOOLONG: return "name too long";
    case FAT_ERR_FILE_TOO_BIG:      return "file too large";
    case FAT_ERR_ROFS:      return "read-only";
    case FAT_ERR_STORE:     return "cannot delete: use Store Remove";
    default:                return "error";
    }
}
