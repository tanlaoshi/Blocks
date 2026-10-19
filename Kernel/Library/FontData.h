/*
 * FontData.h — 兼容转发（PR-D1）
 *
 * 点阵数据在 Library/Fonts/；请改用 Include/Font.h。
 * 本头仅保留旧宏名，映射到当前字体度量，便于过渡。
 */
#ifndef FONT_DATA_H
#define FONT_DATA_H

#include "Font.h"

#define FONT_ADVANCE_X FontAdvanceX()
#define FONT_ADVANCE_Y FontAdvanceY()
#define FONT_CELL_W    FontCellW()
#define FONT_CELL_H    FontCellH()

#endif /* FONT_DATA_H */
