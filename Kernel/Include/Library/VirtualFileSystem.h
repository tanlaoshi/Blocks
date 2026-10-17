/*
 * VirtualFileSystem.h — 虚拟文件系统的后端表（PR-F1；PR-F2 FileStat/FileSync；PR-F3 多后端）
 *
 * 卷前缀 / 只读策略仍在 FileSystem；本头是「已激活卷」上的可插拔后端。
 * FAT 为第一个后端；RES 为可选只读资源卷第二后端。Common 业务走 Fs*。
 */
#ifndef VIRTUAL_FILE_SYSTEM_H
#define VIRTUAL_FILE_SYSTEM_H

#include "BootTypes.h"
#include "Fat.h"

typedef struct FS_OPS {
    const char *Name;
    /* 挂载：Block 后端以 StartLba 为卷起点；合成卷可忽略 StartLba */
    int (*Mount)(UINT32 StartLba);
    int (*ListDir)(const char *Path);
    int (*ListEntries)(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount);
    int (*ReadFile)(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
    int (*WriteFile)(const char *Path, const void *Buffer, UINTN Size);
    int (*DeleteFile)(const char *Path);
    int (*Mkdir)(const char *Path);
    int (*Rmdir)(const char *Path);
    int (*Rename)(const char *OldPath, const char *NewPath);
    /* PR-F2：可为 NULL（旧后端）；缺省 → FAT_ERR_IO */
    int (*FileStat)(const char *Path, FAT_FILE_STAT *Out);
    int (*FileSync)(void);
    /* PR-F3：1 = 无 Block（合成卷）；0/缺省 = 需 BlockSelect + StartLba */
    int Synthetic;
    /* PR-U-stream-1：可为 NULL → VirtualFileSystem*At 返回 INVAL */
    int (*ReadFileAt)(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN);
    int (*WriteFileAt)(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN);
} FS_OPS;

#define VIRTUAL_FILE_SYSTEM_MAX_BACKENDS 4

/* 注册后端（可多次；首次亦为当前后端）。成功 0，满表 -1 */
int VirtualFileSystemRegister(const FS_OPS *Ops);
/* 切换当前后端（Activate 时按卷选择） */
int VirtualFileSystemSelect(const FS_OPS *Ops);
const FS_OPS *VirtualFileSystemOps(void);

/* 直接经表调用（已 Activate 的相对路径）；无后端 → FAT_ERR_IO */
int VirtualFileSystemMount(UINT32 StartLba);
int VirtualFileSystemListDir(const char *Path);
int VirtualFileSystemListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount);
int VirtualFileSystemReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int VirtualFileSystemWriteFile(const char *Path, const void *Buffer, UINTN Size);
int VirtualFileSystemDeleteFile(const char *Path);
int VirtualFileSystemMkdir(const char *Path);
int VirtualFileSystemRmdir(const char *Path);
int VirtualFileSystemRename(const char *OldPath, const char *NewPath);
int VirtualFileSystemFileStat(const char *Path, FAT_FILE_STAT *Out);
int VirtualFileSystemFileSync(void);
int VirtualFileSystemReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN);
int VirtualFileSystemWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN);

/* FAT 后端（Library/Fat）；FS=ram 时由 FileSystem/Student 提供同名 FatFsOps */
const FS_OPS *FatFsOps(void);
const FS_OPS *ResFsOps(void);
const FS_OPS *RamFsOps(void);

#endif
