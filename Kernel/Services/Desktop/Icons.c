/*
 * Icons.c — 图标延迟加载状态 / 物理像素 blit（缩放档资产见 IconBitmapLoad.c）
 *
 * PR-BOOT-fast-1：冷启动只占位；BMP/菜单由 Worker 补齐。
 * 勿在 GuiPoll→TickClock 读盘：会与 Worker iwl/FW 的 IoBreath 重入 FAT → #GP@iretq。
 *
 * Blit：按 HalVideoGetDrawNum/Den 写物理像素，使 bmp96@200% 等 1:1 铺满，
 * 避免「逻辑 48 色块再被 PixelSpan 放大」发糊。
 */
#include "DesktopPrivate.h"

#define ICON_BLIT_LINE_MAX 128u
/* 圆角底板外填色；blit 跳过，露出壁纸（字形勿用纯黑） */
#define ICON_BITMAP_KEY_COLOR 0x00000000u

/* 0=尚未加载资源；1=已加载（含失败回退纯色） */
static int sDesktopIconsReady;
/* Worker 加载完成后置位；Gui TickClock Consume 后 RequestRefresh */
static int sDesktopIconsNeedRefresh;
/* 切语言等：菜单需重建；仅 Worker / 点开始时扫盘 */
static int sDesktopMenuRebuildWant;
/* 商店装卸后：即使 Ensure 正忙也要再扫一轮 Apps 图标 */
static int sDesktopAppsReloadPending;

void DesktopIconsResetDeferred(void) {
    sDesktopIconsReady = 0;
    sDesktopIconsNeedRefresh = 0;
}

void DesktopRequestAppsReload(void) {
    sDesktopAppsReloadPending = 1;
    sDesktopIconsReady = 0;
}

void DesktopNotifyAppsChanged(void) {
    int AppsWasOpen = gMenuAppsOpen;

    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    /* 对表开机 TaskManager：盘上有 Apps/<id>/ 后须 Ensure 扫进桌面/菜单 */
    DesktopRequestAppsReload();
    DesktopEnsureIconsLoaded();
    /*
     * 商店装完若开始→应用正开着：Rebuild 已写入新行，须重新打开 flyout 并刷新，
     * 否则用户仍盯着装之前的空/旧二级菜单。
     */
    if (DesktopStartMenuIsOpen() && AppsWasOpen) {
        gMenuAppsOpen = 1;
        DesktopMarkMenuArea();
        RequestRefresh();
    }
}

int DesktopIconsConsumeNeedRefresh(void) {
    if (!sDesktopIconsNeedRefresh) {
        return 0;
    }
    sDesktopIconsNeedRefresh = 0;
    return 1;
}

void DesktopRequestMenuRebuild(void) {
    sDesktopMenuRebuildWant = 1;
}

void DesktopEnsureMenuRebuilt(void) {
    if (!sDesktopMenuRebuildWant || gDesktopBusy) {
        return;
    }
    gDesktopBusy = 1;
    RebuildStartMenu();
    sDesktopMenuRebuildWant = 0;
    gDesktopBusy = 0;
}

int DesktopIconsLoaded(void) {
    return sDesktopIconsReady ? 1 : 0;
}

void DesktopEnsureIconsLoaded(void) {
    int i;

    if (gDesktopBusy) {
        return;
    }
    if (sDesktopIconsReady && !sDesktopAppsReloadPending) {
        return;
    }
    gDesktopBusy = 1;
    do {
        sDesktopAppsReloadPending = 0;
        DesktopLoadAppIcons();
        LoadIconLayout();
        LoadDesktopIcons();
        RebuildStartMenu();
        sDesktopMenuRebuildWant = 0;
        /*
         * 对表开机 PlaceDesktopIcons：系统项 Label 钉 LocaleString。
         * 菜单 Rebuild 会再 LocaleString 其它串；装卸后须重钉，且预热图标字
         *（与 RebuildStartMenu 预热菜单同形），否则首帧 Theme/脏区空标签。
         */
        DesktopRefreshLabels();
        for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
            if (gIcons[i].Present && gIcons[i].Label && gIcons[i].Label[0]) {
                FontTtfPreheatUtf8(gIcons[i].Label);
                (void)FontStringWidth(gIcons[i].Label);
            }
        }
        sDesktopIconsNeedRefresh = 1;
        /*
         * 装卸 Notify 可能在本轮扫盘中途到达：pending 再置位则重扫，
         * 避免旧 Ensure 收尾把 ready=1 钉死、新装 desktop=yes 不上桌面。
         */
    } while (sDesktopAppsReloadPending);
    sDesktopIconsReady = 1;
    gDesktopBusy = 0;
    LogGui("Boot: Desktop Icons Loaded\n");
    DebugWrite("desktop: deferred icons+menu loaded (worker)\n");
}

