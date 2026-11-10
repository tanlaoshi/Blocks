/*
 * PaintWiden.c — Store 列表加宽 / 列宽（自 PaintList.c 纯搬 · PR-NORM-16）
 */
#include "StoreUiPrivate.h"
#include "GuiPrivate.h"

static UINT32 StoreColAdv(void) {
    UINT32 Adv = FontAdvanceX();
    return (Adv < 8u) ? 8u : Adv;
}

/* 四列：已装、来源、编号、名称。宽度随字号变。 */
void StoreColumns(UINT32 *Src, UINT32 *Id, UINT32 *Name, UINT32 *Need) {
    UINT32 Adv = StoreColAdv();

    *Src = Adv * 6u;
    *Id = *Src + Adv * 8u;
    *Name = *Id + Adv * 12u;
    *Need = *Name + Adv * 14u + UI_LAYOUT_PAD * 2u + STORE_SB_W + UI_LAYOUT_GAP;
}

void StoreWidenList(UINT32 *Cw) {
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 Src;
    UINT32 Id;
    UINT32 Name;
    UINT32 Need;
    UINT32 Add;
    UINT32 MaxW;
    int Idx;
    int Grown;
    GUI_WINDOW *Win;

    Idx = gGuiWindowManager.FocusWin;
    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    Win = &gGuiWindowManager.Windows[Idx];
    if (Win->Kind != GUI_WIN_STORE) {
        return;
    }
    MaxW = gGuiWindowManager.ScreenWidth;
    if (MaxW > 8u) {
        MaxW -= 8u;
    }
    StoreColumns(&Src, &Id, &Name, &Need);
    /*
     * 详情栏按客户区三分之一长。只补「缺口」一次，多出来的宽度会再被详情吃掉，
     * 列表仍短一截。连补几次，直到列表放下四列或碰到屏幕边。
     */
    Grown = 0;
    for (;;) {
        UiLayoutTriple(*Cw, &SideW, &ListW, &DetailW);
        if (ListW >= Need || Win->Width >= MaxW) {
            break;
        }
        Add = Need - ListW;
        if (Win->Width + Add > MaxW) {
            Add = MaxW - Win->Width;
        }
        if (Add == 0) {
            break;
        }
        Win->Width += Add;
        *Cw += Add;
        Grown = 1;
    }
    /* 加宽后旧备份尺寸不够，整窗作废，避免拖动时右侧透出后窗按钮 */
    if (Grown) {
        gGuiWindowManager.WinBackupValid[Idx] = 0;
    }
}

