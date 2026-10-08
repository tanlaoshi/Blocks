/*
 * HalSerialGop.c — PR-K3：boot 日志镜像到帧缓冲（最小子集）
 *
 * 【初学者】
 * SCREEN_LOG=1 时，受 TOY_SCREEN_LOG_* 放行的通道会把一行字画到屏上。
 * 用 K2 的 8×8 DrawStringAt；满屏则丢掉最旧行并重绘正文区。
 * 背缓冲开启后画在 back 上，须 Present 才上屏（无 Theme / 无平滑）。
 *
 * 正文从 Y=80 起，躲开左上角横幅。
 */
#include "HalSerial.h"
#include "HalSerialGop.h"
#include "HalVideo.h"
#include "ToySerialConfig.h"
#include "FontGlyph8x8.h"

#if TOY_SCREEN_LOG

#define BOOT_LOG_X       8u
#define BOOT_LOG_BODY_Y  80u
#define BOOT_LOG_MARGIN  8u
#define BOOT_LOG_LINE_MAX 96u
#define BOOT_VIS_MAX     48u

static int gVideoUp;
static int gGopMirror = 1;
static int gGopMute;
static UINT32 gBootLogY = BOOT_LOG_BODY_Y;
static char gBootVis[BOOT_VIS_MAX][BOOT_LOG_LINE_MAX];
static UINT32 gBootVisN;

static int ChannelGopOn(int Channel) {
    switch (Channel) {
    case TOY_SLOG_BOOT: return TOY_SCREEN_LOG_BOOT;
    case TOY_SLOG_USB:  return TOY_SCREEN_LOG_USB;
    case TOY_SLOG_SMP:  return TOY_SCREEN_LOG_SMP;
    case TOY_SLOG_GUI:  return TOY_SCREEN_LOG_GUI;
    case TOY_SLOG_NET:  return TOY_SCREEN_LOG_NET;
    case TOY_SLOG_FS:   return TOY_SCREEN_LOG_FS;
    case TOY_SLOG_MEM:  return TOY_SCREEN_LOG_MEM;
    case TOY_SLOG_DRV:  return TOY_SCREEN_LOG_DRV;
    case TOY_SLOG_MISC:
    default:            return TOY_SCREEN_LOG_MISC;
    }
}

static void CopyLine(char *Dst, const char *Src) {
    UINTN i = 0;

    if (Dst == 0) {
        return;
    }
    if (Src == 0) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && Src[i] != '\n' && Src[i] != '\r' &&
           i + 1 < BOOT_LOG_LINE_MAX) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static UINT32 VisCap(UINT32 H) {
    UINT32 Bottom;
    UINT32 Rows;

    if (H <= BOOT_LOG_BODY_Y + FONT_CELL_H + BOOT_LOG_MARGIN) {
        return 1;
    }
    Bottom = H - BOOT_LOG_MARGIN;
    Rows = (Bottom - BOOT_LOG_BODY_Y) / FONT_CELL_H;
    if (Rows == 0) {
        Rows = 1;
    }
    if (Rows > BOOT_VIS_MAX) {
        Rows = BOOT_VIS_MAX;
    }
    return Rows;
}

static void ScreenCommit(void) {
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }
}

static void RepaintBody(UINT32 W, UINT32 H) {
    UINT32 Bottom;
    UINT32 i;
    UINT32 Y;

    Bottom = (H > BOOT_LOG_MARGIN) ? (H - BOOT_LOG_MARGIN) : BOOT_LOG_BODY_Y;
    if (Bottom > BOOT_LOG_BODY_Y) {
        HalVideoFillRect(0, BOOT_LOG_BODY_Y, W, Bottom - BOOT_LOG_BODY_Y,
                         0x00000000u);
    }
    Y = BOOT_LOG_BODY_Y;
    for (i = 0; i < gBootVisN; i++) {
        HalVideoDrawStringAt(BOOT_LOG_X, Y, gBootVis[i], 0x00FFFFFFu);
        Y += FONT_CELL_H;
    }
    gBootLogY = Y;
    ScreenCommit();
}

static void PushLine(const char *Line) {
    UINT32 W = 0;
    UINT32 H = 0;
    UINT32 Cap;
    UINT32 i;

    HalVideoGetSize(&W, &H);
    if (W == 0 || H == 0) {
        return;
    }
    Cap = VisCap(H);
    if (gBootVisN >= Cap) {
        for (i = 1; i < gBootVisN; i++) {
            CopyLine(gBootVis[i - 1], gBootVis[i]);
        }
        if (gBootVisN > 0) {
            gBootVisN--;
        }
        CopyLine(gBootVis[gBootVisN], Line);
        gBootVisN++;
        RepaintBody(W, H);
        return;
    }
    CopyLine(gBootVis[gBootVisN], Line);
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gBootVis[gBootVisN],
                         0x00FFFFFFu);
    gBootVisN++;
    gBootLogY += FONT_CELL_H;
    ScreenCommit();
}

static void MirrorText(const char *Text) {
    char Line[BOOT_LOG_LINE_MAX];
    UINTN i = 0;

    if (Text == 0 || !gGopMirror || !gVideoUp || gGopMute) {
        return;
    }
    while (*Text) {
        if (*Text == '\r') {
            Text++;
            continue;
        }
        if (*Text == '\n') {
            Line[i] = 0;
            PushLine(Line);
            i = 0;
            Text++;
            continue;
        }
        if (i + 1 < BOOT_LOG_LINE_MAX) {
            Line[i++] = *Text;
        }
        Text++;
    }
    if (i > 0) {
        Line[i] = 0;
        PushLine(Line);
    }
}

void HalSerialGopTryMirror(int Channel, const char *Text) {
    if (!ChannelGopOn(Channel)) {
        return;
    }
    MirrorText(Text);
}

void HalSerialGopEnable(void) {
    UINT32 W = 0;
    UINT32 H = 0;

    HalVideoGetSize(&W, &H);
    if (W == 0 || H == 0) {
        gVideoUp = 0;
        return;
    }
    gVideoUp = 1;
    gGopMirror = 1;
    gGopMute = 0;
    gBootVisN = 0;
    gBootLogY = BOOT_LOG_BODY_Y;
    PushLine("HalSerial: screen log on");
}

void HalSerialGopMirror(int Enable) {
    gGopMirror = Enable ? 1 : 0;
}

int HalSerialGopMirroring(void) {
    return (gGopMirror && gVideoUp && !gGopMute) ? 1 : 0;
}

void HalSerialGopMute(int Mute) {
    gGopMute = Mute ? 1 : 0;
}

void HalSerialBootLogRewind(void) {
    UINT32 W = 0;
    UINT32 H = 0;

    gBootVisN = 0;
    gBootLogY = BOOT_LOG_BODY_Y;
    HalVideoGetSize(&W, &H);
    if (gVideoUp && W != 0 && H != 0) {
        RepaintBody(W, H);
    }
}

#else /* !TOY_SCREEN_LOG */

void HalSerialGopTryMirror(int Channel, const char *Text) {
    (void)Channel;
    (void)Text;
}

void HalSerialGopEnable(void) {
}

void HalSerialGopMirror(int Enable) {
    (void)Enable;
}

int HalSerialGopMirroring(void) {
    return 0;
}

void HalSerialGopMute(int Mute) {
    (void)Mute;
}

void HalSerialBootLogRewind(void) {
}

#endif
