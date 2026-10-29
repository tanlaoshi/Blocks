/*
 * Live.c — 运行时改缩放 / 分辨率
 * 核心：Theme.c
 */
#include "Theme.h"
#include "ThemePrivate.h"

int ThemeApplyUiScaleLive(UINT32 Percent) {
    UINT32 Prev = ThemeUiScale();
    UINT32 Next = NormalizeUiScale(Percent);

    gThemeUiScale = Next;
    gScaleUserSet = 1; /* Settings 显式缩放；勿再被 4K 默认 200% 覆盖 */
    if (HalVideoSetUiScale(Next) != 0) {
        gThemeUiScale = Prev;
        (void)HalVideoSetUiScale(Prev);
        return -1;
    }
    GuiOnDisplayResize();
    return 0;
}

int ThemeApplyDisplayLive(UINT32 Width, UINT32 Height) {
    const BOOT_HANDOFF *Info;
    UINT64 Base;
    UINT64 MapBytes;
    UINT64 Need;

    if (Width < 640 || Height < 480) {
        return -1;
    }
    if (!HalVideoCanHotSetMode()) {
        return -1;
    }

    Info = BootHandoffGet();
    Base = HalVideoFrameBufferBase();
    if (Base == 0 && Info) {
        Base = Info->FrameBufferBase;
    }
    if (Base == 0) {
        return -1;
    }

    Need = (UINT64)Width * (UINT64)Height * sizeof(UINT32);
    /* 至少 16MiB；4K ≈ 32MiB+（Settings 可到 3840×2160） */
    MapBytes = 16ull * 1024 * 1024;
    if (Info && Info->FrameBufferSize > MapBytes) {
        MapBytes = Info->FrameBufferSize;
    }
    if (Need > MapBytes) {
        MapBytes = Need;
    }
    if (VirtualMemoryMapRange(Base, Base, (UINTN)MapBytes,
                              HalVideoFbMapFlags()) != 0) {
        return -1;
    }

    if (HalVideoSetMode(Width, Height) != 0) {
        return -1;
    }
    GuiOnDisplayResize();
    return 0;
}

/*
 * ThemeBootScaleApply — 开机第一帧就用最终缩放（PR-28）
 *
 * 以前 ThemeLoad 在 GuiInitialize 里才把 scale 落到 HalVideoSetUiScale，
 * 而 Logo 早在 VideoInitialize 就以 100% 画过一次：屏上看到「先 100%
 * 大小、再跳到目标档」。这里在画 Logo 之前把同一份 DB 值先应用一次，
 * 于是第一帧就是对的，不再有跳变。
 *
 * 注意：只应用缩放，不碰主题/字体；GuiInitialize 里的 ThemeLoad 照常跑
 * （DbInitialize 幂等，这里提前初始化不影响后续）。
 */
int ThemeBootScaleApply(void) {
    char Raw[16];
    UINT32 Val;

    if (DbInitialize() != DB_OK) {
        return -1;
    }
    if (DbGet("scale", Raw, sizeof Raw) != DB_OK || Raw[0] == 0) {
        return -1;
    }
    if (ParseDecU32(Raw, &Val, 0) != 0) {
        return -1;
    }
    gThemeUiScale = NormalizeUiScale(Val);
    if (HalVideoSetUiScale(gThemeUiScale) != 0) {
        return -1;
    }
    LogGui("Theme: boot scale applied\n");
    return 0;
}
