/*
 * Layout.h — 桌面/窗几何描述 + 按分辨率自适应（只缩不放）
 *
 * 【初学者】
 * 设计稿钉在 1280×720；真 FB 更小时等比缩小令牌与窗，更大不放大。
 * 窗尺寸/落点在描述表（或 LAYOUT.CFG），绘制代码只问本头，不写死 520×300。
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include "BootTypes.h"

#define GUI_LAYOUT_REF_W 1280u
#define GUI_LAYOUT_REF_H 720u

/* 相对 Shell 的偏移 / 内容区绝对坐标 / 水平居中 */
#define GUI_PLACE_CENTER      0
#define GUI_PLACE_XY          1
#define GUI_PLACE_SHELL_DELTA 2

typedef struct {
    const char *Name; /* LAYOUT.CFG 键：shell/about/… */
    UINT32 DesignW;
    UINT32 DesignH;
    UINT32 MinW;
    UINT32 MinH;
    int Place; /* GUI_PLACE_* */
    INT32 DesignX;
    INT32 DesignY;
    int OpenByDefault;
} GUI_WIN_LAYOUT;

void LayoutSetFb(UINT32 W, UINT32 H);
/* 可选：读根目录 LAYOUT.CFG 覆盖设计宽高/栏高；无文件则用内建表 */
int LayoutLoadConfiguration(void);

UINT32 LayoutFbW(void);
UINT32 LayoutFbH(void);
UINT32 LayoutBarH(void);
UINT32 LayoutTitleH(void);
UINT32 LayoutCloseW(void);
UINT32 LayoutContentTop(void);
UINT32 LayoutContentBottom(void); /* 底栏顶边 Y（不含底栏） */

/* 设计像素 → 屏像素（scale≤1） */
UINT32 LayoutPx(UINT32 DesignPx);

const GUI_WIN_LAYOUT *LayoutWindowDesc(int WinId);
/* 按当前 FB 解析窗矩形到 Out* */
void LayoutResolveWindow(int WinId, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);

/* 桌面图标槽（设计坐标自适应） */
void LayoutIconSlot(int Slot, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
UINT32 LayoutIconTile(void);

/* 开始钮 / 菜单（底栏） */
void LayoutStartBtn(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
void LayoutStartMenu(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);

#endif
