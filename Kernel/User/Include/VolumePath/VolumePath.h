/*
 * VolumePath.h — 用户态卷路径 / 列目录（libVolumePath）
 *
 * 底层仍是 F4：OpenDirectory / ReadDirectory / FileStat。
 * 不改变 syscall 号；不替代 <Host/DirectoryEntry.h> / open。
 *
 * 破坏性变更升 VOLUME_PATH_ABI_VERSION_MAJOR。
 */
#ifndef VOLUME_PATH_H
#define VOLUME_PATH_H

#include <Host/DirectoryEntry.h>

#define VOLUME_PATH_ABI_VERSION_MAJOR 2
#define VOLUME_PATH_ABI_VERSION_MINOR 0
#define VOLUME_PATH_ABI_VERSION_PATCH 0
#define VOLUME_PATH_ABI_VERSION_STRING "2.0.0"

#define FS_PATH_MAX 127
#define FS_VOL_BLOCKS "BLOCKS"
#define FS_VOL_ESP   "ESP"
#define FS_VOL_RES   "RES"

/* 目录 + 名 → Out。Name 已带卷前缀则原样拷贝。成功 0，失败 -1 */
int VolumePathJoin(char *Out, unsigned Cap, const char *Dir, const char *Name);
/* Vol + Rel → "BLOCKS:HELLO.ELF"。Rel 已带卷前缀则原样拷贝 */
int VolumePathCompose(char *Out, unsigned Cap, const char *Vol, const char *Rel);
/* 等价 VolumePathCompose(..., FS_VOL_BLOCKS, Rel) */
int VolumePathBlocks(char *Out, unsigned Cap, const char *Rel);
/* 路径是否含卷前缀（第一个 / 之前有 :） */
int VolumePathHasVolume(const char *Path);

/* 列目录到 Out[0..Max)。成功返回项数（0 表示空目录），失败 -1 */
int VolumePathListDirectory(const char *Path, FS_DIR_ENT *Out, int Max);
int VolumePathStat(const char *Path, FS_FILE_STAT *Out);
/* 1=目录，0=非目录，-1=不存在/失败 */
int VolumePathIsDirectory(const char *Path);

#endif
