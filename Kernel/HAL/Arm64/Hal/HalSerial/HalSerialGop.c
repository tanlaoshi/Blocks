/*
 * HalSerialGop.c — Arm64 开机屏上滚（对齐 X64 PR-S-halserial-1，不减功能）
 *
 * Ubuntu 式上滚：可见行在 RAM，满则丢最旧并整区重绘。
 * boot 镜像直写 front；横幅一次性 Present。
 * 行追加：隔几行再 FbDcacheSync（Pi VC）；勿每行全 FB clean + 假延时成「PPT」。
 */
#include "HalSerial.h"
#include "HalSerial/HalSerialPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"
#include "SerialConfig.h"

static UINT32 BootLogCellW(void) {
    UINT32 Cell = FontAdvanceX();
    if (Cell == 0) {
        Cell = 8;
    }
    return Cell;
}

static UINT32 BootLogMaxCols(void) {
    UINT32 W;
    UINT32 H;
    UINT32 Cell;
    UINT32 Cols;

    HalVideoGetSize(&W, &H);
    (void)H;
    if (W == 0) {
        W = 1024;
    }
    if (W <= BOOT_LOG_X + BOOT_LOG_MARGIN) {
        return 1;
    }
    Cell = BootLogCellW();
    Cols = (W - BOOT_LOG_X - BOOT_LOG_MARGIN) / Cell;
    if (Cols == 0) {
        Cols = 1;
    }
    if (Cols > BOOT_LOG_LINE_MAX - 1) {
        Cols = BOOT_LOG_LINE_MAX - 1;
    }
    return Cols;
}

static UINT32 BootLogVisCap(UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    UINT32 Bottom;
    UINT32 Rows;

    if (H <= BodyY + LineH + BOOT_LOG_MARGIN) {
        return 1;
    }
    Bottom = H - BOOT_LOG_MARGIN;
    Rows = (Bottom - BodyY) / LineH;
    if (Rows == 0) {
        Rows = 1;
    }
    if (Rows > BOOT_VIS_MAX) {
        Rows = BOOT_VIS_MAX;
    }
    return Rows;
}

static void BootLogCopyRow(char *Dst, const char *Src) {
    UINTN i = 0;

    if (!Dst) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i + 1 < BOOT_LOG_LINE_MAX) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

/* 每写这么多行才 dc clean 一整屏可见 FB；卷屏/收笔强制 sync */
#define BOOT_LOG_SYNC_EVERY 8u

static UINT32 gBootUnsynced;

static void BootLogFrontSync(int Force) {
    if (Force || gBootUnsynced >= BOOT_LOG_SYNC_EVERY) {
        HalVideoFbDcacheSync();
        gBootUnsynced = 0;
    }
}

static void BootLogRepaintBody(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    UINT32 Bottom;
    UINT32 i;
    UINT32 Y;

    if (W == 0) {
        W = 1024;
    }
    Bottom = (H > BOOT_LOG_MARGIN) ? (H - BOOT_LOG_MARGIN) : BodyY;
    HalVideoDrawBeginFront();
    if (Bottom > BodyY) {
        HalVideoFillRect(0, BodyY, W, Bottom - BodyY, 0x00000000u);
    }
    Y = BodyY;
    for (i = 0; i < gBootVisN; i++) {
        HalVideoDrawStringAt(BOOT_LOG_X, Y, gBootVis[i], 0x00FFFFFFu);
        Y += LineH;
    }
    HalVideoDrawEndFrontNoSync();
    gBootUnsynced++;
    BootLogFrontSync(1);
    gBootLogY = Y;
}

static void BootLogPushVisible(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 Cap = BootLogVisCap(H, LineH);
    UINT32 i;

    if (gBootVisN >= Cap) {
        for (i = 1; i < gBootVisN; i++) {
            BootLogCopyRow(gBootVis[i - 1], gBootVis[i]);
        }
        if (gBootVisN > 0) {
            gBootVisN--;
        }
        BootLogCopyRow(gBootVis[gBootVisN], gHalSerialLine);
        gBootVisN++;
        BootLogRepaintBody(W, H, LineH);
        return;
    }
    BootLogCopyRow(gBootVis[gBootVisN], gHalSerialLine);
    gBootVisN++;
    HalVideoDrawBeginFront();
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gHalSerialLine, 0x00FFFFFFu);
    HalVideoDrawEndFrontNoSync();
    gBootUnsynced++;
    BootLogFrontSync(0);
    gBootLogY += LineH;
}

void GopBannerOnce(void) {
#if !SCREEN_LOG
    gGopBanner = 1;
    return;
#else
    {
        UINT32 W;
        UINT32 H;
        UINT32 LineH;

        if (gGopBanner) {
            return;
        }
        HalVideoGetSize(&W, &H);
        if (W == 0) {
            W = 1024;
        }
        LineH = BootLogLineH();
        if (H == 0) {
            H = 720;
        }
        HalVideoClearClip();
        /*
         * 与 X64 同：直写 front；EndFront 触发 FB dcache clean（Pi VC）。
         * 先整屏铺黑，盖住 U-Boot 残留（否则只清标题条会留下「半屏脏色」）。
         */
        HalVideoDrawBeginFront();
        HalVideoFillRect(0, 0, W, H, 0x00000000u);
        HalVideoDrawStringAt(BOOT_LOG_X, BOOT_LOG_TITLE_Y, "Blocks boot", 0x00FFFF00u);
        HalVideoDrawEndFront();
        gBootVisN = 0;
        gBootLogY = BootLogBodyY(LineH);
        gLineLen = 0;
        gGopBanner = 1;
        HalVideoPresent();
    }
#endif
}

static int gGopBatch;

static void GopFlushLine(void) {
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Cap;

    gHalSerialLine[gLineLen] = '\0';
    if (gLineLen == 0) {
        return;
    }
    GopBannerOnce();
    LineH = BootLogLineH();
    HalVideoGetSize(&W, &H);
    if (H == 0) {
        H = 768;
    }
    Cap = BootLogVisCap(H, LineH);
    if (gBootVisN >= Cap && gGopBatch) {
        gLineLen = 0;
        return;
    }
    BootLogPushVisible(W, H, LineH);
    gLineLen = 0;
}

void GopWrite(const char *Text) {
    UINT32 MaxCols;

    if (!Text || !*Text) {
        return;
    }
    GopBannerOnce();
    MaxCols = BootLogMaxCols();
    while (*Text) {
        if (*Text == '\n') {
            GopFlushLine();
            Text++;
            continue;
        }
        if (gLineLen >= MaxCols) {
            GopFlushLine();
            MaxCols = BootLogMaxCols();
        }
        if (gLineLen + 1 < sizeof(gHalSerialLine)) {
            gHalSerialLine[gLineLen++] = *Text;
        }
        Text++;
    }
    /* 一段 Write 收笔：把未 sync 的尾行推到 VC */
    BootLogFrontSync(1);
}
