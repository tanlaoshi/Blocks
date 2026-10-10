/*
 * Files.h — K35：Files 窗客户区（根目录列表 / 点 ELF）
 */
#ifndef FILES_H
#define FILES_H

#include "BootTypes.h"

void FilesRefresh(void);
void FilesPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch);
/* 点中 ELF 则 exec，返回 1 */
int FilesClick(INT32 X, INT32 Y);

#endif
