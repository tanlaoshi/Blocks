/*
 * FatFile.h — FAT 根目录读（K19 读文件；K23 列举）
 *
 * 块设备须已 HalBlockInit。只读；8.3 名（跳过 LFN）。
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
 * 按路径读根目录文件（如 "TOYOS.ID" / "HELLO.ELF"）。
 * 成功返回字节数；失败 -1。
 */
int FatFileReadPath(const char *Path, void *Buf, UINT32 Cap, UINT32 *OutSize);

/* 直接 11 字节 8.3（如 "HELLO   ELF"） */
int FatFileRead83(const char Name83[11], void *Buf, UINT32 Cap, UINT32 *OutSize);

#endif
