/*
 * Files.h — Files 窗客户区 API
 *
 * 【初学者】
 * - 实现：Core/Gui/Files.c
 * - 入口：FilesRefresh / FilesPaintClient / FilesClick
 */
#ifndef FILES_H
#define FILES_H

#include "BootTypes.h"

void FilesRefresh(void);
void FilesPaintClient(UINT32 ClientX, UINT32 ClientY, UINT32 ClientWidth,
                      UINT32 ClientHeight);
/* 点中已选 ELF 则 exec；返回 1 表示需重画 */
int FilesClick(INT32 X, INT32 Y);

#endif
