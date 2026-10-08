/*
 * FileSystem.h — 文件系统（K9：识盘最小）
 *
 * 本刀：认 Boot 交接的 TOYOS.ID 旗标。真 Block+FAT 探盘后刀替换实现。
 */
#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

int FileSystemInitialize(void);
/* 1 = 已见系统卷标记 */
int FileSystemHasToyOsId(void);

#endif
