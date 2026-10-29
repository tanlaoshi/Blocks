/*
 * FontLayout.h — FT / stb 两条栅格路径共用的「拉丁加档 em」与「格内基线」。
 *
 * 基线只能有一个定义：缓存槽里只存位图，没有独立的 baseline 字段，绘制
 * 时整格左上角对齐。FT 与 stb 各自用 font metrics 现算，会把「向上取整」
 * 与「向零截断」的差（1px）带进 Oy，表现为英文顶部被削一行。
 */
#ifndef FONT_LAYOUT_H
#define FONT_LAYOUT_H

#include "Font.h"

/* 拉丁加档后的 ppem（逻辑格高 → 栅格 em），1× 用 CellH、2× 传 2×。 */
UINT32 FontTtfLatinEm(UINT32 CellH);

/*
 * 共用基线：格顶到基线的物理像素数。按 hhea ascent/descent 截断（与
 * stb 同口径），再与「加档拉丁的墨顶」取大，保证加档位图顶端不越界。
 * 返回 -1 表示当前没有可用的 stb 字体信息（调用方自行退回旧口径）。
 */
int FontTtfCellBaseline(UINT32 CellH);

#endif /* FONT_LAYOUT_H */
