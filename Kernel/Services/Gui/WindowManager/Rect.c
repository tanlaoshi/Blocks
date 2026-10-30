/*
 * Rect.c — 窗口/桌面可用区只读矩形（`gui wr` 判据与开窗自测）
 *
 * 从哪读：GuiWindowRectGet / GuiDesktopUsableGet。
 * 别改：逻辑坐标是窗口模型的唯一单位；Phys* 只是按 GuiPhysRect 换算给人看，
 *       100%=恒等、150%=3/2、200%=2/1。
 */
#include "GuiPrivate.h"
#include "Desktop.h"

static void RectFill(GUI_RECT_INFO *Out, UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    if (!Out) {
        return;
    }
    Out->X = X;
    Out->Y = Y;
    Out->Width = W;
    Out->Height = H;
    GuiPhysRect(X, Y, W, H, &Out->PhysX, &Out->PhysY, &Out->PhysWidth,
                &Out->PhysHeight);
}

void GuiDesktopUsableGet(GUI_RECT_INFO *Out) {
    UINT32 Bar = DesktopTaskbarHeight();
    UINT32 Height = gGuiWindowManager.ScreenHeight;

    if (Height > Bar) {
        Height -= Bar;
    } else {
        Height = 0;
    }
    RectFill(Out, 0, 0, gGuiWindowManager.ScreenWidth, Height);
}

int GuiWindowRectGet(int Index, GUI_WINDOW_INFO *Out) {
    const GUI_WINDOW *Win;

    if (!Out || Index < 0 || Index >= MAX_WINS) {
        return 0;
    }
    Win = &gGuiWindowManager.Windows[Index];
    if (!Win->Active) {
        Out->Active = 0;
        return 0;
    }
    Out->Active = 1;
    Out->Title = Win->Title ? Win->Title : "";
    RectFill(&Out->Rect, Win->X, Win->Y, Win->Width, Win->Height);
    return 1;
}
