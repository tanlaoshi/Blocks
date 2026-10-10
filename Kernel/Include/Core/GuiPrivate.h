/*
 * GuiPrivate.h — Gui 夹内共享（光标↔指针）
 *
 * 【初学者】
 * - 分层：Core/Gui 内部；对外 API 在 Gui.h（GuiInitialize / GuiPoll / GuiCursor*）
 * - 本头：Cursor* 状态与 PointerReset；Console 等只应 include Gui.h
 */
#ifndef GUI_PRIVATE_H
#define GUI_PRIVATE_H

#include "BootTypes.h"

void CursorSetFramebuffer(UINT32 Width, UINT32 Height);
void CursorEnableAt(INT32 X, INT32 Y);
int CursorIsEnabled(void);
void CursorGetPosition(INT32 *X, INT32 *Y);
void CursorSetPosition(INT32 X, INT32 Y);
void CursorMoveBy(INT32 DeltaX, INT32 DeltaY);
void CursorSetFromAbsolute(INT32 PacketX, INT32 PacketY);
void CursorClamp(void);
void PointerReset(void);

#endif
