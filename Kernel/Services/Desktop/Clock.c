/*
 * Clock.c — 任务栏钟 / MSC 拔出（story-s3 / runtime-c-tick）
 *
 * Worker ~2Hz → HalUsbMscHotPoll==1 → FileSystemRemountVolumes
 * HotPoll：仅拔出 Release（桌面不自动 Claim；挂盘靠开机 Auto / msc hot）
 * 钟/网托采样→栏脏；Present 仍 DesktopTickClock（GuiPollMouse 末）。
 *
 * 上游：Tasks.c WorkerTask 圈首。开机挂卷：FileSystemInitialize（非本文件）。
 * 勿在 Worker 上 RequestRefresh/全量 Compose（4K 栈）。
 */
#include "DesktopPrivate.h"
#include "Gui.h" /* GuiCursorHide/Show；GuiDragActive */

static volatile int sClockBarNeedPaint;

void DesktopWorkerTick(void) {
    static UINT64 LastCheckTick;
    UINT64 Now;
    UINT32 Tps;
    UINT8 Hour = 0;
    UINT8 Minute = 0;
    int Ok;
    int NeedPaint;

    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    if (LastCheckTick != 0 && (Now - LastCheckTick) < (UINT64)(Tps / 2u)) {
        return;
    }
    LastCheckTick = Now;

    /* story-s3：~2Hz 仅拔出；1→Remount */
    if (HalUsbMscHotPoll() == 1) {
        (void)FileSystemRemountVolumes();
        DebugWrite("desktop: msc unplug remount\n");
    }

    NeedPaint = 0;
    Ok = (HalRtcGetTime(0, 0, 0, &Hour, &Minute, 0) == 0) ? 1 : 0;
    if (Ok) {
        if (!(gClockValid && Hour == gClockHour && Minute == gClockMinute)) {
            NeedPaint = 1;
        }
    }
    /* 瞬时读失败：保留上次 HH:MM；网托变仍要刷栏 */
    if (DesktopNetTrayLabelChanged()) {
        NeedPaint = 1;
    }
    if (NeedPaint) {
        sClockBarNeedPaint = 1;
    }
}

void DesktopTickClock(void) {
    int NeedPaint;

    /*
     * 拖窗/改大小时不读 CMOS、不重画任务栏，避免这一拍把鼠标卡住。
     * MSC 已在 WorkerTick，拖窗期仍拍。
     */
    if (GuiDragActive() || DesktopIconDragActive()) {
        return;
    }

    /* Worker 已加载图标则只刷新；勿在 Gui 路径读盘（与 iwl IoBreath 重入） */
    if (DesktopIconsConsumeNeedRefresh()) {
        int i;
        int Any = 0;
        UINT32 X0 = 0;
        UINT32 Y0 = 0;
        UINT32 X1 = 0;
        UINT32 Y1 = 0;

        for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
            UINT32 R;
            UINT32 B;

            if (!gIcons[i].Present) {
                continue;
            }
            R = gIcons[i].X + DESKTOP_ICON_SIZE;
            B = gIcons[i].Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + 64u;
            if (!Any) {
                X0 = gIcons[i].X;
                Y0 = gIcons[i].Y;
                X1 = R;
                Y1 = B;
                Any = 1;
            } else {
                if (gIcons[i].X < X0) {
                    X0 = gIcons[i].X;
                }
                if (gIcons[i].Y < Y0) {
                    Y0 = gIcons[i].Y;
                }
                if (R > X1) {
                    X1 = R;
                }
                if (B > Y1) {
                    Y1 = B;
                }
            }
        }
        if (Any && X1 > X0 && Y1 > Y0) {
            GuiMarkDamage(X0, Y0, X1 - X0, Y1 - Y0);
        }
        RequestRefresh();
    }

    /* 点阵已画上、TTF 随后进缓存：再合成一次，否则补位会一直留在屏上。 */
    if (FontTtfTakePaint()) {
        UINT32 Sw = 0;
        UINT32 Sh = 0;

        HalVideoGetSize(&Sw, &Sh);
        if (Sw > 0 && Sh > 0) {
            GuiMarkDamage(0, 0, Sw, Sh);
            RequestRefresh();
        }
    }

    NeedPaint = sClockBarNeedPaint;
    if (NeedPaint) {
        sClockBarNeedPaint = 0;
    }
    if (!NeedPaint) {
        return;
    }
    /*
     * 勿 BeginFront：scale≠100 时逻辑坐标直写物理 GOP → 假任务栏。
     * 后缓冲 + Present；先擦光标再铺栏，避 Alpha/开始钮烙印。
     * 须 ClearClip：Shell 客户区 clip 会让半透底被裁、开始图标仍在。
     */
    {
        UINT32 BarY;
        UINT32 Sw;
        UINT32 Sh;

        TaskbarGeom(&BarY, &Sw, &Sh);
        HalVideoClearClip();
        GuiCursorHide();
        DesktopFillRect(0, BarY, Sw, TASKBAR_H);
        DrawTaskbarRaw();
        if (gMenuOpen) {
            DrawStartMenuRaw();
        }
        if (DesktopContextMenuIsOpen()) {
            DesktopDrawContextMenu();
        }
        if (DesktopNetTrayIsOpen()) {
            DesktopNetTrayDrawPopup();
        }
        GuiCursorShow();
        HalVideoPresent();
    }
}
