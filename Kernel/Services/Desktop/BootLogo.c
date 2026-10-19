/*
 * BootLogo.c — PR-BOX-2：启动占位 Logo → 清屏 → 再进 DesktopInitialize
 * T=锤、O=足球、Y=弹弓；旁衬 ToyOS。不做动画引擎。
 *
 * Video 后缓冲就绪即 Show，盖住核显 / USB / 挂盘 / 读字体。
 * Finish 时若屏上停得不够久，再补到大约一秒，避免一闪而过。
 */
#include "Desktop.h"
#include "UI.h"
#include "Hal.h"
#include "HalVideo.h"
#include "Font.h"
#include "ToySerialLog.h"
#include "BootTypes.h"

#define LOGO_BG     0x00101828u
#define LOGO_FG     0x00E8EEF4u
#define LOGO_ACCENT 0x00F0A030u
#define LOGO_BALL   0x00C87830u
#define LOGO_WOOD   0x008B5A2Bu
#define LOGO_STEEL  0x00A0A8B0u
/* 至少约 1 秒（按 ~3GHz 估），读盘已久则 Finish 几乎不再空等 */
#define LOGO_MIN_MS 1000u
#define LOGO_TSC_PER_MS 3000000ull

static int gLogoUp;
static UINT64 gLogoTsc;

static UINT64 LogoTsc(void) {
#if defined(__x86_64__)
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
#else
    static UINT64 Fake;

    Fake += LOGO_TSC_PER_MS;
    return Fake;
#endif
}

static void Fill(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 C) {
    if (W == 0 || H == 0) {
        return;
    }
    UiFillRectangle(X, Y, W, H, C);
}

static void Disk(UINT32 Cx, UINT32 Cy, UINT32 R, UINT32 C) {
    UINT32 Y;
    UINT32 X;
    UINT32 R2 = R * R;

    for (Y = 0; Y <= 2 * R; Y++) {
        for (X = 0; X <= 2 * R; X++) {
            INT32 Dx = (INT32)X - (INT32)R;
            INT32 Dy = (INT32)Y - (INT32)R;
            if ((UINT32)(Dx * Dx + Dy * Dy) <= R2) {
                HalVideoDrawPixel(Cx + X - R, Cy + Y - R, C);
            }
        }
    }
}

static void DrawHammer(UINT32 X, UINT32 Y) {
    Fill(X + 28, Y + 8, 12, 56, LOGO_WOOD);
    Fill(X + 8, Y + 4, 52, 18, LOGO_STEEL);
    Fill(X + 4, Y + 8, 8, 10, LOGO_STEEL);
    Fill(X + 56, Y + 8, 8, 10, LOGO_STEEL);
}

static void DrawFootball(UINT32 X, UINT32 Y) {
    UINT32 Cx = X + 32;
    UINT32 Cy = Y + 36;
    Disk(Cx, Cy, 28, LOGO_BALL);
    Fill(Cx - 3, Cy - 22, 6, 44, LOGO_FG);
    Fill(Cx - 10, Cy - 4, 8, 4, LOGO_FG);
    Fill(Cx + 2, Cy - 4, 8, 4, LOGO_FG);
    Fill(Cx - 10, Cy + 8, 8, 4, LOGO_FG);
    Fill(Cx + 2, Cy + 8, 8, 4, LOGO_FG);
}

static void DrawSlingshot(UINT32 X, UINT32 Y) {
    Fill(X + 28, Y + 40, 10, 28, LOGO_WOOD);
    Fill(X + 8, Y + 8, 10, 40, LOGO_WOOD);
    Fill(X + 48, Y + 8, 10, 40, LOGO_WOOD);
    Fill(X + 14, Y + 6, 38, 6, LOGO_ACCENT);
    Fill(X + 18, Y + 20, 30, 4, LOGO_ACCENT);
}

static void LogoHoldUntilMin(void) {
    UINT64 Need = (UINT64)LOGO_MIN_MS * LOGO_TSC_PER_MS;
    UINT64 Start = gLogoTsc;

    if (Start == 0) {
        Start = LogoTsc();
    }
    while (LogoTsc() - Start < Need) {
        HalInputPoll();
        HalCpuRelax();
    }
}

void DesktopBootLogoShow(void) {
    UINT32 W;
    UINT32 H;
    UINT32 BaseX;
    UINT32 BaseY;
    UINT32 Gap = 24;
    UINT32 Icon = 64;
    UINT32 RowW;
    UINT32 TextW;
    UINT32 TextX;
    const char *Title = "ToyOS";

    if (gLogoUp) {
        return;
    }
    HalVideoGetSize(&W, &H);
    if (W < 320 || H < 200) {
        return;
    }

    HalVideoClearScreen(LOGO_BG);

    RowW = Icon * 3 + Gap * 2;
    BaseX = (W > RowW) ? (W - RowW) / 2 : 0;
    BaseY = (H / 2) - 48;
    DrawHammer(BaseX, BaseY);
    DrawFootball(BaseX + Icon + Gap, BaseY);
    DrawSlingshot(BaseX + 2 * (Icon + Gap), BaseY);

    TextW = FontStringWidth(Title);
    TextX = (RowW > TextW) ? (BaseX + (RowW - TextW) / 2) : BaseX;
    HalVideoDrawStringAt(TextX, BaseY + Icon + 16, Title, LOGO_FG);
    HalVideoPresentFlush();
    ToyLogBoot("Boot: Logo\n");
    gLogoTsc = LogoTsc();
    gLogoUp = 1;
}

void DesktopBootLogoFinish(void) {
    if (!gLogoUp) {
        DesktopBootLogoShow();
    }
    LogoHoldUntilMin();
    HalVideoClearScreen(0x00000000u);
    HalVideoPresentFlush();
    ToyLogBoot("Boot: Logo Cleared\n");
    gLogoUp = 0;
    gLogoTsc = 0;
}
