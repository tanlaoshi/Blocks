/*
 * GuiFiles.h — K35：Files 窗客户区（根目录列表 / 点 ELF）
 */
#ifndef GUI_FILES_H
#define GUI_FILES_H

#include "BootTypes.h"

void GuiFilesRefresh(void);
void GuiFilesPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch);
/* 点中 ELF 则 exec，返回 1 */
int GuiFilesClick(INT32 X, INT32 Y);

#endif
