/*
 * GuiPointerMouse.c — 分辨率钳窗、方向键、右键与按键边沿
 * 核心：GuiPointer.c
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Console.h"
#include "Process.h"
#include "ToySerialLog.h"

void GuiOnDisplayResize(void) {
    int i;

    HalVideoGetSize(&gGuiWindowManager.ScreenWidth, &gGuiWindowManager.ScreenHeight);
    if (gGuiWindowManager.ScreenWidth == 0) {
        gGuiWindowManager.ScreenWidth = 1024;
    }
    if (gGuiWindowManager.ScreenHeight == 0) {
        gGuiWindowManager.ScreenHeight = 768;
    }
    if (gGuiWindowManager.CursorX >= gGuiWindowManager.ScreenWidth) {
        gGuiWindowManager.CursorX = gGuiWindowManager.ScreenWidth / 2;
    }
    if (gGuiWindowManager.CursorY >= gGuiWindowManager.ScreenHeight) {
        gGuiWindowManager.CursorY = gGuiWindowManager.ScreenHeight / 2;
    }
    /*
     * 相对 USB 鼠把像素累加在驱动 Abs 里；只改 gCursor* 不同步 →
     * 下一帧仍按旧大坐标入队，Gui 钳到 Sw-1 → 十字钉在右缘，要挪很久才进桌面。
     * 平板 Absolute 路径不读 Abs，排空队列仍无害。
     */
    HalInputMouseHandoffDesktop(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
    gGuiWindowManager.CursorVisible = 0;
    gGuiWindowManager.DragWin = -1;
    gGuiWindowManager.ResizeWin = -1;
    gGuiWindowManager.ResizeEdge = RESIZE_EDGE_NONE;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        gGuiWindowManager.WinBackupValid[i] = 0;
        if (gGuiWindowManager.Windows[i].Width > gGuiWindowManager.ScreenWidth) {
            gGuiWindowManager.Windows[i].Width = gGuiWindowManager.ScreenWidth;
        }
        if (gGuiWindowManager.Windows[i].Height > gGuiWindowManager.ScreenHeight) {
            gGuiWindowManager.Windows[i].Height = gGuiWindowManager.ScreenHeight;
        }
        if (gGuiWindowManager.Windows[i].X + gGuiWindowManager.Windows[i].Width > gGuiWindowManager.ScreenWidth) {
            gGuiWindowManager.Windows[i].X = (gGuiWindowManager.ScreenWidth > gGuiWindowManager.Windows[i].Width)
                              ? (gGuiWindowManager.ScreenWidth - gGuiWindowManager.Windows[i].Width)
                              : 0;
        }
        if (gGuiWindowManager.Windows[i].Y + gGuiWindowManager.Windows[i].Height > gGuiWindowManager.ScreenHeight) {
            gGuiWindowManager.Windows[i].Y = (gGuiWindowManager.ScreenHeight > gGuiWindowManager.Windows[i].Height)
                              ? (gGuiWindowManager.ScreenHeight - gGuiWindowManager.Windows[i].Height)
                              : 0;
        }
    }

    DesktopOnDisplayResize();
    /*
     * 必须全窗合成（含客户区）。勿仅 GuiRedraw 画 chrome：
     * Settings 改 scale 时常持 GuiInputLock，旧逻辑会跳过内容重绘 →
     * 其它窗用户区空白。Compose 不受 lock 影响（lock 只挡点击）。
     */
    GuiComposeThemeScene();
    DebugWrite("Gui: display resize ");
    DebugHex32(gGuiWindowManager.ScreenWidth);
    DebugWrite("x");
    DebugHex32(gGuiWindowManager.ScreenHeight);
    DebugWrite("\n");
}


