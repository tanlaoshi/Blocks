/*
 * Clock.c — 任务栏钟 / MSC 热插拔收口（usb-map / story-s3 / runtime-c-tick）
 *
 * ```
 * WorkerTask 圈首 DesktopWorkerTick ~2Hz
 *   → HalUsbMscHotPoll（UsbMsc.c；开机不调）
 *   → RemountVolumes → FsUpdateBootMediumState
 *   → BootMediumGone? Log msc/4 halt → HalCpuPark
 *   → 否则 FilesUiRefresh
 * ```
 * 旁路 U 盘热拔：不 Park。U 盘起拔启动盘应 Park（usb-boot-unplug 缺口：OsTarget 仍可能钉旁路）。
 * 钟/网托→栏脏；Present 仍 DesktopTickClock。勿 Worker 上全量 Compose。
 */
#include "DesktopPrivate.h"
#include "Gui.h" /* GuiCursorHide/Show；GuiDragActive */
#include "FilesUi.h"
#include "FileSystem.h"
#include "SerialLog.h"
#include "Hal.h"

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

    /* story-s3 / msc-desktop-hotplug：拔或插成功 → Remount + Files */
    if (HalUsbMscHotPoll() == 1) {
        (void)FileSystemRemountVolumes();
        /*
         * U 盘起再拔启动盘：Kernel 仍在 RAM，但根文件系统没了——不能当旁路热拔继续跑。
         * NUC 起拔 U 盘：启动盘仍在，走正常 Remount。
         */
        if (FileSystemBootMediumGone()) {
            LogFs("Fs: msc/4 halt (boot medium removed)\n");
            for (;;) {
                HalCpuPark();
            }
        }
        FilesUiRefresh();
        LogFs("Fs: msc/4 hotpoll remount+Files\n");
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

    /*
     * 点阵→TTF：Worker Want 只灌缓存。空闲时勿 RequestRefresh——
     * 会 ComposeBusy 丢弃鼠动 + 与 PresentCursor 抢双缓冲（空拖一直顿、留印）。
     * 标签在下次菜单/钟栏/点桌面等自然重画时换成 TTF。
     */
    if (FontTtfTakePaint()) {
        FontTtfAckPaint();
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
