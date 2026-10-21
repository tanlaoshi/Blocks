/*
 * FileSystem.h — 文件系统模块（usb-map / story-s3 / PR-FS2 多卷）
 *
 * 开机 FileSystemInitialize；桌面 HotPoll 拔/插（UsbMsc + Clock）；Shell `msc hot`。
 * BootMediumGone：启动盘已拔且本会话曾见其上卷 → 失根（应 Park；非旁路热拔）。
 * 路径：`BLOCKS:…`；链图 FileSystem.c / MountOs.c / Clock.c / UsbMsc.c 头。
 */
#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include "BootTypes.h"
#include "Fat.h"

#define FS_MAX_VOLUMES 8 /* NUC：NVMe 多 FAT + Live USB ESP/BLOCKS；4 易挤掉系统卷 */
#define FS_VOL_NAME_MAX 8

/* 0 成功；-1 注册失败或最终仍无卷（模块表会停机） */
int FileSystemInitialize(void);

/* 1 = 这条路径现在不能直接改。没登记则不拦截。 */
void FileSystemRegisterPathGuard(int (*Blocked)(const char *Path));

/* PR-H-msc-6：Block 后端变更后重扫卷（不清 gVirtualFileSystem.Register）；成功 1 */
int FileSystemRemountVolumes(void);

/* 卷数；下标 0..Count-1 */
int FileSystemVolCount(void);
int FileSystemDefaultVol(void);
/* OsTarget 卷下标；未挂上（含启动介质已拔）为 -1；≠ 默认浏览卷 */
int FileSystemOsVol(void);
/* 1=启动盘已拔且根曾在其上：会话失根，应停机（非旁路 U 盘热拔） */
int FileSystemBootMediumGone(void);
/* Name 如 "BLOCKS"/"A"/"RES"；Flags 可选输出只读等，可为 NULL */
int FileSystemVolInfo(int Idx, char *Name, int NameMax, UINT32 *Drive,
                      UINT32 *StartLba, int *ReadOnly);
/* 卷所在盘 GPT DiskGUID（混端 UUID 串，Buf 建议 ≥37）；无则 -1 */
int FileSystemVolDiskGuidStr(int Idx, char *Buf, int BufMax);
/* fs-msc-defer：旁路卷已就绪；0 时 Files 只展 OsDisk */
int FileSystemSecondaryVolsReady(void);
/* 1=该卷在 OsTarget 所在盘（无 OsDisk 时恒 1） */
int FileSystemVolOnOsDisk(int Idx);
/* 1=该卷在 BootDiskGuid 盘上；0=否；-1=无 GUID */
int FileSystemVolOnBootDisk(int Idx);
/* 1=卷有 BLOCKS.ID */
int FileSystemVolHasBlocksId(int Idx);
/* 桌面后慢认旁路 MSC；一次生效 */
void FileSystemEnsureSecondaryVolumes(void);
/* PR-F3：后端名 fat / res */
int FileSystemVolBackend(int Idx, const char **OutName);

/* 解析 Path → 卷下标 + 卷内相对路径（可指向 Path 内子串） */
int FileSystemResolve(const char *Path, int *OutVol, const char **OutRel);

/* 切换 Block + FatInitialize 到指定卷；成功 FAT_OK */
int FileSystemActivate(int VolIdx);

/* 带前缀的路径操作（内部 Resolve+Activate+VirtualFileSystem*；写操作拒只读卷） */
int FileSystemListDirectory(const char *Path);
/* PR-FB1：结构化目录枚举（供文件浏览器） */
int FileSystemListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount);
int FileSystemReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int FileSystemWriteFile(const char *Path, const void *Buffer, UINTN Size);
/* PR-U-stream-1 */
int FileSystemReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN);
int FileSystemWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN);
int FileSystemDeleteFile(const char *Path);
int FileSystemMakeDirectory(const char *Path);
int FileSystemRemoveDirectory(const char *Path);
/* PR-S-bundle-fs：逐级建目录；已存在目录视为成功；中间为文件 → NOTDIR */
int FileSystemMakePath(const char *Path);
/* PR-S-bundle-fs：递归删文件或目录树（空/非空目录均可）；托管载荷 → STORE */
int FileSystemRemoveTree(const char *Path);
int FileSystemRename(const char *OldPath, const char *NewPath);
/* PR-F2：文件状态查询 / 落盘同步（名称写全，勿用孤立 stat/fsync） */
int FileSystemFileStat(const char *Path, FAT_FILE_STAT *Out);
int FileSystemFileSync(const char *Path);
/* PR-F3：FAT 子目录簇扩展回归（RES 等合成卷 → ROFS/INVAL） */
int FileSystemDirStress(const char *Path, int MaxFiles, int *OutCreated, int *OutGrew);

#endif
