/*
 * VolumePath.h — 用户态卷路径 / 列目录（libVolumePath）
 *
 * 底层仍是 F4：OpenDirectory / ReadDirectory / FileStat。
 * 不改变 syscall 号；不替代 <dirent.h> / open。
 *
 * 破坏性变更升 FS_UTIL_ABI_VERSION_MAJOR。
 */
#ifndef FS_UTIL_H
#define FS_UTIL_H

#include <dirent.h>

#define FS_UTIL_ABI_VERSION_MAJOR 2
#define FS_UTIL_ABI_VERSION_MINOR 0
#define FS_UTIL_ABI_VERSION_PATCH 0
#define FS_UTIL_ABI_VERSION_STRING "2.0.0"

#define FS_PATH_MAX 127
#define FS_VOL_BLOCKS "BLOCKS"
#define FS_VOL_ESP   "ESP"
#define FS_VOL_RES   "RES"

/* 目录 + 名 → Out。Name 已带卷前缀则原样拷贝。成功 0，失败 -1 */
int FsUtilJoin(char *Out, unsigned Cap, const char *Dir, const char *Name);
/* Vol + Rel → "BLOCKS:HELLO.ELF"。Rel 已带卷前缀则原样拷贝 */
int FsUtilVolumePath(char *Out, unsigned Cap, const char *Vol, const char *Rel);
/* 等价 FsUtilVolumePath(..., FS_VOL_BLOCKS, Rel) */
int FsUtilBlocksPath(char *Out, unsigned Cap, const char *Rel);
/* 路径是否含卷前缀（第一个 / 之前有 :） */
int FsUtilHasVolume(const char *Path);

/* 列目录到 Out[0..Max)。成功返回项数（0 表示空目录），失败 -1 */
int FsUtilListDir(const char *Path, FS_DIR_ENT *Out, int Max);
int FsUtilStat(const char *Path, FS_FILE_STAT *Out);
/* 1=目录，0=非目录，-1=不存在/失败 */
int FsUtilIsDir(const char *Path);

#endif
