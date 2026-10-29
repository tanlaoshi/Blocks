/*
 * Fat.c — 卷几何初始化
 * 扇区与簇：InputOutput.c
 */
#include "Fat.h"
#include "FatPrivate.h"

/*
 * 选卷：同盘同 LBA 复用已有上下文；否则找空位新建。
 * 必须先切 gFatCur 再读 BPB——扇区缓冲在卷上下文里。
 */
static int FatSelectVolume(UINT32 StartLba) {
    UINT32 Drive = BlockSelectedDrive();
    FAT_VOL *V = 0;
    int Free = -1;
    int i;

    for (i = 0; i < FAT_VOL_MAX; i++) {
        if (gFatVols[i].Ready != 0 &&
            gFatVols[i].StartLba == StartLba &&
            gFatVols[i].Drive == Drive) {
            V = &gFatVols[i];
            break;
        }
        if (Free < 0 && gFatVols[i].Ready == 0) {
            Free = i;
        }
    }
    if (!V) {
        if (Free < 0) {
            return FAT_ERR_NOSPC;
        }
        V = &gFatVols[Free];
        V->Ready = 0;
        V->StartLba = StartLba;
        V->Drive = Drive;
    }
    gFatCur = V;
    return FAT_OK;
}

static void FatZeroContext(FAT_VOL *V) {
    UINT8 *P = (UINT8 *)V;
    UINTN i;

    for (i = 0; i < sizeof(FAT_VOL); i++) {
        P[i] = 0;
    }
}

int FatInitialize(UINT32 StartLba) {
    UINT32 TotSec;
    UINT32 DataSectors;
    UINT32 Drive = BlockSelectedDrive();
    int St = FatSelectVolume(StartLba);
    FAT_VOL *V;

    if (St != FAT_OK) {
        return St;
    }
    V = gFatCur;
    /*
     * 每次都重读 BPB（一个扇区）：卷可能被格式化过，缓存几何会过期。
     * 分卷上下文保证重读只影响本卷，不会冲掉别的卷的缓冲。
     */
    FatZeroContext(V);
    V->StartLba = StartLba;
    V->Drive = Drive;
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
        V->Ready = 1;
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
    V->Ready = 1;
    return FAT_OK;
}
