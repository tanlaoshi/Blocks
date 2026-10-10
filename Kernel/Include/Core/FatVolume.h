/*
 * FatVolume.h — FAT 卷打开 / 根目录遍历 / 写簇辅助（File·Dir·Mut 共用）
 */
#ifndef FAT_VOLUME_H
#define FAT_VOLUME_H

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
} FAT_VOLUME;

typedef int (*FAT_DIR_FN)(const UINT8 *Ent32, void *Ctx);

/* PartLba=0：整盘 BPB；否则分区起点 */
int FatVolumeOpenAt(FAT_VOLUME *V, UINT32 PartLba);
/* 兼容：在 LBA0 / 首个 MBR FAT 分区上打开 */
int FatVolumeOpen(FAT_VOLUME *V);
UINT32 FatVolumeNext(const FAT_VOLUME *V, UINT32 Clus);
int FatVolumeSetNext(FAT_VOLUME *V, UINT32 Clus, UINT32 Next);
UINT32 FatVolumeAlloc(FAT_VOLUME *V);
UINT32 FatVolumeClusLba(const FAT_VOLUME *V, UINT32 Clus);
int FatVolumeWriteCluster(FAT_VOLUME *V, UINT32 Clus, const UINT8 *Data, UINT32 Len);
int FatVolumeWalkRoot(FAT_VOLUME *V, FAT_DIR_FN Fn, void *Ctx);
void FatName83ToDisplay(const UINT8 *N83, char Out[13]);
int FatPathTo83(const char *Path, char Name83[11]);
UINT16 FatRd16(const UINT8 *P);
UINT32 FatRd32(const UINT8 *P);
void FatWr16(UINT8 *P, UINT16 V);
void FatWr32(UINT8 *P, UINT32 V);

#endif
