/*
 * BootGone.c — 启动盘失根大窗倒数关机（原 Clock.c 拆出）
 */
#include "DesktopPrivate.h"
#include "Gui.h"
#include "Font.h"
#include "Locale.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalVideo.h"

#define BOOT_GONE_SECS 10u

static void BootGonePaint(const char *Title, const char *Body,
                          const char *CountLine) {
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
    BoxH = Pad * 2u + CellH * 5u + 24u;
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
    Y += CellH + 20u;
    Tw = FontStringWidth(CountLine);
    HalVideoDrawStringAt(BoxX + (BoxW - Tw) / 2u, Y, CountLine, ThemeText());
    HalVideoPresent();
}

static void BootGoneBuildCount(char *Out, UINTN OutMax, UINT32 Sec) {
    const char *Before;
    const char *After;
    UINTN i = 0;
    UINTN j;

    if (!Out || OutMax < 8u) {
        return;
    }
    Before = LocStr(MSG_BOOT_GONE_BEFORE);
    After = LocStr(MSG_BOOT_GONE_AFTER);
    if (!Before) {
        Before = "";
    }
    if (!After) {
        After = "";
    }
    for (j = 0; Before[j] && i + 1u < OutMax; j++) {
        Out[i++] = Before[j];
    }
    if (i + 1u < OutMax) {
        Out[i++] = ' ';
    }
    if (Sec >= 10u && i + 1u < OutMax) {
        Out[i++] = (char)('0' + (Sec / 10u) % 10u);
    }
    if (i + 1u < OutMax) {
        Out[i++] = (char)('0' + Sec % 10u);
    }
    if (i + 1u < OutMax) {
        Out[i++] = ' ';
    }
    for (j = 0; After[j] && i + 1u < OutMax; j++) {
        Out[i++] = After[j];
    }
    Out[i] = 0;
}

/* 等 CMOS 秒跳变一次（真机 HalTicksPerSec=250 常偏慢→倒数翻倍快，勿用 LAPIC 计秒） */
static void BootGoneWaitRtcSecond(void) {
    UINT8 Sec0 = 0;
    UINT8 Sec1 = 0;
    HAL_MOUSE_REPORT Mouse;
    HAL_KEYBOARD_REPORT Key;
    UINT32 Guard = 0;
    UINT32 Tps;
    UINT64 Deadline;

    if (HalRtcGetTime(0, 0, 0, 0, 0, &Sec0) != 0) {
        /* RTC 不可用：按标称 Tps 等 1s（可能仍偏，但有兜底） */
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
        /* 防 RTC 卡住：约 3s 标称拍后放弃本秒 */
        if ((++Guard & 0xFFFFFu) == 0) {
            return;
        }
    }
}

void DesktopBootMediumGoneShutdown(void) {
    const char *Title;
    const char *Body;
    char CountLine[96];
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

    /*
     * 锁输入；按 RTC 秒跳倒数（勿用 HalTicksPerSec——NUC 上 250 拍/s 与墙钟不符，
     * 曾见 1 墙钟秒跳 2 个数）。
     * 同步灌 TTF：关机大窗不走 Gui WantDrain；且 Gone 后门面拒 Ensure → 否则整屏方框。
     */
    GuiInputLock(1);
    GuiCursorHide();
    HalVideoClearClip();
    FontTtfPreheatUtf8(Title);
    FontTtfPreheatUtf8(Body);
    FontTtfPreheatUtf8(LocStr(MSG_BOOT_GONE_BEFORE));
    FontTtfPreheatUtf8(LocStr(MSG_BOOT_GONE_AFTER));

    for (Left = BOOT_GONE_SECS; Left >= 1u; Left--) {
        BootGoneBuildCount(CountLine, sizeof(CountLine), Left);
        FontTtfPreheatUtf8(CountLine);
        BootGonePaint(Title, Body, CountLine);
        while (HalMouseDequeue(&Mouse)) {
        }
        while (HalKeyboardDequeue(&Key)) {
        }
        BootGoneWaitRtcSecond();
    }

    BootGoneBuildCount(CountLine, sizeof(CountLine), 0);
    BootGonePaint(Title, Body, CountLine);
    LogFs("FS: msc/4 shutdown (boot medium removed)\n");
    HalCpuShutdown();
    for (;;) {
        HalCpuPark();
    }
}
