/*
 * KsendFreezeWindow.c — NUC/Pi：热更大窗「正在热更」+ 锁输入
 *
 * 对表 BootGone 全屏对话框；Duo 不登记本 Ops（仅 Common 串口行）。
 */
#include "DesktopPrivate.h"
#include "Gui.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "Hal.h"
#include "HalDevices.h"
#include "HalVideo.h"
#include "Hal/KsendFreeze.h"

static void KsendFreezeWindowPaint(const char *Title, const char *Body) {
    UINT32 ScreenWidth = 0;
    UINT32 ScreenHeight = 0;
    UINT32 BoxWidth;
    UINT32 BoxHeight;
    UINT32 BoxX;
    UINT32 BoxY;
    UINT32 CellHeight;
    UINT32 TextWidth;
    UINT32 Padding = 32u;
    UINT32 Y;

    HalVideoGetSize(&ScreenWidth, &ScreenHeight);
    if (ScreenWidth < 320u || ScreenHeight < 200u) {
        ScreenWidth = 1024;
        ScreenHeight = 768;
    }
    CellHeight = FontCellH();
    if (CellHeight < 16u) {
        CellHeight = 16u;
    }
    BoxWidth = ScreenWidth * 3u / 5u;
    if (BoxWidth < 400u) {
        BoxWidth = ScreenWidth > 40u ? ScreenWidth - 40u : ScreenWidth;
    }
    if (BoxWidth > ScreenWidth - 40u) {
        BoxWidth = ScreenWidth > 40u ? ScreenWidth - 40u : ScreenWidth;
    }
    BoxHeight = Padding * 2u + CellHeight * 3u + 24u;
    BoxX = (ScreenWidth - BoxWidth) / 2u;
    BoxY = (ScreenHeight - BoxHeight) / 2u;

    HalVideoFillRect(0, 0, ScreenWidth, ScreenHeight, 0x00101018u);
    HalVideoFillRect(BoxX, BoxY, BoxWidth, BoxHeight, ThemeDialogFace());
    HalVideoFillRect(BoxX, BoxY, BoxWidth, 3u, ThemeDialogBorder());
    HalVideoFillRect(BoxX, BoxY + BoxHeight - 3u, BoxWidth, 3u,
                     ThemeDialogBorder());
    HalVideoFillRect(BoxX, BoxY, 3u, BoxHeight, ThemeDialogBorder());
    HalVideoFillRect(BoxX + BoxWidth - 3u, BoxY, 3u, BoxHeight,
                     ThemeDialogBorder());

    Y = BoxY + Padding;
    TextWidth = FontStringWidth(Title);
    HalVideoDrawStringAt(BoxX + (BoxWidth - TextWidth) / 2u, Y, Title,
                         ThemeText());
    Y += CellHeight + 16u;
    TextWidth = FontStringWidth(Body);
    HalVideoDrawStringAt(BoxX + (BoxWidth - TextWidth) / 2u, Y, Body,
                         ThemeTextMuted());
}

static void KsendFreezeWindowPaintShow(const char *Title, const char *Body) {
    /* 对表 BootGone / boot GOP：直写 scanout + dcache，勿只 PresentFlush */
    HalVideoDrawBeginFront();
    KsendFreezeWindowPaint(Title, Body);
    HalVideoDrawEndFront();
}

static void KsendFreezeWindowBegin(void) {
    const char *Title;
    const char *Body;

    Title = LocaleString(MESSAGE_KSEND_UPDATING_TITLE);
    Body = LocaleString(MESSAGE_KSEND_UPDATING_BODY);
    if (Title == 0 || Title[0] == 0) {
        Title = "正在热更";
    }
    if (Body == 0 || Body[0] == 0) {
        Body = "请稍候，勿断电";
    }

    /*
     * 对表 BootGone：InputLock + DeferPush（勿 DeferPause——Pause 清 Defer=0
     * 会放行 Present，冻窗刚画就被桌面刷掉，2026-11-04）。
     * 勿 GuiCursorHide→GraphicsPresent：局部 Present 会在冻窗上留下右下角
     * 彩斑/脏条；整屏 BeginFront 铺底即可盖掉软光标。
     */
    GuiInputLock(1);
    HalVideoClearClip();
    FontTtfPreheatUtf8(Title);
    FontTtfPreheatUtf8(Body);
    GuiPresentDeferPush();
    KsendFreezeWindowPaintShow(Title, Body);
}

static void KsendFreezeWindowDrainKeyboard(void) {
    HAL_KEYBOARD_REPORT Report;

    while (HalKeyboardDequeue(&Report)) {
    }
}

static void KsendFreezeWindowEnd(void) {
    UINT32 ScreenWidth = 0;
    UINT32 ScreenHeight = 0;

    /*
     * 勿同步 GuiRedraw。仍持 Lock 时 BeginFront 盖冻窗；排空键队列再解锁，
     * 避免堆积 Enter 灌进 Shell；Pop Defer 后 Mark+Refresh 闲时合成桌面。
     */
    HalVideoGetSize(&ScreenWidth, &ScreenHeight);
    if (ScreenWidth < 320u || ScreenHeight < 200u) {
        ScreenWidth = 1024;
        ScreenHeight = 768;
    }
    GuiCursorHide();
    HalVideoDrawBeginFront();
    HalVideoFillRect(0, 0, ScreenWidth, ScreenHeight, ThemeDesktopBackground());
    HalVideoDrawEndFront();
    KsendFreezeWindowDrainKeyboard();
    GuiPresentDeferPop();
    GuiInputLock(0);
    GuiMarkDamage(0, 0, ScreenWidth, ScreenHeight);
    RequestRefresh();
    if (GuiCursorUsbReady()) {
        GuiCursorShow();
    }
}

void KsendFreezeWindowRegister(void) {
    static KSEND_FREEZE_OPS Operations;

    Operations.Begin = KsendFreezeWindowBegin;
    Operations.End = KsendFreezeWindowEnd;
    KsendFreezeOpsRegister(&Operations);
}
