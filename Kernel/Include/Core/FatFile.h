/*
 * FatFile.h — FAT 根目录读写（K19/K23 读；K24 写；K37 THEME.CFG）
 *
 * 块设备须已 HalBlockInit。根目录 8.3（跳过 LFN）。
 */
#ifndef FAT_FILE_H
#define FAT_FILE_H

#include "BootTypes.h"

#define FAT_NAME_MAX 13

typedef struct {
    char Name[FAT_NAME_MAX]; /* 如 "HELLO.ELF" */
    UINT32 Size;
    int IsDir;
} FAT_DIR_ENT;

/* 列根目录到 Out[0..Cap)；*Count 实际条数；成功 0 */
int FatDirListRoot(FAT_DIR_ENT *Out, UINT32 Cap, UINT32 *Count);

/*
 * 按路径读根目录文件（如 "BLOCKS.ID" / "HELLO.ELF"）。
 * 成功返回字节数；失败 -1。
 */
int FatFileReadPath(const char *Path, void *Buf, UINT32 Cap, UINT32 *OutSize);

/* 直接 11 字节 8.3（如 "HELLO   ELF"） */
int FatFileRead83(const char Name83[11], void *Buf, UINT32 Cap, UINT32 *OutSize);

/* K24：写/建/删根路径；成功 0 */
int FatFileWritePath(const char *Path, const void *Buf, UINT32 Len);
int FatMkdirPath(const char *Path);
int FatRmPath(const char *Path);

#endif
