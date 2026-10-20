/*
 * UiLayout.h — 编译期布局表 + LoadWindow（不改 Ui.h ABI）
 */
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "Ui.h"

typedef enum {
    UI_WIDGET_LABEL,
    UI_WIDGET_BUTTON,
    UI_WIDGET_CHECKBOX,
    UI_WIDGET_TEXTBOX
} UI_WIDGET_KIND;

typedef struct {
    UI_WIDGET_KIND Kind;
    int Id;
    int X, Y, W, H;
    const char *Text;
} UI_WIDGET;

/* 设计坐标 × scale = 物理像素；Scale = ScreenW / DesignW（整数除法） */
int UiLoadWindow(const char *Title, int DesignW, int DesignH,
                    const UI_WIDGET *Widgets, int Count);

/* 屏宽/高；经 SYS_SCREEN_SIZE 查询，失败回退 1280/720（见 Tools/UiDesigner/README） */
int UiScreenWidth(void);
int UiScreenHeight(void);

/*
 * PR-UID-font：scale(千分) → 字号档位（1=默认, 2=×2, 3=×3）。
 * 只算档位；是否调 toy_set_font_id 由 App 自决（改全局字影响桌面/Shell）。
 */
int UiFontTier(int Scale1000);

#endif
