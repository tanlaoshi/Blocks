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
    /* PR-U-stream-1：可为 NULL → ReadFileAt / WriteFileAt 返回 INVAL */
    int (*ReadFileAt)(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN);
    int (*WriteFileAt)(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN);
} FS_OPS;

#define VIRTUAL_FILE_SYSTEM_MAX_BACKENDS 4

/*
 * 模块入口。字段是短名，实现在 VirtualFileSystem.c 里是 static。
 * 直接经当前后端调用；无后端 → FAT_ERR_IO。偏移读写的函数指针为空 → FAT_ERR_INVAL。
 */
typedef struct {
    /* 注册后端（可多次；首次亦为当前后端）。成功 0，满表 -1 */
    int (*Register)(const FS_OPS *Ops);
    /* 切换当前后端（Activate 时按卷选择） */
    int (*Select)(const FS_OPS *Ops);
    const FS_OPS *(*Ops)(void);
    int (*Mount)(UINT32 StartLba);
    int (*ListDir)(const char *Path);
    int (*ListEntries)(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount);
    int (*ReadFile)(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
    int (*WriteFile)(const char *Path, const void *Buffer, UINTN Size);
    int (*DeleteFile)(const char *Path);
    int (*Mkdir)(const char *Path);
    int (*Rmdir)(const char *Path);
    int (*Rename)(const char *OldPath, const char *NewPath);
    int (*FileStat)(const char *Path, FAT_FILE_STAT *Out);
    int (*FileSync)(void);
    int (*ReadFileAt)(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN);
    int (*WriteFileAt)(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN);
} VIRTUAL_FILE_SYSTEM_OPS;

extern VIRTUAL_FILE_SYSTEM_OPS gVirtualFileSystem;

/* FAT 后端（Library/Fat）；FS=ram 时由 FileSystem/Student 提供同名 FatFsOps */
const FS_OPS *FatFsOps(void);
const FS_OPS *ResourceFileSystemOps(void);
const FS_OPS *RamFsOps(void);

#endif
