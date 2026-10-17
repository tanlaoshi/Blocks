/*
 * InputOutput.c — FAT 扇区/簇 I/O、FAT 链
 * 卷几何初始化：Fat.c
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

UINT8 gSector[SECTOR];
UINT8 gCluster[SECTOR * 128];

UINT32 gStartLba;
UINT32 gBytesPerSector;
UINT32 gSectorsPerCluster;
UINT32 gReservedSectors;
UINT32 gNumFats;
UINT32 gSectorsPerFat;
UINT32 gRootCluster;
UINT32 gRootLba;
UINT32 gRootSectors;
UINT32 gFatStart;
UINT32 gDataStart;
UINT32 gFatType;
UINT32 gMaxCluster;

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

UINT32 FatAllocCluster(void) {
    UINT32 C;
    for (C = 2; C <= gMaxCluster; C++) {
        UINT32 V = FatNext(C);
        if (V == 0) {
            if (!FatSet(C, EocValue())) {
                return 0;
            }
            return C;
        }
    }
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
