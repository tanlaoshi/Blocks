/*
 * Blocks/Dir.h — 目录 API（selfhost；Dir* 本位）
 */
#ifndef BLOCKS_DIR_H
#define BLOCKS_DIR_H

#include <stddef.h>

#define FS_ENT_NAME_MAX 64
#define FS_ATTR_RO      0x01
#define FS_ATTR_DIR     0x10
#define FS_LIST_MAX     64

typedef struct {
    char          Name[FS_ENT_NAME_MAX];
    unsigned char Attr;
    unsigned      Size;
} FS_DIR_ENT;

typedef struct {
    unsigned char Attr;
    unsigned      Size;
    unsigned      Cluster;
} FS_FILE_STAT;

typedef struct FsDirectory FS_DIR;

FS_DIR *DirOpen(const char *path);
int DirRead(FS_DIR *dir, FS_DIR_ENT *out);
int DirClose(FS_DIR *dir);
int FileStat(const char *path, FS_FILE_STAT *out);

#endif
