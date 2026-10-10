/*
 * StoreUi.h — K48：商店窗客户区（list + Install）
 */
#ifndef STORE_UI_H
#define STORE_UI_H

#include "BootTypes.h"

void StoreUiPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch);
/* 1=状态变了需重画 */
int StoreUiClick(INT32 X, INT32 Y);
void StoreUiRefresh(void);

#endif