UINT32 BmpSampleScaled(const BMP_IMAGE *Img, UINT32 Dx, UINT32 Dy,
                       UINT32 Dw, UINT32 Dh) {
    UINT32 Sx;
    UINT32 Sy;

    if (!Img || !Img->Pixels || Img->Width == 0 || Img->Height == 0 ||
        Dw == 0 || Dh == 0) {
        return 0;
    }
    Sx = (Dx * Img->Width) / Dw;
    Sy = (Dy * Img->Height) / Dh;
    if (Sx >= Img->Width) {
        Sx = Img->Width - 1;
    }
    if (Sy >= Img->Height) {
        Sy = Img->Height - 1;
    }
    return Img->Pixels[Sy * Img->Width + Sx];
}

void BlitBmpScaledRaw(UINT32 X, UINT32 Y, UINT32 Dw, UINT32 Dh,
                      const BMP_IMAGE *Img) {
    UINT32 Num;
    UINT32 Den;
    UINT32 PhysicalX;
    UINT32 PhysicalY;
    UINT32 PhysicalWidth;
    UINT32 PhysicalHeight;
    UINT32 Row;
    UINT32 Col;
    UINT32 Line[ICON_BLIT_LINE_MAX];
    UINT32 Chunk;

    if (!Img || !Img->Pixels || Dw == 0 || Dh == 0) {
        return;
    }
    Num = HalVideoGetDrawNum();
    Den = HalVideoGetDrawDen();
    if (Num == 0u) {
        Num = 1u;
    }
    if (Den == 0u) {
        Den = 1u;
    }
    PhysicalX = (UINT32)(((UINT64)X * (UINT64)Num) / (UINT64)Den);
    PhysicalY = (UINT32)(((UINT64)Y * (UINT64)Num) / (UINT64)Den);
    PhysicalWidth =
        (UINT32)(((UINT64)(X + Dw) * (UINT64)Num) / (UINT64)Den) - PhysicalX;
    PhysicalHeight =
        (UINT32)(((UINT64)(Y + Dh) * (UINT64)Num) / (UINT64)Den) - PhysicalY;
    if (PhysicalWidth == 0u) {
        PhysicalWidth = 1u;
    }
    if (PhysicalHeight == 0u) {
        PhysicalHeight = 1u;
    }

    for (Row = 0; Row < PhysicalHeight; Row++) {
        UINT32 RunStart = 0;
        int InRun = 0;

        for (Col = 0; Col <= PhysicalWidth; Col++) {
            int Opaque = 0;

            if (Col < PhysicalWidth) {
                UINT32 Color = BmpSampleScaled(Img, Col, Row, PhysicalWidth,
                                               PhysicalHeight);
                Opaque = (Color != ICON_BITMAP_KEY_COLOR) ? 1 : 0;
            }
            if (Opaque && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (!Opaque && InRun) {
                UINT32 Off = RunStart;

                while (Off < Col) {
                    Chunk = Col - Off;
                    if (Chunk > ICON_BLIT_LINE_MAX) {
                        Chunk = ICON_BLIT_LINE_MAX;
                    }
                    {
                        UINT32 i;
                        for (i = 0; i < Chunk; i++) {
                            Line[i] = BmpSampleScaled(Img, Off + i, Row,
                                                      PhysicalWidth,
                                                      PhysicalHeight);
                        }
                    }
                    HalVideoWriteRectPhys(PhysicalX + Off, PhysicalY + Row,
                                          Chunk, 1u, Line);
                    Off += Chunk;
                }
                InRun = 0;
            }
        }
    }
}

void BlitIconFaceRaw(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon) {
    if (Icon->BmpReady && Icon->Bmp.Pixels) {
        BlitBmpScaledRaw(X, Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, &Icon->Bmp);
        return;
    }
    GraphicsFillRectangle(X, Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                          Icon->IconColor);
}

void BlitIconFaceFree(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon) {
    UINT32 W;
    UINT32 H;
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (Icon->BmpReady && Icon->Bmp.Pixels) {
        UINT32 Line[64];

        W = DESKTOP_ICON_SIZE;
        H = DESKTOP_ICON_SIZE;
        if (W > 64u) {
            W = 64u;
        }
        if (H > 64u) {
            H = 64u;
        }
        /*
         * 遮挡路径仍走逻辑 WriteRect：PointOccupied 是逻辑坐标；
         * 与窗叠边时偶发略糊可接受，主路径 BlitIconFaceRaw 已物理采样。
         */
        for (Row = 0; Row < H; Row++) {
            InRun = 0;
            RunStart = 0;
            for (Col = 0; Col < W; Col++) {
                UINT32 Color = BmpSampleScaled(&Icon->Bmp, Col, Row, W, H);
                int Free = !PointOccupied(X + Col, Y + Row) &&
                           (Color != ICON_BITMAP_KEY_COLOR);

                Line[Col] = Color;
                if (Free && !InRun) {
                    RunStart = Col;
                    InRun = 1;
                } else if (!Free && InRun) {
                    HalVideoWriteRect(X + RunStart, Y + Row, Col - RunStart, 1,
                                      &Line[RunStart]);
                    InRun = 0;
                }
            }
            if (InRun) {
                HalVideoWriteRect(X + RunStart, Y + Row, W - RunStart, 1,
                                  &Line[RunStart]);
            }
        }
        return;
    }
    FillRectFree(X, Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, Icon->IconColor);
}
