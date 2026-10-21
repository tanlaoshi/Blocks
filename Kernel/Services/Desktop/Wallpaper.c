/*
 * Wallpaper.c — 壁纸缓存 / DesktopBgAt / DesktopFillRect
 *
 * gui-wall-defer：开机不 BuildWallScreen（4K 逐像素最痛）；Worker 在
 * Reveal 后 LoadBmp+Build，Gui TickClock Consume 再刷新。未就绪时用纯色/渐变。
 */
#include "DesktopPrivate.h"
#include "Scheduler.h"
#include "SerialLog.h"

/* Worker 置位；Gui DesktopTickClock Consume 后 RequestRefresh */
static int sWallNeedRefresh;

void FreeWallScreen(void) {
    if (gWallScreen && gWallScreenPages) {
        PhysicalMemoryFreePages(gWallScreen, gWallScreenPages);
    }
    gWallScreen = 0;
    gWallScreenW = 0;
    gWallScreenH = 0;
    gWallScreenPages = 0;
}

void BuildWallScreen(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 Y;
    UINT32 X;
    UINT32 WallW;
    UINT32 WallH;
    UINT32 *WallPix;
    UINT32 *Dst;

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0 || Sh == 0) {
        return;
    }
    if (gWallScreen && gWallScreenW == Sw && gWallScreenH == Sh) {
        return;
    }
    FreeWallScreen();
    if (!gWallReady || !gWall.Pixels || gWall.Width == 0 || gWall.Height == 0) {
        return;
    }
    /* 快照尺寸与指针，避免缩放循环中被重入释放 */
    WallPix = gWall.Pixels;
    WallW = gWall.Width;
    WallH = gWall.Height;
    Bytes = (UINT64)Sw * (UINT64)Sh * sizeof(UINT32);
    Pages = (UINT32)((Bytes + 4095ull) / 4096ull);
    if (Pages == 0) {
        return;
    }
    Dst = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (!Dst) {
        return;
    }
    for (Y = 0; Y < Sh; Y++) {
        UINT32 Sy = (Y * WallH) / Sh;
        if (Sy >= WallH) {
            Sy = WallH - 1;
        }
        for (X = 0; X < Sw; X++) {
            UINT32 Sx = (X * WallW) / Sw;
            if (Sx >= WallW) {
                Sx = WallW - 1;
            }
            Dst[Y * Sw + X] = WallPix[Sy * WallW + Sx];
        }
        /* 4K 缩放别饿死鼠：隔行让路 */
        if ((Y & 15u) == 0u) {
            SchedulerIoBreath();
        }
    }
    gWallScreen = Dst;
    gWallScreenPages = Pages;
    gWallScreenW = Sw;
    gWallScreenH = Sh;
}

/* 只读 BMP，不 Build（开机 / Worker 共用） */
void LoadWallpaper(void) {
    FreeWallScreen();
    if (!ThemeWallpaperEnabled()) {
        gWallReady = 0;
        return;
    }
    gWallReady = LoadBmpPath("Assets/Images/WALL.BMP", &gWall, WALL_FILE_MAX,
                             "desktop: wallpaper");
}

int DesktopWallConsumeNeedRefresh(void) {
    if (!sWallNeedRefresh) {
        return 0;
    }
    sWallNeedRefresh = 0;
    return 1;
}

/*
 * Worker：主题开壁纸且尚未缩放到屏 —— 读盘+Build。
 * 须在光标 Reveal 之后调，避免挡进桌面/露光标。
 */
void DesktopEnsureWallpaperBuilt(void) {
    if (gDesktopBusy) {
        return;
    }
    if (!ThemeWallpaperEnabled()) {
        if (gWallScreen || gWallReady) {
            FreeWallScreen();
            gWallReady = 0;
        }
        return;
    }
    if (gWallScreen && gWallScreenW != 0 && gWallScreenH != 0) {
        return;
    }
    gDesktopBusy = 1;
    if (!gWallReady) {
        LoadWallpaper();
    }
    if (gWallReady) {
        BuildWallScreen();
        if (gWallScreen) {
            sWallNeedRefresh = 1;
            LogGui("Boot: Desktop Wallpaper Built\n");
        }
    }
    gDesktopBusy = 0;
}

UINT32 DesktopBgAt(UINT32 X, UINT32 Y) {
    UINT32 Sw;
    UINT32 Sh;

    /*
     * 有缩放缓存才用壁纸像素；未 Build 前勿同步 Load/Build（gui-wall-defer）。
     */
    if (ThemeWallpaperEnabled() && gWallScreen && gWallScreenW && gWallScreenH) {
        if (X >= gWallScreenW) {
            X = gWallScreenW - 1;
        }
        if (Y >= gWallScreenH) {
            Y = gWallScreenH - 1;
        }
        return gWallScreen[Y * gWallScreenW + X];
    }
    HalVideoGetSize(&Sw, &Sh);
    if (ThemeDesktopGradientEnabled()) {
        return ThemeDesktopGradientAt(X, Y, Sw, Sh);
    }
    return ThemeDesktopBackground();
}

void DesktopFillRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Row;
    UINT32 Col;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 CopyW;
    static UINT32 sRow[512];
    UINT32 Chunk;
    UINT32 Off;

    if (W == 0 || H == 0) {
        return;
    }
    if (ThemeWallpaperEnabled() && gWallScreen) {
        Sw = gWallScreenW;
        Sh = gWallScreenH;
        if (X >= Sw || Y >= Sh) {
            return;
        }
        if (X + W > Sw) {
            W = Sw - X;
        }
        if (Y + H > Sh) {
            H = Sh - Y;
        }
        CopyW = W;
        for (Row = 0; Row < H; Row++) {
            HalVideoWriteRect(X, Y + Row, CopyW, 1,
                              &gWallScreen[(Y + Row) * Sw + X]);
        }
        return;
    }
    if (ThemeDesktopGradientEnabled()) {
        HalVideoGetSize(&Sw, &Sh);
        if (Sw == 0 || Sh == 0) {
            GraphicsFillRectangle(X, Y, W, H, ThemeDesktopBackground());
            return;
        }
        for (Row = 0; Row < H; Row++) {
            Off = 0;
            while (Off < W) {
                Chunk = W - Off;
                if (Chunk > (UINT32)(sizeof(sRow) / sizeof(sRow[0]))) {
                    Chunk = (UINT32)(sizeof(sRow) / sizeof(sRow[0]));
                }
                for (Col = 0; Col < Chunk; Col++) {
                    sRow[Col] = ThemeDesktopGradientAt(X + Off + Col, Y + Row, Sw, Sh);
                }
                HalVideoWriteRect(X + Off, Y + Row, Chunk, 1, sRow);
                Off += Chunk;
            }
        }
        return;
    }
    GraphicsFillRectangle(X, Y, W, H, ThemeDesktopBackground());
}

/* 只填未被窗占用的像素（图标拖动擦旧脚印，勿盖标题栏/客户区） */
void DesktopFillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (W == 0 || H == 0) {
        return;
    }
    for (Row = Y; Row < Y + H; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = X; Col < X + W; Col++) {
            int Free = !PointOccupied(Col, Row);
            if (Free && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (!Free && InRun) {
                if (Col > RunStart) {
                    DesktopFillRect(RunStart, Row, Col - RunStart, 1);
                }
                InRun = 0;
            }
        }
        if (InRun && X + W > RunStart) {
            DesktopFillRect(RunStart, Row, X + W - RunStart, 1);
        }
    }
}
