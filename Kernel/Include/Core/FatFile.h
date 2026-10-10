/*
 * FatFile.h — FAT 根目录读写（读 + 写/建/删/改名）
 *
 * 【初学者】
 * - 分层：Core/FileSystem 对外头；实现分 FatFile/FatDirectory/FatWrite/…
 * - 写路径：FatFileWritePath / FatMakeDirectory / FatDeleteFile / FatRenamePath
 * - 块设备须已就绪；根目录 8.3（跳过 LFN）
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
int FatDirectoryListRoot(FAT_DIR_ENT *Out, UINT32 Cap, UINT32 *Count);

/*
 * 按路径读根目录文件（如 "BLOCKS.ID" / "HELLO.ELF"）。
 * 成功返回字节数；失败 -1。
 */
int FatFileReadPath(const char *Path, void *Buffer, UINT32 Capacity, UINT32 *OutSize);

/* 直接 11 字节 8.3（如 "HELLO   ELF"） */
int FatFileRead83(const char Name83[11], void *Buffer, UINT32 Capacity, UINT32 *OutSize);

/* K24：写/建/删根路径；成功 0 */
int FatFileWritePath(const char *Path, const void *Buffer, UINT32 Length);
int FatMakeDirectory(const char *Path);
int FatDeleteFile(const char *Path);
/* K45：同卷改名（根路径 / 可带 VOL:）；成功 0 */
int FatRenamePath(const char *OldPath, const char *NewPath);

#endif
