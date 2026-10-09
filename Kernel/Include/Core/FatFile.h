/*
 * FatFile.h — FAT 根目录读/写（K19 读；K23 列举；K24 写）
 *
 * 块设备须已 HalBlockInit。只根目录；8.3 名（跳过 LFN）。
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

int FatDirListRoot(FAT_DIR_ENT *Out, UINT32 Cap, UINT32 *Count);
int FatFileReadPath(const char *Path, void *Buf, UINT32 Cap, UINT32 *OutSize);
int FatFileRead83(const char Name83[11], void *Buf, UINT32 Cap, UINT32 *OutSize);

/* K24：写小文件 / 建目录 / 删项（成功 0） */
int FatFileWritePath(const char *Path, const void *Buf, UINT32 Len);
int FatMkdirPath(const char *Path);
int FatRmPath(const char *Path);

#endif
