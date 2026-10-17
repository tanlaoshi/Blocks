/*
 * Fat.c — 卷几何初始化
 * 扇区与簇：InputOutput.c
 */
#include "Fat.h"
#include "FatPrivate.h"

int FatInitialize(UINT32 StartLba) {
    UINT32 TotSec;
    UINT32 DataSectors;

    gStartLba = StartLba;
    if (!LoadSector(StartLba)) {
        return FAT_ERR_IO;
    }
    gBytesPerSector = Read16(gSector + 11);
    gSectorsPerCluster = gSector[13];
    gReservedSectors = Read16(gSector + 14);
    gNumFats = gSector[16];
    gFatType = 0;

    if (gBytesPerSector != SECTOR || gSectorsPerCluster == 0 || gSectorsPerCluster > 128) {
        return FAT_ERR_INVAL;
    }

    TotSec = Read16(gSector + 19);
    if (TotSec == 0) {
        TotSec = Read32(gSector + 32);
    }

    if (gSector[82] == 'F' && gSector[83] == 'A' && gSector[84] == 'T') {
        gFatType = 32;
        gSectorsPerFat = Read32(gSector + 36);
        gRootCluster = Read32(gSector + 44);
        gFatStart = StartLba + gReservedSectors;
        gDataStart = gFatStart + gNumFats * gSectorsPerFat;
        DataSectors = TotSec ? (TotSec - (gDataStart - StartLba)) : (gSectorsPerFat * 128);
        gMaxCluster = 2 + DataSectors / gSectorsPerCluster - 1;
        if (gMaxCluster < 3) {
            gMaxCluster = FAT_U16_MASK;
        }
        return FAT_OK;
    }

    gSectorsPerFat = Read16(gSector + 22);
    {
        UINT16 RootEntries = Read16(gSector + 17);
        gFatStart = StartLba + gReservedSectors;
        gRootLba = gFatStart + gNumFats * gSectorsPerFat;
        gRootSectors = ((UINT32)RootEntries * 32 + SECTOR - 1) / SECTOR;
        gDataStart = gRootLba + gRootSectors;
        DataSectors = TotSec ? (TotSec - (gDataStart - StartLba)) : (gSectorsPerFat * 256);
        gMaxCluster = 2 + DataSectors / gSectorsPerCluster - 1;
        if (gMaxCluster < 3) {
            gMaxCluster = FAT16_CLUSTERS_MAX;
        }
    }
    gFatType = 16;
    return FAT_OK;
}
