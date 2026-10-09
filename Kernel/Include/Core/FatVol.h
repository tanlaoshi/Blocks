/*
 * FatVol.h — FAT 卷打开 / 根目录遍历（File 与 Dir 共用）
 */
#ifndef FAT_VOL_H
#define FAT_VOL_H

#include "BootTypes.h"

#define FAT_SECTOR 512u

typedef struct {
    UINT8 Spc;
    UINT32 FatLba;
    UINT32 RootLba;
    UINT32 RootSecs;
    UINT32 DataLba;
    UINT32 FatBits;
    UINT32 RootClus;
} FAT_VOL;

typedef int (*FAT_DIR_FN)(const UINT8 *Ent32, void *Ctx);

int FatVolOpen(FAT_VOL *V);
UINT32 FatVolNext(const FAT_VOL *V, UINT32 Clus);
int FatVolWalkRoot(FAT_VOL *V, FAT_DIR_FN Fn, void *Ctx);
void FatName83ToDisplay(const UINT8 *N83, char Out[13]);
int FatPathTo83(const char *Path, char Name83[11]);
UINT16 FatRd16(const UINT8 *P);
UINT32 FatRd32(const UINT8 *P);

#endif
