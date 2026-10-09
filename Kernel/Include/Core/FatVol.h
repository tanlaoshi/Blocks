/*
 * FatVol.h — FAT 卷打开 / 根目录遍历 / 写簇辅助（File·Dir·Mut 共用）
 */
#ifndef FAT_VOL_H
#define FAT_VOL_H

#include "BootTypes.h"

#define FAT_SECTOR 512u
#define FAT_ATTR_DIR  0x10u
#define FAT_ATTR_ARCH 0x20u

typedef struct {
    UINT8 Spc;
    UINT8 Nfats;
    UINT32 FatLba;
    UINT32 FatSz; /* 每张 FAT 扇区数 */
    UINT32 RootLba;
    UINT32 RootSecs;
    UINT32 DataLba;
    UINT32 FatBits;
    UINT32 RootClus;
} FAT_VOL;

typedef int (*FAT_DIR_FN)(const UINT8 *Ent32, void *Ctx);

int FatVolOpen(FAT_VOL *V);
UINT32 FatVolNext(const FAT_VOL *V, UINT32 Clus);
int FatVolSetNext(FAT_VOL *V, UINT32 Clus, UINT32 Next);
UINT32 FatVolAlloc(FAT_VOL *V);
UINT32 FatVolClusLba(const FAT_VOL *V, UINT32 Clus);
int FatVolWriteCluster(FAT_VOL *V, UINT32 Clus, const UINT8 *Data, UINT32 Len);
int FatVolWalkRoot(FAT_VOL *V, FAT_DIR_FN Fn, void *Ctx);
void FatName83ToDisplay(const UINT8 *N83, char Out[13]);
int FatPathTo83(const char *Path, char Name83[11]);
UINT16 FatRd16(const UINT8 *P);
UINT32 FatRd32(const UINT8 *P);
void FatWr16(UINT8 *P, UINT16 V);
void FatWr32(UINT8 *P, UINT32 V);

#endif
