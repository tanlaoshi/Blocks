/*
 * FsVol.h — K44：多卷表 + 路径前缀（BLOCKS: / ESP: / A:）
 */
#ifndef FS_VOL_H
#define FS_VOL_H

#include "BootTypes.h"
#include "FatVol.h"

#define FS_VOL_MAX      4
#define FS_VOL_NAME_MAX 12

typedef struct {
    int Used;
    int Drive; /* HalBlock 盘号 */
    UINT32 PartLba;
    char Name[FS_VOL_NAME_MAX]; /* BLOCKS / ESP / A … */
    char Letter;                /* 'A'.. */
    int ReadOnly;
    int IsEsp;
} FS_VOL;

int FsVolMountAll(void);
int FsVolCount(void);
const FS_VOL *FsVolGet(int Index);
int FsVolDefaultIndex(void);
/* 解析 "BLOCKS:HELLO.ELF" → 激活卷，*OutPath="HELLO.ELF"；无前缀用默认卷 */
int FsVolResolve(const char *Path, const char **OutPath);
/* 打开已激活卷的 FAT_VOL（须先 Resolve 或 Activate） */
int FsVolOpenActive(FAT_VOL *V);
int FsVolActivate(int Index);
int FsVolActiveIndex(void);

#endif
