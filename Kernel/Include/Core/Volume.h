/*
 * Volume.h — K44：多卷表 + 路径前缀（BLOCKS: / ESP: / A:）
 */
#ifndef VOLUME_H
#define VOLUME_H

#include "BootTypes.h"
#include "FatVolume.h"

#define VOLUME_MAX      4
#define VOLUME_NAME_MAX 12

typedef struct {
    int Used;
    int Drive; /* HalBlock 盘号 */
    UINT32 PartLba;
    char Name[VOLUME_NAME_MAX]; /* BLOCKS / ESP / A … */
    char Letter;                /* 'A'.. */
    int ReadOnly;
    int IsEsp;
} VOLUME;

int VolumeMountAll(void);
int VolumeCount(void);
const VOLUME *VolumeGet(int Index);
int VolumeDefaultIndex(void);
/* 解析 "BLOCKS:HELLO.ELF" → 激活卷，*OutPath="HELLO.ELF"；无前缀用默认卷 */
int VolumeResolve(const char *Path, const char **OutPath);
/* 打开已激活卷的 FAT_VOLUME（须先 Resolve 或 Activate） */
int VolumeOpenActive(FAT_VOLUME *V);
int VolumeActivate(int Index);
int VolumeActiveIndex(void);

#endif
