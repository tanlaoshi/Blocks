/*
 * BootGone.c — 启动盘失根：大窗提示 + 串口同文，静等 10 秒后关机
 *
 * 不再倒数刷秒（曾栅 0–9 刷 FONT 串口；用户要「同大窗文案、不倒数」）。
 */
#include "DesktopPrivate.h"
#include "Gui.h"
#include "Font.h"
#include "Locale.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HalConsole.h"

#define BOOT_GONE_SECS 10u

static void BootGonePaint(const char *Title, const char *Body) {
    UINT32 Sw = 0;
    UINT32 Sh = 0;
    UINT32 BoxW;
    UINT32 BoxH;
    UINT32 BoxX;
    UINT32 BoxY;
    UINT32 CellH;
    UINT32 Tw;
    UINT32 Pad = 32u;
    UINT32 Y;

    HalVideoGetSize(&Sw, &Sh);
    if (Sw < 320u || Sh < 200u) {
        Sw = 1024;
        Sh = 768;
    }
    CellH = FontCellH();
    if (CellH < 16u) {
        CellH = 16u;
    }
    BoxW = Sw * 3u / 5u;
    if (BoxW < 400u) {
        BoxW = Sw > 40u ? Sw - 40u : Sw;
    }
    if (BoxW > Sw - 40u) {
        BoxW = Sw > 40u ? Sw - 40u : Sw;
    }
    BoxH = Pad * 2u + CellH * 3u + 24u;
    BoxX = (Sw - BoxW) / 2u;
    BoxY = (Sh - BoxH) / 2u;

    HalVideoFillRect(0, 0, Sw, Sh, 0x00101018u);
    HalVideoFillRect(BoxX, BoxY, BoxW, BoxH, ThemeDialogFace());
    HalVideoFillRect(BoxX, BoxY, BoxW, 3u, ThemeDialogBorder());
    HalVideoFillRect(BoxX, BoxY + BoxH - 3u, BoxW, 3u, ThemeDialogBorder());
    HalVideoFillRect(BoxX, BoxY, 3u, BoxH, ThemeDialogBorder());
    HalVideoFillRect(BoxX + BoxW - 3u, BoxY, 3u, BoxH, ThemeDialogBorder());

    Y = BoxY + Pad;
    Tw = FontStringWidth(Title);
    HalVideoDrawStringAt(BoxX + (BoxW - Tw) / 2u, Y, Title, ThemeText());
    Y += CellH + 16u;
    Tw = FontStringWidth(Body);
    HalVideoDrawStringAt(BoxX + (BoxW - Tw) / 2u, Y, Body, ThemeTextMuted());
    HalVideoPresent();
}

/* 等 CMOS 秒跳变一次（真机 HalTicksPerSec=250 常偏慢，勿用 LAPIC 计秒） */
static void BootGoneWaitRtcSecond(void) {
    UINT8 Sec0 = 0;
    UINT8 Sec1 = 0;
    HAL_MOUSE_REPORT Mouse;
    HAL_KEYBOARD_REPORT Key;
    UINT32 Guard = 0;
    UINT32 Tps;
    UINT64 Deadline;

    if (HalRtcGetTime(0, 0, 0, 0, 0, &Sec0) != 0) {
        Tps = HalTicksPerSec();
        if (Tps == 0) {
            Tps = 250;
        }
        Deadline = HalCpuTicks(0) + (UINT64)Tps;
        while (HalCpuTicks(0) < Deadline) {
            while (HalMouseDequeue(&Mouse)) {
            }
            while (HalKeyboardDequeue(&Key)) {
            }
            HalCpuRelax();
        }
        return;
    }
    for (;;) {
        while (HalMouseDequeue(&Mouse)) {
        }
        while (HalKeyboardDequeue(&Key)) {
        }
        if (HalRtcGetTime(0, 0, 0, 0, 0, &Sec1) == 0 && Sec1 != Sec0) {
            return;
        }
        HalCpuRelax();
        if ((++Guard & 0xFFFFFu) == 0) {
            return;
        }
    }
}

/* 另起一行离开 Blocks>；文案与大窗 Title/Body 一致 */
static void BootGoneSerialNotice(const char *Title, const char *Body) {
    HalConsoleWriteSerial("\n");
    if (Title && Title[0] != 0) {
        HalConsoleWriteSerial(Title);
        HalConsoleWriteSerial("\n");
    }
    if (Body && Body[0] != 0) {
        HalConsoleWriteSerial(Body);
        HalConsoleWriteSerial("\n");
    }
}

void DesktopBootMediumGoneShutdown(void) {
    const char *Title;
    const char *Body;
    UINT32 Left;
    HAL_MOUSE_REPORT Mouse;
    HAL_KEYBOARD_REPORT Key;

    Title = LocStr(MSG_BOOT_GONE_TITLE);
    Body = LocStr(MSG_BOOT_GONE_BODY);
    if (!Title || Title[0] == 0) {
        Title = "Boot disk removed";
    }
    if (!Body || Body[0] == 0) {
        Body = "The system cannot continue.";
    }

    GuiInputLock(1);
    GuiCursorHide();
    HalVideoClearClip();
    FontTtfPreheatUtf8(Title);
    FontTtfPreheatUtf8(Body);
    BootGonePaint(Title, Body);
    BootGoneSerialNotice(Title, Body);

    for (Left = 0; Left < BOOT_GONE_SECS; Left++) {
        while (HalMouseDequeue(&Mouse)) {
        }
        while (HalKeyboardDequeue(&Key)) {
        }
        BootGoneWaitRtcSecond();
    }

    LogFs("FS: msc/4 shutdown (boot medium removed)\n");
    HalCpuShutdown();
    for (;;) {
        HalCpuPark();
    }
}
