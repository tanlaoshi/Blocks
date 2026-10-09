/*
 * FileSystem.h — 文件系统模块
 *
 * 内核侧 Block + FatProbe 识 TOYOS.ID；失败可退 Boot handoff 旗标。
 */
#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

int FileSystemInitialize(void);
/* 1 = 已见系统卷标记 */
int FileSystemHasToyOsId(void);

#endif
