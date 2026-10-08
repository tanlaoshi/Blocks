/*
 * Gui.c — K12：桌面壳最小子集
 *
 * 【初学者】
 * 完整桌面有 Theme、窗管、托盘、开始菜单。
 * 本刀只证明：模块表能挂 Gui，且帧缓冲上能看出「有桌面」——
 * 铺底色、画一条顶栏、写标题，Present 上屏。
 */
#include "Gui.h"
#include "BootInfo.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "ToySerialConfig.h"

#define GUI_BAR_H     28u
#define GUI_BG        0x001A1F24u
#define GUI_BAR       0x002A323Cu
#define GUI_TITLE_FG  0x00E8EEF4u

static int gDesktopReady;

int GuiDesktopReady(void) {
    return gDesktopReady;
}

int GuiInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT32 W = 0;
    UINT32 H = 0;

    gDesktopReady = 0;
    HalVideoGetSize(&W, &H);
    if (Info == 0 || Info->FrameBufferSize == 0 || W < 160 || H < 80) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: skip (no FB)\n");
        return 0;
    }

    HalVideoFillRect(0, 0, W, H, GUI_BG);
    HalVideoFillRect(0, 0, W, GUI_BAR_H, GUI_BAR);
    HalVideoDrawStringAt(12, 8, "Blocks", GUI_TITLE_FG);
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }

    gDesktopReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: desktop ok\n");
    return 0;
}
