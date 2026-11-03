/*
 * BootGone.c — 启动盘失根：大窗提示 + 串口同文，静等 10 秒后关机
 *
 * 上屏：HalVideoDrawBeginFront → 画 → EndFront（直写 scanout + dcache；
 * 对表 boot GOP）。禁 FontTtfPreheat / GuiCursorHide→GraphicsPresent（失根后
 * 读盘或 Present 可堵死）。
 *
 * 屏上只用 ASCII：失根后 TTF 再栅格会触盘 → 方框（手测仅「启」有缓存）。
 * 串口仍打 LocStr 中文。
 *
 * 等待：须 HalDelayMilliseconds（CNTVCT）。禁 HalCpuTicks——Pi 无定时 IRQ 时
 * gCpuTicks 不涨 → 死等不关机（1103 手测）。
 *
 * 勿每秒重画。InputLocked→PresentBlocked；DeferPush 吞 GraphicsPresent。
 * 串口：ShellOwn 后须 HalSerialWriteShell。
 */
#include "DesktopPrivate.h"
#include "Gui.h"
#include "Font.h"
#include "Locale.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HalSerial.h"

#define BOOT_GONE_SECS 10u

static void BootGonePaint(const char *Title, const char *Body) {
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

    HalVideoDrawBeginFront();
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
    HalVideoDrawEndFront();
}

static void BootGoneWaitSeconds(UINT32 Seconds) {
    HAL_MOUSE_REPORT Mouse;
    HAL_KEYBOARD_REPORT Key;
    UINT32 Slice;
    UINT32 TotalSlices;

    /* 100ms 一片：其间排空 HID；时长走 CNTVCT，不靠 gCpuTicks */
    TotalSlices = Seconds * 10u;
    for (Slice = 0; Slice < TotalSlices; Slice++) {
        while (HalMouseDequeue(&Mouse)) {
        }
        while (HalKeyboardDequeue(&Key)) {
        }
        HalDelayMilliseconds(100);
    }
}

static void BootGoneSerialNotice(const char *Title, const char *Body) {
    HalSerialWriteShell("\n");
    if (Title && Title[0] != 0) {
        HalSerialWriteShell(Title);
        HalSerialWriteShell("\n");
    }
    if (Body && Body[0] != 0) {
        HalSerialWriteShell(Body);
        HalSerialWriteShell("\n");
    }
}

void DesktopBootMediumGoneShutdown(void) {
    const char *Title;
    const char *Body;
    static int sOnce;

    /* 重入：Notify→本函数→… 勿二次进 */
    if (sOnce) {
        HalCpuShutdown();
        for (;;) {
            HalCpuPark();
        }
    }
    sOnce = 1;

    /* 最先串口：与是否读盘/Present 无关 */
    HalSerialWriteShell("\nFS: boot-gone window\n");

    Title = LocStr(MSG_BOOT_GONE_TITLE);
    Body = LocStr(MSG_BOOT_GONE_BODY);
    if (!Title || Title[0] == 0) {
        Title = "启动盘已拔出";
    }
    if (!Body || Body[0] == 0) {
        Body = "系统无法继续运行。";
    }
    BootGoneSerialNotice(Title, Body);

    GuiInputLock(1);
    HalVideoClearClip();
    /*
     * DeferPause 会把 DeferPresent 清 0（放行光标）——失根窗忌用。
     * Push 吞 GraphicsPresent；InputLocked 进 PresentBlocked 挡 Shell Present。
     */
    GuiPresentDeferPush();
    /* 屏 ASCII，勿 TTF 触盘；勿 GuiCursorHide */
    BootGonePaint("Boot medium removed", "System cannot continue.");
    BootGoneWaitSeconds(BOOT_GONE_SECS);

    HalSerialWriteShell("FS: boot-gone shutdown\n");
    LogFs("FS: msc/4 shutdown (boot medium removed)\n");
    HalCpuShutdown();
    for (;;) {
        HalCpuPark();
    }
}
