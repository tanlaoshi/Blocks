/*
 * BlitString.c — 用户窗客户区按坐标画字（SYS_GRAPHICS_DRAW_STRING）
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Font.h"

int GuiDrawStringUser(int Wid, UINT32 X, UINT32 Y, UINT32 Color, const char *Text) {
    GUI_WINDOW *Win;
    UINT32 ClientX;
    UINT32 ClientY;
    UINT32 ClientWidth;
    UINT32 ClientHeight;
    UINT32 MaxHeight;
    UINT32 ScreenX;
    UINT32 ScreenY;
    UINT32 TextWidth;
    UINT32 TextHeight;
    int Index;
    int ButtonIndex;
    int ButtonCount;

    if (!Text || Text[0] == 0) {
        return -1;
    }
    Index = UserWindowIndexAfterRaise(Wid);
    if (Index < 0) {
        return -1;
    }
    Win = &gGuiWindowManager.Windows[Index];
    if (Win->Width <= 2 + GUI_CLIENT_PAD * 2 ||
        Win->Height <= TITLE_HEIGHT + 1 + GUI_CLIENT_PAD * 2) {
        return -1;
    }
    ClientX = Win->X + 1 + GUI_CLIENT_PAD;
    ClientY = Win->Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
    ClientWidth = Win->Width - 2 - GUI_CLIENT_PAD * 2;
    ClientHeight = Win->Height - TITLE_HEIGHT - 1 - GUI_CLIENT_PAD * 2;
    MaxHeight = ClientHeight;
    ButtonCount = 0;
    for (ButtonIndex = 0; ButtonIndex < 4; ButtonIndex++) {
        if (gGuiWindowManager.UserSlot[Index].UserButtonUsed[ButtonIndex]) {
            ButtonCount++;
        }
    }
    if (ButtonCount > 0 && ClientHeight >= 48) {
        if (MaxHeight > 40) {
            MaxHeight -= 40;
        } else {
            MaxHeight = 0;
        }
    }
    if (X >= ClientWidth || Y >= MaxHeight) {
        return -1;
    }
    FontTtfPreheatUtf8(Text);
    ScreenX = ClientX + X;
    ScreenY = ClientY + Y;
    TextWidth = FontStringWidth(Text);
    TextHeight = FontAdvanceY();
    if (TextWidth == 0) {
        TextWidth = FontAdvanceX();
    }
    if (X + TextWidth > ClientWidth) {
        TextWidth = ClientWidth - X;
    }
    if (Y + TextHeight > MaxHeight) {
        TextHeight = MaxHeight - Y;
    }
    if (TextWidth == 0 || TextHeight == 0) {
        return -1;
    }
    GuiFrameBufferBegin();
    HalVideoSetClipOrigin(ClientX, ClientY, ClientWidth, MaxHeight, Win->Background);
    HalVideoDrawStringAt(ScreenX, ScreenY, Text, Color);
    HalVideoClearClip();
    GuiBackupSyncRect(ScreenX, ScreenY, TextWidth, TextHeight);
    GuiFrameBufferEnd();
    return 0;
}