void GuiOnArrowKey(UINT8 Key) {
    UINT32 X = gGuiWindowManager.CursorX;
    UINT32 Y = gGuiWindowManager.CursorY;
    UINT32 Step = 8;

    if (Key == 0x50 && X >= Step) {
        X -= Step;
    } else if (Key == 0x4F && X + Step < gGuiWindowManager.ScreenWidth) {
        X += Step;
    } else if (Key == 0x52 && Y >= Step) {
        Y -= Step;
    } else if (Key == 0x51 && Y + Step < gGuiWindowManager.ScreenHeight) {
        Y += Step;
    } else if (Key == 0x28) {
        GUI_MOUSE_STATE M;
        M.X = gGuiWindowManager.CursorX;
        M.Y = gGuiWindowManager.CursorY;
        M.Buttons = 1;
        M.Wheel = 0;
        GuiOnMouse(&M);
        M.Buttons = 0;
        GuiOnMouse(&M);
        return;
    } else {
        return;
    }
    CursorMove(X, Y);
}


/* PR-I3：右键占位 — 仅串口记一笔；不弹菜单、不派发点击（菜单另刀） */
void GuiRightClickPlaceholder(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
    ToyLogGui("Gui: right-click\n");
}

void GuiOnMouse(const GUI_MOUSE_STATE *Mouse) {
    /* 合成进行中只跟踪坐标/钮，避免嵌套 Move/Capture 采到半成品 FB。
     * 若光标仍画在旧位置，先擦掉，否则 Compose 期间移动会留下十字印。 */
    if (gGuiWindowManager.ComposeBusy) {
        /*
         * 合成中只跟踪坐标/钮。禁止清 gGuiWindowManager.CursorVisible：
         * ComposeBegin 后、CursorRestore 前若 AP 清掉 Visible，Restore 空操作，
         * 十字留在后缓冲 → 末尾 ReadRect 把字形采进 gGuiWindowManager.Under → 镂空方块。
         */
        gGuiWindowManager.CursorX = Mouse->X;
        gGuiWindowManager.CursorY = Mouse->Y;
        gGuiWindowManager.CursorBtn = Mouse->Buttons;
        gGuiWindowManager.MousePrevBtn = Mouse->Buttons;
        return;
    }

    gGuiWindowManager.CursorBtn = Mouse->Buttons;

    if ((Mouse->Buttons & 1) && !(gGuiWindowManager.MousePrevBtn & 1)) {
        GuiHandleClick(Mouse->X, Mouse->Y);
    } else if ((Mouse->Buttons & 1) && gGuiWindowManager.ResizeWin >= 0) {
        GuiResizeUpdate(Mouse->X, Mouse->Y);
    } else if ((Mouse->Buttons & 1) && gGuiWindowManager.DragWin >= 0) {
        /* PR-G10 L2：与 GuiPollMouse 统一，按住拖动时持续更新 */
        GuiDragUpdate(Mouse->X, Mouse->Y);
    } else if ((Mouse->Buttons & 1) && DesktopIconDragActive()) {
        if (gGuiWindowManager.CursorVisible) {
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
        }
        DesktopIconDragUpdate(Mouse->X, Mouse->Y);
    }
    if (!(Mouse->Buttons & 1) && (gGuiWindowManager.MousePrevBtn & 1)) {
        int WasIconDrag = DesktopIconDragActive();

        GuiResizeEnd();
        GuiDragEnd();
        DesktopIconDragEnd();
        if (WasIconDrag) {
            GfxIrqEnter();
            CursorPaint();
            GfxPresent();
            GfxIrqLeave();
        }
    }
    /* PR-I3：右键按下边沿 → 占位回调（bit1） */
    if ((Mouse->Buttons & 2) && !(gGuiWindowManager.MousePrevBtn & 2)) {
        GuiRightClickPlaceholder(Mouse->X, Mouse->Y);
    }

    /* 边沿处理后再 Move/悬停，避免 Sync 抹掉 Settings 按下高亮 */
    GuiPointerMove(Mouse->X, Mouse->Y);

    /* PR-I2：滚轮 — Files 列表 / Shell 客户区；其它忽略 */
    if (Mouse->Wheel != 0) {
        if (GuiFocusKind() == GUI_WIN_FILES) {
            FilesUiOnWheel(Mouse->Wheel);
        } else if (GuiFocusKind() == GUI_WIN_SHELL) {
            gConsole.OnWheel(Mouse->Wheel);
        }
    }
    gGuiWindowManager.MousePrevBtn = Mouse->Buttons;
}
